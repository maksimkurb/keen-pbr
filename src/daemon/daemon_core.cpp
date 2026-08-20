#include "daemon.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <poll.h>
#include <set>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

#include "../dns/dns_probe_server.hpp" // IWYU pragma: keep
#include "../firewall/firewall.hpp"
#include "../firewall/firewall_verifier.hpp"
#include "../ipc/control_protocol.hpp"
#include "../log/logger.hpp"
#include "../util/daemon_signals.hpp"
#include "../util/ipv6_support.hpp"
#include "../util/safe_exec.hpp"
#include "../util/time_utils.hpp"
#include "scheduler.hpp"

#ifdef WITH_API
#include "../api/handlers.hpp" // IWYU pragma: keep
#include "../api/server.hpp"
#include "../api/sse_broadcaster.hpp"
#include "../api/status_stream.hpp"
#endif

namespace keen_pbr3 {

namespace {

constexpr auto SIGUSR1_DEBOUNCE_DELAY = std::chrono::milliseconds{150};
constexpr auto INTERFACE_MONITOR_RECONNECT_RETRY_DELAY =
    std::chrono::seconds{5};
constexpr std::size_t kMaxConcurrentRoutingTests = 2;
constexpr std::size_t kMaxPendingControlClients = 64;
constexpr std::size_t kMaxControlRequestBytes = std::size_t{4} * 1024U;
constexpr auto kControlIngressTimeout = std::chrono::seconds{1};
constexpr auto kControlHelloWriteTimeout = std::chrono::milliseconds{100};
constexpr auto kControlWriteTimeout = std::chrono::seconds{1};

bool send_control_bytes(int fd, std::string_view bytes,
                        std::chrono::steady_clock::duration timeout) noexcept {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const ssize_t count =
        send(fd, bytes.data() + offset, bytes.size() - offset, MSG_NOSIGNAL);
    if (count > 0) {
      offset += static_cast<std::size_t>(count);
      continue;
    }
    if (count == 0)
      return false;
    if (errno == EINTR)
      continue;
    if (errno != EAGAIN && errno != EWOULDBLOCK)
      return false;

    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline)
      return false;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - now);
    pollfd writable{fd, POLLOUT, 0};
    const int wait_ms = static_cast<int>(std::max<std::int64_t>(
        1, remaining.count()));
    const int ready = poll(&writable, 1, wait_ms);
    if (ready < 0 && errno == EINTR)
      continue;
    if (ready <= 0 || (writable.revents & POLLOUT) == 0)
      return false;
  }
  return true;
}

void send_control_response_and_close(int fd,
                                     const nlohmann::json &response) noexcept {
  try {
    const std::string frame = ipc::encode_message(response);
    (void)send_control_bytes(fd, frame, kControlWriteTimeout);
  } catch (const std::exception &error) {
    Logger::instance().error("control response encoding failed: {}",
                             error.what());
  } catch (...) {
    Logger::instance().error("control response encoding failed: unknown error");
  }
  close(fd);
}

nlohmann::json control_rule_state_json(const ControlRuntimeSnapshot::Rule &rule) {
  return {{"rule_index", rule.rule_index},
          {"set_names", rule.set_names},
          {"outbound_tag", rule.outbound_tag},
          {"action_type", static_cast<int>(rule.action_type)},
          {"fwmark", rule.fwmark}};
}

nlohmann::json control_runtime_state_json(
    const ControlRuntimeSnapshot &snapshot, FirewallBackend backend,
    bool ipv6_enabled, std::uint64_t generation,
    const std::string &config_path) {
  nlohmann::json rules = nlohmann::json::array();
  for (const auto &rule : snapshot.realized_rules)
    rules.push_back(control_rule_state_json(rule));
  const bool fallback = snapshot.runtime_state == RuntimeState::stopped ||
                        snapshot.runtime_state == RuntimeState::shutting_down;
  return {{"runtime_state", runtime_state_name(snapshot.runtime_state)},
          {"runtime_state_reason", snapshot.runtime_state_reason},
          {"routing_runtime_active", snapshot.routing_runtime_active},
          {"generation", generation},
          {"firewall_backend", firewall_backend_name(backend)},
          {"ipv6_enabled", ipv6_enabled},
          {"config_path", config_path},
          {"resolver_mode", fallback ? "fallback" : "active"},
          {"resolver_fallback_reason",
           fallback
               ? (snapshot.runtime_state == RuntimeState::shutting_down
                      ? "runtime_shutting_down"
                      : "runtime_stopped")
               : ""},
          {"realized_rules", std::move(rules)}};
}

std::int64_t
steady_duration_ms(std::chrono::steady_clock::time_point started_at) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - started_at)
      .count();
}

} // namespace

std::string get_outbound_tag(const Outbound &ob) { return ob.tag; }

const Outbound *find_outbound(const std::vector<Outbound> &outbounds,
                              const std::string &tag) {
  for (const auto &ob : outbounds) {
    if (ob.tag == tag) {
      return &ob;
    }
  }
  return nullptr;
}

Daemon::Daemon(Config config, std::string config_path, DaemonOptions opts,
               HookCommandExecutor hook_command_executor)
    : config_store_(config),
      list_service_(config.daemon.value_or(DaemonConfig{})
                        .cache_dir.value_or("/var/cache/keen-pbr"),
                    max_file_size_bytes(config)),
      config_(std::move(config)), config_path_(std::move(config_path)),
      opts_(std::move(opts)),
      firewall_(create_firewall(firewall_backend_preference(config_),
                                opts.use_raw_prerouting)),
      interface_monitor_(std::make_unique<InterfaceMonitor>(
          [this](const InterfaceMonitor::Event &event) {
            handle_interface_event(event);
          })),
      netlink_(), route_table_(netlink_), policy_rules_(netlink_),
      firewall_state_(), url_tester_(),
      outbound_marks_(allocate_outbound_marks(
          config_.fwmark.value_or(FwmarkConfig{}),
          config_.outbounds.value_or(std::vector<Outbound>{}))),
      hook_command_executor_(std::move(hook_command_executor)) {
  if (opts_.use_raw_prerouting) {
    Logger::instance().info(
        "iptables PREROUTING table: raw (IPv4); iptables OUTPUT table: mangle; "
        "IPv6 PREROUTING remains mangle");
  }

  if (!hook_command_executor_) {
    hook_command_executor_ = default_hook_command_executor;
  }

  const auto daemon_config = config_.daemon.value_or(DaemonConfig{});
  set_safe_exec_timeouts(
      std::chrono::seconds{daemon_config.exec_timeout_seconds.value_or(30)},
      std::chrono::seconds{daemon_config.exec_kill_grace_seconds.value_or(2)});

  epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
  if (epoll_fd_ < 0) {
    throw DaemonError("epoll_create1 failed: " + std::string(strerror(errno)));
  }

  // Acquire the single-instance lock before creating the externally visible
  // socket.  A rejected second service invocation must never unlink the
  // active daemon's control socket during its own setup.
  write_pid_file();

  setup_signals();
  setup_control_channel();
  setup_ipc_control_socket();

  const int64_t verify_max_bytes =
      config_.daemon.value_or(DaemonConfig{})
          .firewall_verify_max_bytes.value_or(
              static_cast<int64_t>(DEFAULT_FIREWALL_VERIFY_CAPTURE_MAX_BYTES));
  set_firewall_verifier_capture_max_bytes(
      static_cast<size_t>(verify_max_bytes));

  firewall_state_.set_outbound_marks(outbound_marks_);
  firewall_state_.set_fwmark_mask(
      fwmark_mask_value(config_.fwmark.value_or(FwmarkConfig{})));
  list_service_.ensure_dir();
  scheduler_ = std::make_unique<Scheduler>(*this);

#ifdef WITH_API
  dns_test_broadcaster_ = std::make_unique<SseBroadcaster>();
#endif
}

Daemon::~Daemon() {
  try {
    accept_posted_control_tasks_.store(false, std::memory_order_release);
    lifecycle_executor_.shutdown();
    resolver_hook_executor_.shutdown();
    resolver_io_executor_.shutdown();
    routing_test_executor_.shutdown();
    blocking_executor_.shutdown();
    if (rollback_config_fd_ >= 0) {
      close(rollback_config_fd_);
      rollback_config_fd_ = -1;
    }

    // Stop the acceptor while its eventfd wake target is still valid.
    remove_ipc_control_socket();
    if (control_fd_ >= 0) {
      epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, control_fd_, nullptr);
      close(control_fd_);
      control_fd_ = -1;
    }
    if (signal_fd_ >= 0) {
      epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, signal_fd_, nullptr);
      close(signal_fd_);
      signal_fd_ = -1;
    }
    if (epoll_fd_ >= 0) {
      close(epoll_fd_);
      epoll_fd_ = -1;
    }

    unblock_daemon_signals_for_current_thread();
  } catch (const std::exception &e) {
    Logger::instance().error("Daemon destruction cleanup failed: {}", e.what());
  } catch (...) {
    Logger::instance().error(
        "Daemon destruction cleanup failed: unknown error");
  }
}

void Daemon::setup_control_channel() {
  control_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (control_fd_ < 0) {
    throw DaemonError("eventfd failed: " + std::string(strerror(errno)));
  }

  struct epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = control_fd_;
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, control_fd_, &ev) < 0) {
    throw DaemonError("epoll_ctl add control_fd failed: " +
                      std::string(strerror(errno)));
  }
}

void Daemon::setup_ipc_control_socket() {
  ipc_control_socket_path_ = KEEN_PBR_CONTROL_SOCKET;
  if (ipc_control_socket_path_.size() >= sizeof(sockaddr_un::sun_path)) {
    throw DaemonError("control socket path is too long");
  }

  std::filesystem::create_directories(
      std::filesystem::path(ipc_control_socket_path_).parent_path());
  struct stat existing{};
  if (lstat(ipc_control_socket_path_.c_str(), &existing) == 0) {
    if (!S_ISSOCK(existing.st_mode) ||
        unlink(ipc_control_socket_path_.c_str()) != 0) {
      throw DaemonError("unsafe stale control socket path");
    }
  } else if (errno != ENOENT) {
    throw DaemonError("failed to inspect control socket path: " +
                      std::string(strerror(errno)));
  }

  ipc_control_fd_ =
      socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (ipc_control_fd_ < 0) {
    throw DaemonError("control socket create failed: " +
                      std::string(strerror(errno)));
  }
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::strncpy(address.sun_path, ipc_control_socket_path_.c_str(),
               sizeof(address.sun_path) - 1U);
  if (bind(ipc_control_fd_, reinterpret_cast<const sockaddr *>(&address),
           sizeof(address)) != 0 ||
      listen(ipc_control_fd_, 16) != 0 ||
      chown(ipc_control_socket_path_.c_str(), 0, 0) != 0 ||
      chmod(ipc_control_socket_path_.c_str(), 0600) != 0) {
    const std::string error = strerror(errno);
    remove_ipc_control_socket();
    throw DaemonError("control socket setup failed: " + error);
  }
  ipc_accept_running_.store(true, std::memory_order_release);
  ipc_accept_thread_ = std::thread([this] { run_ipc_control_acceptor(); });
}

void Daemon::run_ipc_control_acceptor() noexcept {
  struct PendingClient {
    int fd{-1};
    std::string frame;
    std::optional<std::size_t> expected_size;
    std::chrono::steady_clock::time_point deadline;
  };
  std::vector<PendingClient> pending;
  pending.reserve(kMaxPendingControlClients);

  const auto reject = [](int fd, std::string_view code,
                         std::string_view message) {
    send_control_response_and_close(
        fd, {{"protocol_version", ipc::kControlProtocolVersion},
             {"request_id", nullptr},
             {"ok", false},
             {"error", {{"code", code}, {"message", message}}}});
  };

  while (ipc_accept_running_.load(std::memory_order_acquire)) {
    std::vector<pollfd> poll_fds;
    poll_fds.reserve(pending.size() + 1U);
    poll_fds.push_back({ipc_control_fd_, POLLIN, 0});
    for (const auto &client : pending)
      poll_fds.push_back({client.fd, POLLIN, 0});

    const int ready = poll(poll_fds.data(), poll_fds.size(), 100);
    if (ready < 0) {
      if (errno == EINTR)
        continue;
      if (ipc_accept_running_.load(std::memory_order_acquire)) {
        Logger::instance().error("control socket acceptor poll failed: {}",
                                 strerror(errno));
      }
      break;
    }

    if ((poll_fds.front().revents & POLLIN) != 0) {
      while (ipc_accept_running_.load(std::memory_order_acquire)) {
        const int client = accept4(ipc_control_fd_, nullptr, nullptr,
                                   SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client < 0) {
          if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;
          if (errno == EINTR)
            continue;
          if (ipc_accept_running_.load(std::memory_order_acquire)) {
            Logger::instance().error("control socket accept failed: {}",
                                     strerror(errno));
          }
          break;
        }

        // HELO is a transport-level greeting. It deliberately precedes request
        // reads, peer authorization, JSON parsing, and all executor queues.
        if (!send_control_bytes(client, ipc::kControlHelloMarker,
                                kControlHelloWriteTimeout)) {
          close(client);
          continue;
        }

        std::size_t completed_count = 0;
        {
          KPBR_LOCK_GUARD(ipc_accepted_clients_mutex_);
          completed_count = ipc_accepted_clients_.size();
        }
        if (pending.size() + completed_count >= kMaxPendingControlClients) {
          reject(client, "busy", "control ingress queue is full");
          continue;
        }
        pending.push_back(
            {client, {}, std::nullopt,
             std::chrono::steady_clock::now() + kControlIngressTimeout});
      }
    }

    const auto now = std::chrono::steady_clock::now();
    for (std::size_t index = pending.size(); index-- > 0;) {
      auto &client = pending[index];
      short revents = 0;
      if (poll_fds.size() > index + 1U)
        revents = poll_fds[index + 1U].revents;
      bool failed = false;
      bool complete = false;

      if ((revents & POLLIN) != 0) {
        char buffer[4096];
        for (;;) {
          const ssize_t count = recv(client.fd, buffer, sizeof(buffer), 0);
          if (count > 0) {
            client.frame.append(buffer, static_cast<std::size_t>(count));
            if (!client.expected_size.has_value() &&
                client.frame.size() >= sizeof(std::uint32_t)) {
              std::uint32_t network_size = 0;
              std::memcpy(&network_size, client.frame.data(), sizeof(network_size));
              const std::size_t payload_size = ntohl(network_size);
              if (payload_size > kMaxControlRequestBytes) {
                failed = true;
                break;
              }
              client.expected_size = sizeof(network_size) + payload_size;
            }
            if (client.expected_size.has_value() &&
                client.frame.size() >= *client.expected_size) {
              complete = client.frame.size() == *client.expected_size;
              failed = !complete;
              break;
            }
            continue;
          }
          if (count == 0) {
            failed = true;
            break;
          }
          if (errno == EINTR)
            continue;
          if (errno != EAGAIN && errno != EWOULDBLOCK)
            failed = true;
          break;
        }
      }
      if (!complete &&
          ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 ||
           now >= client.deadline)) {
        failed = true;
      }
      if (!complete && !failed)
        continue;

      const int fd = client.fd;
      std::string frame = std::move(client.frame);
      pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(index));
      if (failed) {
        reject(fd, "protocol_error", "incomplete control request");
        continue;
      }

      nlohmann::json request = nlohmann::json::object();
      try {
        ucred peer{};
        socklen_t peer_length = sizeof(peer);
        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &peer_length) != 0)
          throw ipc::ControlProtocolError("unable to verify control peer");
        request = ipc::decode_message(frame);
        ipc::validate_request_envelope(request);
        bool queued = false;
        {
          KPBR_LOCK_GUARD(ipc_accepted_clients_mutex_);
          if (ipc_accepted_clients_.size() < kMaxPendingControlClients) {
            ipc_accepted_clients_.push_back(
                {fd, static_cast<std::uint32_t>(peer.uid), std::move(request)});
            queued = true;
          }
        }
        if (!queued) {
          reject(fd, "busy", "control dispatch queue is full");
          continue;
        }
        try {
          wake_control_loop();
        } catch (const std::exception &error) {
          Logger::instance().error("control socket wake failed: {}", error.what());
        }
      } catch (const std::exception &error) {
        send_control_response_and_close(
            fd, ipc::make_error_response(request, "protocol_error", error.what()));
      }
    }
  }

  for (const auto &client : pending) {
    close(client.fd);
  }
}

void Daemon::remove_ipc_control_socket() noexcept {
  ipc_accept_running_.store(false, std::memory_order_release);
  if (ipc_control_fd_ >= 0) {
    (void)shutdown(ipc_control_fd_, SHUT_RDWR);
  }
  if (ipc_accept_thread_.joinable()) {
    ipc_accept_thread_.join();
  }
  if (ipc_control_fd_ >= 0) {
    close(ipc_control_fd_);
    ipc_control_fd_ = -1;
  }
  {
    KPBR_LOCK_GUARD(ipc_accepted_clients_mutex_);
    for (const auto &client : ipc_accepted_clients_)
      close(client.fd);
    ipc_accepted_clients_.clear();
  }
  if (!ipc_control_socket_path_.empty()) {
    (void)unlink(ipc_control_socket_path_.c_str());
    ipc_control_socket_path_.clear();
  }
}

bool Daemon::try_begin_routing_test() {
  std::size_t current = routing_tests_inflight_.load(std::memory_order_acquire);
  while (current < kMaxConcurrentRoutingTests) {
    if (routing_tests_inflight_.compare_exchange_weak(
            current, current + 1, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
      return true;
    }
  }
  return false;
}

void Daemon::finish_routing_test() {
  routing_tests_inflight_.fetch_sub(1, std::memory_order_acq_rel);
}

void Daemon::handle_ipc_control_socket() {
  while (true) {
    IpcControlRequest accepted;
    {
      KPBR_LOCK_GUARD(ipc_accepted_clients_mutex_);
      if (ipc_accepted_clients_.empty())
        return;
      accepted = std::move(ipc_accepted_clients_.front());
      ipc_accepted_clients_.pop_front();
    }
    const int client = accepted.fd;
    nlohmann::json request = std::move(accepted.request);
    nlohmann::json response;
    try {
      const std::string operation = request.at("operation").get<std::string>();
      const bool resolver_hook_inflight =
          ipc_resolver_hook_inflight_.load(std::memory_order_acquire);
      const bool read_only_operation =
          operation == "status" || operation == "resolver-config-hash" ||
          operation == "test-routing";
      const bool startup_mutation =
          runtime_state_machine_.state() == RuntimeState::starting &&
          (operation == "download" || operation == "test-routing");
      if (resolver_hook_inflight && operation != "generate-resolver-config" &&
          operation != "resolver-config-generated" &&
          !read_only_operation) {
        response =
            ipc::make_error_response(request, "busy",
                                     "mutating control operations are "
                                     "unavailable during resolver reload");
      } else if (startup_mutation) {
        response = ipc::make_error_response(
            request, "busy",
            "routing runtime initialization is still in progress");
      } else {
        const bool root_peer = accepted.peer_uid == 0;
        if (!root_peer) {
          throw ipc::ControlProtocolError(
              "control peer is not authorized for this operation");
        }
        if (operation != "status" && operation != "resolver-config-hash" &&
            operation != "download" && operation != "test-routing" &&
            operation != "generate-resolver-config" &&
            operation != "resolver-config-generated") {
          response = ipc::make_error_response(request, "unsupported_operation",
                                              "unsupported control operation");
        } else if (operation == "resolver-config-generated") {
          const auto generation = request.value("generation", std::uint64_t{0});
          const auto hash = request.value("hash", "");
          if (!accept_resolver_generated_hash(generation, hash)) {
            response = ipc::make_error_response(
                request, "stale_generation",
                "resolver output belongs to a stale runtime generation");
          } else {
            response = {{"protocol_version", ipc::kControlProtocolVersion},
                        {"request_id", request.at("request_id")},
                        {"ok", true},
                        {"result", {{"generation", generation}}}};
          }
        } else if (operation == "status" ||
                   operation == "resolver-config-hash" ||
                   operation == "test-routing" ||
                   operation == "generate-resolver-config") {
          const bool include_realized_rules =
              operation == "status" || operation == "test-routing";
          const auto snapshot =
              runtime_state_store_.control_snapshot(include_realized_rules);
          if (snapshot.runtime_state == RuntimeState::applying &&
              operation != "generate-resolver-config") {
            response = ipc::make_error_response(
                request, "busy", "runtime configuration is being applied");
          } else {
            const bool ipv6_enabled = resolver_generation_snapshot_.has_value()
                                          ? resolver_generation_snapshot_->ipv6_enabled
                                          : resolve_ipv6_support(config_).enabled;
            response = {
                {"protocol_version", ipc::kControlProtocolVersion},
                {"request_id", request.at("request_id")},
                {"ok", true},
                {"result", control_runtime_state_json(
                               snapshot, firewall_->backend(), ipv6_enabled,
                               runtime_generation_.load(std::memory_order_acquire),
                               config_path_)}};
          }
        } else if (operation == "download") {
          bool expected = false;
          if (!ipc_mutation_inflight_.compare_exchange_strong(
                  expected, true, std::memory_order_acq_rel)) {
            response = ipc::make_error_response(
                request, "busy", "another control mutation is in progress");
            send_control_response_and_close(client, response);
            continue;
          }
          struct MutationGate {
            std::atomic<bool> &flag;
            ~MutationGate() { flag.store(false, std::memory_order_release); }
          } gate{ipc_mutation_inflight_};
          const bool reload = request.value("reload", false);
          RemoteListsRefreshResult refresh;
          bool reloaded = false;
          if (reload) {
            auto result = execute_remote_list_refresh(nullptr, "ipc");
            refresh = std::move(result.refresh_result);
            reloaded = result.reloaded;
          } else {
            const auto relevant = collect_relevant_list_names(config_);
            const auto dns_relevant = collect_dns_relevant_list_names(config_);
            refresh = list_service_.refresh_remote_lists(
                config_, outbound_marks_, &relevant, nullptr, &dns_relevant);
          }
          response = {{"protocol_version", ipc::kControlProtocolVersion},
                      {"request_id", request.at("request_id")},
                      {"ok", refresh.failed_lists.empty()},
                      {"result",
                       {{"refreshed_lists", refresh.refreshed_lists},
                        {"changed_lists", refresh.changed_lists},
                        {"failed_lists", refresh.failed_lists},
                        {"reloaded", reloaded}}}};
        }
      }
    } catch (const std::exception &error) {
      response =
          ipc::make_error_response(request, "protocol_error", error.what());
    }
    send_control_response_and_close(client, response);
  }
}

void Daemon::wake_control_loop() {
  const uint64_t inc = 1;
  ssize_t n = write(control_fd_, &inc, sizeof(inc));
  if (n < 0 && errno != EAGAIN) {
    throw DaemonError("eventfd write failed: " + std::string(strerror(errno)));
  }
}

bool Daemon::is_event_loop_thread() const {
  return event_loop_thread_id_.load(std::memory_order_relaxed) ==
         std::this_thread::get_id();
}

void Daemon::enqueue_control_task(std::function<void()> task,
                                  bool wait_for_completion,
                                  const std::string &label) {
  if (!task) {
    return;
  }

  const auto effective_label =
      label.empty() ? std::string("control-task") : label;
  const TraceId trace_id = ensure_trace_id();
  auto run_inline = [task = std::move(task), effective_label,
                     trace_id]() mutable {
    ScopedTraceContext trace_scope(trace_id);
    const auto started_at = std::chrono::steady_clock::now();
    Logger::instance().trace("control_task_start", "label={} mode=inline",
                             effective_label);
    try {
      task();
      Logger::instance().trace("control_task_end",
                               "label={} mode=inline duration_ms={}",
                               effective_label, steady_duration_ms(started_at));
    } catch (const std::exception &e) {
      Logger::instance().trace(
          "control_task_error", "label={} mode=inline duration_ms={} error={}",
          effective_label, steady_duration_ms(started_at), e.what());
      throw;
    } catch (...) {
      Logger::instance().trace(
          "control_task_error",
          "label={} mode=inline duration_ms={} error=unknown", effective_label,
          steady_duration_ms(started_at));
      throw;
    }
  };

  if (!event_loop_active_.load(std::memory_order_acquire) ||
      event_loop_thread_id_.load(std::memory_order_relaxed) ==
          std::thread::id{}) {
    run_inline();
    return;
  }

  if (event_loop_thread_id_.load(std::memory_order_relaxed) ==
      std::this_thread::get_id()) {
    run_inline();
    return;
  }

  if (wait_for_completion) {
    auto done = std::make_shared<std::promise<void>>();
    auto fut = done->get_future();
    {
      KPBR_LOCK_GUARD(control_tasks_mutex_);
      control_tasks_.push_back(ControlTask{
          .callback =
              [cmd = std::move(run_inline), done]() mutable {
                try {
                  cmd();
                  done->set_value();
                } catch (...) {
                  done->set_exception(std::current_exception());
                }
              },
          .label = effective_label,
          .trace_id = trace_id,
      });
    }
    Logger::instance().trace("control_task_enqueue", "label={} wait=true",
                             effective_label);
    wake_control_loop();
    fut.get();
    return;
  }

  {
    KPBR_LOCK_GUARD(control_tasks_mutex_);
    control_tasks_.push_back(ControlTask{
        .callback = std::move(run_inline),
        .label = effective_label,
        .trace_id = trace_id,
    });
  }
  Logger::instance().trace("control_task_enqueue", "label={} wait=false",
                           effective_label);
  wake_control_loop();
}

bool Daemon::post_control_task(std::function<void()> task,
                               const std::string &label) {
  if (!task)
    return false;
  if (!accept_posted_control_tasks_.load(std::memory_order_acquire)) {
    Logger::instance().trace("control_task_skip",
                             "label={} reason=posted_tasks_disabled",
                             label.empty() ? "post-control-task" : label);
    return false;
  }

  const auto effective_label =
      label.empty() ? std::string("post-control-task") : label;
  const TraceId trace_id = ensure_trace_id();
  auto traced_task = [task = std::move(task), effective_label,
                      trace_id]() mutable {
    ScopedTraceContext trace_scope(trace_id);
    const auto started_at = std::chrono::steady_clock::now();
    Logger::instance().trace("control_task_start", "label={} mode=posted",
                             effective_label);
    try {
      task();
      Logger::instance().trace("control_task_end",
                               "label={} mode=posted duration_ms={}",
                               effective_label, steady_duration_ms(started_at));
    } catch (const std::exception &e) {
      Logger::instance().trace(
          "control_task_error", "label={} mode=posted duration_ms={} error={}",
          effective_label, steady_duration_ms(started_at), e.what());
      throw;
    } catch (...) {
      Logger::instance().trace(
          "control_task_error",
          "label={} mode=posted duration_ms={} error=unknown", effective_label,
          steady_duration_ms(started_at));
      throw;
    }
  };

  {
    KPBR_LOCK_GUARD(control_tasks_mutex_);
    control_tasks_.push_back(ControlTask{
        .callback = std::move(traced_task),
        .label = effective_label,
        .trace_id = trace_id,
    });
  }
  Logger::instance().trace("control_task_enqueue",
                           "label={} wait=false mode=post", effective_label);
  wake_control_loop();
  return true;
}

void Daemon::enqueue_control_command(std::function<void()> command,
                                     bool wait_for_completion,
                                     const std::string &label) {
  enqueue_control_task(std::move(command), wait_for_completion, label);
}

void Daemon::handle_control_commands() {
  uint64_t counter = 0;
  while (read(control_fd_, &counter, sizeof(counter)) > 0) {
  }
  if (errno != EAGAIN && errno != EWOULDBLOCK) {
    throw DaemonError("eventfd read failed: " + std::string(strerror(errno)));
  }

  // The IPC acceptor uses this eventfd after it has completed nonblocking
  // request framing. Drain those ready requests before potentially expensive
  // internal control tasks so CLI responses are not left waiting in the queue.
  handle_ipc_control_socket();

  std::vector<ControlTask> commands;
  {
    KPBR_LOCK_GUARD(control_tasks_mutex_);
    commands.swap(control_tasks_);
  }

  for (auto &command : commands) {
    command.callback();
  }
}

void Daemon::setup_signals() {
  block_daemon_signals_for_current_thread();
  sigset_t mask = daemon_signal_mask();

  signal_fd_ = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
  if (signal_fd_ < 0) {
    throw DaemonError("signalfd failed: " + std::string(strerror(errno)));
  }

  struct epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = signal_fd_;
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, signal_fd_, &ev) < 0) {
    throw DaemonError("epoll_ctl add signalfd failed: " +
                      std::string(strerror(errno)));
  }
}

void Daemon::handle_signal() {
  struct signalfd_siginfo info{};
  ssize_t n = read(signal_fd_, &info, sizeof(info));
  if (n != sizeof(info)) {
    return;
  }

  switch (info.ssi_signo) {
  case SIGTERM:
  case SIGINT:
    running_.store(false, std::memory_order_release);
    break;
  case SIGUSR1:
    handle_sigusr1();
    break;
  case SIGHUP:
    handle_sighup();
    break;
  default:
    break;
  }
}

void Daemon::handle_sigusr1() {
  auto &log = Logger::instance();
  log.info("SIGUSR1: scheduling firewall refresh...");
  schedule_sigusr1_runtime_refresh();
}

void Daemon::schedule_sigusr1_runtime_refresh() {
  if (sigusr1_refresh_task_id_ >= 0) {
    scheduler_->cancel(sigusr1_refresh_task_id_);
    sigusr1_refresh_task_id_ = -1;
  }

  sigusr1_refresh_task_id_ = scheduler_->schedule_oneshot(
      SIGUSR1_DEBOUNCE_DELAY,
      [this]() {
        sigusr1_refresh_task_id_ = -1;
#ifdef WITH_API
        if (operation_coordinator_.busy()) {
          Logger::instance().info("SIGUSR1: config operation is active; "
                                  "deferring runtime reconcile");
          schedule_sigusr1_runtime_refresh();
          return;
        }
#endif
        Logger::instance().info("SIGUSR1: applying firewall refresh...");
        refresh_iproute_and_firewall_runtime();
        Logger::instance().info("SIGUSR1: firewall refresh complete.");
      },
      "sigusr1-runtime-refresh");
}

void Daemon::handle_sighup() {
  auto &log = Logger::instance();
#ifndef WITH_API
  log.warn("SIGHUP: configuration reload is unavailable in headless builds");
  return;
#else
  log.info("SIGHUP: full reload starting...");
  if (!operation_coordinator_.try_begin("sighup-reload")) {
    log.warn("SIGHUP: reload skipped because another config operation is in "
             "progress");
    return;
  }
  const bool enqueued =
      blocking_executor_.try_post("sighup-config-transaction", [this] {
        ConfigApplyResult result;
        try {
          std::ifstream input(config_path_);
          if (!input.is_open()) {
            throw DaemonError("Cannot open config file: " + config_path_);
          }
          std::ostringstream contents;
          contents << input.rdbuf();
          Config candidate = parse_config(contents.str());
          validate_config(candidate);
          result = apply_validated_config_via_control_task(std::move(candidate),
                                                           "", false);
        } catch (const std::exception &error) {
          result.error = error.what();
        }
        post_control_task(
            [this, result = std::move(result)] {
              operation_coordinator_.finish();
              if (result.error.empty()) {
                Logger::instance().info("SIGHUP: full reload complete.");
              } else {
                Logger::instance().error("SIGHUP: reload failed: {}",
                                         result.error);
              }
            },
            "sighup-config-transaction-complete");
      });
  if (!enqueued) {
    operation_coordinator_.finish();
    log.error("SIGHUP: reload could not be scheduled");
  }
#endif
}

void Daemon::refresh_iproute_and_firewall_runtime(StatusPublishScope scope) {
  auto &log = Logger::instance();
  try {
    reconcile_static_routing();
    apply_firewall(runtime_refresh_firewall_mode());
    publish_runtime_state(scope);
    log.info("Runtime iproute and firewall refresh complete.");
  } catch (const std::exception &e) {
    log.error("Runtime iproute and firewall refresh failed: {}", e.what());
  }
}

FirewallApplyMode Daemon::runtime_refresh_firewall_mode() const {
  return config_.daemon.value_or(DaemonConfig{})
                 .reuse_static_sets_on_runtime_refresh.value_or(true)
             ? FirewallApplyMode::RulesOnly
             : FirewallApplyMode::PreserveSets;
}

bool Daemon::is_interface_outbound_in_use(
    const std::string &interface_name) const {
  const auto outbounds = config_.outbounds.value_or(std::vector<Outbound>{});
  return std::any_of(outbounds.begin(), outbounds.end(),
                     [&interface_name](const Outbound &outbound) {
                       return outbound.type == OutboundType::INTERFACE &&
                              outbound.interface.has_value() &&
                              outbound.interface.value() == interface_name;
                     });
}

void Daemon::handle_interface_event(const InterfaceMonitor::Event &event) {
  auto &log = Logger::instance();
  if (!event.administrative_state_changed ||
      !is_interface_outbound_in_use(event.interface_name)) {
#ifdef WITH_API
    if (status_stream_)
      status_stream_->reconcile(StatusUpdate::Interfaces |
                                StatusUpdate::Outbounds);
#endif
    return;
  }

  log.info("Interface {} state changed to {}, iproute and firewall refresh "
           "triggered",
           event.interface_name, event.is_up ? "UP" : "DOWN");
  refresh_iproute_and_firewall_runtime(
      StatusPublishScope::OutboundsAndInterfaces);
}

void Daemon::handle_interface_monitor_events(uint32_t events) {
  constexpr uint32_t relevant_events = EPOLLIN | EPOLLERR | EPOLLHUP;
  if ((events & relevant_events) == 0) {
    return;
  }
  if (!interface_monitor_) {
    return;
  }

  if ((events & (EPOLLERR | EPOLLHUP)) != 0 && (events & EPOLLIN) == 0) {
    Logger::instance().error(
        "Interface monitor fd reported epoll error/hangup");
    reconnect_interface_monitor();
    return;
  }

  try {
    interface_monitor_->handle_events();
  } catch (const std::exception &e) {
    Logger::instance().error("Interface monitor event handling failed: {}",
                             e.what());
    reconnect_interface_monitor();
  }
}

void Daemon::register_interface_monitor_fd() {
  if (!interface_monitor_) {
    return;
  }

  const int fd = interface_monitor_->fd();
  add_fd(
      fd, EPOLLIN,
      [this](uint32_t events) { handle_interface_monitor_events(events); },
      true, "interface-monitor");
  interface_monitor_fd_ = fd;
}

void Daemon::unregister_interface_monitor_fd() {
  if (!interface_monitor_fd_) {
    return;
  }

  remove_fd(*interface_monitor_fd_, true, "interface-monitor");
  interface_monitor_fd_.reset();
}

void Daemon::schedule_interface_monitor_reconnect_retry() {
  if (!scheduler_ || interface_monitor_reconnect_task_id_ >= 0) {
    return;
  }

  interface_monitor_reconnect_task_id_ = scheduler_->schedule_oneshot(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          INTERFACE_MONITOR_RECONNECT_RETRY_DELAY),
      [this]() {
        interface_monitor_reconnect_task_id_ = -1;
        reconnect_interface_monitor();
      },
      "interface-monitor-reconnect");
}

void Daemon::reconnect_interface_monitor() {
  enqueue_control_task(
      [this]() {
        if (!interface_monitor_) {
          return;
        }

        unregister_interface_monitor_fd();

        try {
          interface_monitor_->reconnect();
          register_interface_monitor_fd();
          Logger::instance().warn(
              "Interface monitor reconnected after netlink error");
        } catch (const std::exception &e) {
          Logger::instance().error("Interface monitor reconnect failed: {}",
                                   e.what());
          schedule_interface_monitor_reconnect_retry();
        }
      },
      false, "interface-monitor-reconnect");
}

void Daemon::add_fd(int fd, uint32_t events, FdCallback cb,
                    bool wait_for_completion, const std::string &label) {
  enqueue_control_task(
      [this, fd, events, cb = std::move(cb)]() mutable {
        struct epoll_event ev{};
        ev.events = events;
        ev.data.fd = fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
          throw DaemonError("epoll_ctl add fd failed: " +
                            std::string(strerror(errno)));
        }

        KPBR_LOCK_GUARD(fd_entries_mutex_);
        fd_entries_.push_back({fd, std::move(cb)});
      },
      wait_for_completion, label.empty() ? "add-fd" : label);
}

void Daemon::remove_fd(int fd, bool wait_for_completion,
                       const std::string &label) {
  enqueue_control_task(
      [this, fd]() {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);

        KPBR_LOCK_GUARD(fd_entries_mutex_);
        fd_entries_.erase(
            std::remove_if(fd_entries_.begin(), fd_entries_.end(),
                           [fd](const FdEntry &e) { return e.fd == fd; }),
            fd_entries_.end());
      },
      wait_for_completion, label.empty() ? "remove-fd" : label);
}

void Daemon::dispatch_event_fd(int fd, uint32_t events) {
  if (fd == signal_fd_) {
    handle_signal();
    return;
  }
  if (fd == control_fd_) {
    handle_control_commands();
    return;
  }

  FdCallback callback;
  {
    KPBR_LOCK_GUARD(fd_entries_mutex_);
    for (auto &entry : fd_entries_) {
      if (entry.fd == fd) {
        callback = entry.callback;
        break;
      }
    }
  }
  if (callback) {
    callback(events);
  }
}

void Daemon::run_event_loop() {
  constexpr int MAX_EVENTS = 16;
  struct epoll_event events[MAX_EVENTS];

  while (running_.load(std::memory_order_acquire)) {
    int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);
    if (nfds < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw DaemonError("epoll_wait failed: " + std::string(strerror(errno)));
    }

    for (int i = 0; i < nfds; ++i) {
      dispatch_event_fd(events[i].data.fd, events[i].events);
    }
  }
}

void Daemon::fail_startup_runtime(std::string error) {
  auto &log = Logger::instance();
  std::string transition_error;
  const std::string reason = "startup failed: " + error;
  if (!runtime_state_machine_.transition(RuntimeState::broken, reason,
                                         transition_error)) {
    log.error("Unable to publish failed startup state: {}", transition_error);
  }
  publish_runtime_state();
  log.error("Routing runtime startup failed: {}", error);
}

void Daemon::begin_startup_runtime() {
  auto &log = Logger::instance();
  try {
    setup_static_routing();
    log.info("Static routing tables and ip rules installed.");

    log.info("Startup lists: checking local cache; only missing remote lists "
             "will be downloaded.");
    const auto relevant_lists = collect_relevant_list_names(config_);
    const auto dns_relevant_lists = collect_dns_relevant_list_names(config_);
    const bool queued = blocking_executor_.try_post(
        "startup-lists", [this, relevant_lists, dns_relevant_lists] {
          std::optional<RemoteListsRefreshResult> refresh_result;
          std::string error;
          try {
            refresh_result = list_service_.download_uncached(
                config_, outbound_marks_, &relevant_lists, &dns_relevant_lists);
          } catch (const std::exception &exception) {
            error = exception.what();
          } catch (...) {
            error = "unknown startup list error";
          }
          post_control_task(
              [this, refresh_result = std::move(refresh_result),
               error = std::move(error)]() mutable {
                continue_startup_after_lists(std::move(refresh_result),
                                             std::move(error));
              },
              "startup-after-lists");
        });
    if (!queued) {
      throw DaemonError("startup list executor is unavailable");
    }
  } catch (const std::exception &exception) {
    fail_startup_runtime(exception.what());
  } catch (...) {
    fail_startup_runtime("unknown routing initialization error");
  }
}

void Daemon::continue_startup_after_lists(
    std::optional<RemoteListsRefreshResult> refresh_result, std::string error) {
  auto &log = Logger::instance();
  if (!refresh_result.has_value()) {
    fail_startup_runtime(error.empty() ? "startup list refresh failed"
                                       : std::move(error));
    return;
  }

  const auto &result = *refresh_result;

  if (!result.cached_lists.empty()) {
    log.info("Startup lists: using cached list(s): {}",
             format_list_names(result.cached_lists));
  }
  if (!result.changed_lists.empty()) {
    log.info("Startup lists: downloaded missing list(s): {}",
             format_list_names(result.changed_lists));
  } else if (result.refreshed_lists.empty() && result.failed_lists.empty()) {
    log.info("Startup lists: all remote lists are available locally; no "
             "downloads needed.");
  }
  if (!result.unchanged_lists.empty()) {
    log.info("Startup lists: downloaded list(s) were unchanged: {}",
             format_list_names(result.unchanged_lists));
  }
  if (!result.failed_lists.empty()) {
    log.warn("Startup lists: failed to download missing list(s): {}",
             format_list_names(result.failed_lists));
  }

  try {
    apply_firewall(FirewallApplyMode::Destructive);
    log.info("Firewall rules and routing applied.");
    if (result.any_dns_relevant_changed()) {
      log.info("Startup lists: DNS-relevant list(s) changed: {}",
               format_list_names(result.dns_relevant_changed_lists));
    }

    // Publish the desired resolver generation before invoking the system
    // hook. dnsmasq can then stream the complete managed configuration
    // through the already-running control socket.
    apply_started_ts_.store(unix_timestamp_now_seconds(),
                            std::memory_order_release);
    update_resolver_config_hash();
    publish_runtime_state();

    const bool queued =
        resolver_hook_executor_.try_post("startup-resolver-hook", [this] {
          bool hook_succeeded = false;
          std::string hook_error;
          try {
            hook_succeeded = run_system_resolver_hook_reload();
            if (!hook_succeeded)
              hook_error = "system resolver reload hook failed";
          } catch (const std::exception &exception) {
            hook_error = exception.what();
          } catch (...) {
            hook_error = "unknown system resolver hook error";
          }
          post_control_task(
              [this, hook_succeeded,
               hook_error = std::move(hook_error)]() mutable {
                finish_startup_after_resolver_hook(hook_succeeded,
                                                   std::move(hook_error));
              },
              "startup-after-resolver-hook");
        });
    if (!queued) {
      throw DaemonError("startup resolver hook executor is unavailable");
    }
  } catch (const std::exception &exception) {
    fail_startup_runtime(exception.what());
  } catch (...) {
    fail_startup_runtime("unknown firewall or resolver initialization error");
  }
}

void Daemon::finish_startup_after_resolver_hook(bool hook_succeeded,
                                                std::string error) {
  if (!hook_succeeded) {
    fail_startup_runtime(error.empty() ? "system resolver reload hook failed"
                                       : std::move(error));
    return;
  }

  try {
    update_resolver_config_hash();
    setup_dns_probe();
    register_interface_monitor_fd();
    if (!has_system_resolver(config_)) {
      complete_running_runtime("startup complete");
      schedule_resolver_config_hash_actual_refresh();
      Logger::instance().info("Routing runtime started.");
      return;
    }

    const Config candidate = config_;
    const std::string expected =
        resolver_sync_.snapshot(unix_timestamp_now_seconds()).expected_hash;
    const std::int64_t started =
        apply_started_ts_.load(std::memory_order_acquire);
    const bool queued = resolver_io_executor_.try_post(
        "startup-resolver-verification", [this, candidate, expected, started] {
          std::string verification_error;
          const bool verified = wait_for_resolver_config_hash_confirmation(
              candidate, expected, started, verification_error);
          post_control_task(
              [this, verified,
               verification_error = std::move(verification_error)]() mutable {
                if (!verified) {
                  fail_startup_runtime(verification_error);
                  return;
                }
                try {
                  complete_running_runtime("startup complete");
                  schedule_resolver_config_hash_actual_refresh();
                  Logger::instance().info("Routing runtime started.");
                } catch (const std::exception &exception) {
                  fail_startup_runtime(exception.what());
                }
              },
              "startup-after-resolver-verification");
        });
    if (!queued)
      throw DaemonError(
          "startup resolver verification executor is unavailable");
  } catch (const std::exception &exception) {
    fail_startup_runtime(exception.what());
  } catch (...) {
    fail_startup_runtime("unknown startup finalization error");
  }
}

void Daemon::run() {
  auto &log = Logger::instance();

  // Make the control plane observable before any routing, list download, or
  // resolver work. Health reports runtime_state=starting until the deferred
  // initialization pipeline completes or reports a concrete failure.
  publish_runtime_state();

  running_.store(true, std::memory_order_release);
  event_loop_thread_id_.store(std::this_thread::get_id(),
                              std::memory_order_relaxed);
  event_loop_active_.store(true, std::memory_order_release);
  accept_posted_control_tasks_.store(true, std::memory_order_release);

#ifdef WITH_API
  setup_api();
#endif

  log.info("Daemon control plane running. PID: {}", getpid());
  post_control_task([this] { begin_startup_runtime(); }, "startup-runtime");

  run_event_loop();

  log.info("Shutting down...");
  transition_runtime_or_throw(RuntimeState::shutting_down, "daemon shutdown");
  publish_runtime_state();
  try {
    if (!run_system_resolver_hook("deactivate")) {
      log.warn("System resolver shutdown hook failed; dnsmasq will use "
               "fallback on its next restart");
    }
  } catch (const std::exception &error) {
    log.warn("System resolver shutdown hook failed: {}; dnsmasq will use "
             "fallback on its next restart",
             error.what());
  }

  // Some init scripts return before dnsmasq invokes its conf-script. Keep
  // the control socket alive long enough to answer that final fallback
  // request instead of closing the connection underneath the helper.
  drain_shutdown_resolver_callbacks(std::chrono::seconds{1});

  event_loop_active_.store(false, std::memory_order_release);
  event_loop_thread_id_.store(std::thread::id{}, std::memory_order_relaxed);
  accept_posted_control_tasks_.store(false, std::memory_order_release);
  lifecycle_executor_.shutdown();
  resolver_hook_executor_.shutdown();
  resolver_io_executor_.shutdown();
  routing_test_executor_.shutdown();
  blocking_executor_.shutdown();

#ifdef WITH_API
  if (status_stream_) {
    status_stream_->close_all();
  }
  if (dns_test_broadcaster_) {
    dns_test_broadcaster_->close_all();
  }
  if (api_server_) {
    api_server_->stop();
  }
#endif

  teardown_dns_probe();

  if (urltest_manager_) {
    urltest_manager_->clear();
  }
  scheduler_->cancel_all();
  const uint32_t mark_mask =
      fwmark_mask_value(config_.fwmark.value_or(FwmarkConfig{}));
  std::set<uint32_t> owned_marks;
  for (const auto &[tag, mark] : outbound_marks_) {
    (void)tag;
    owned_marks.insert(mark);
  }
  for (uint32_t mark : owned_marks) {
    if (!conntrack_manager_.delete_mark(mark, mark_mask)) {
      log.warn("Best-effort conntrack cleanup failed for mark {:#x}/{:#x}",
               mark, mark_mask);
    }
  }
  policy_rules_.clear();
  route_table_.clear();
  firewall_->cleanup();
  remove_pid_file();
}

void Daemon::stop() { running_.store(false, std::memory_order_release); }

bool Daemon::running() const {
  return running_.load(std::memory_order_acquire);
}

void Daemon::write_pid_file() {
  const auto pid_file =
      config_.daemon.value_or(DaemonConfig{}).pid_file.value_or("");
  try {
    pid_file_.acquire(pid_file);
  } catch (const std::exception &error) {
    throw DaemonError(error.what());
  }
}

void Daemon::remove_pid_file() { pid_file_.remove(); }

} // namespace keen_pbr3
