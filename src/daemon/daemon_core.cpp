#include "daemon.hpp"
#include "disk_config_state.hpp"

#include <arpa/inet.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <ctime>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <grp.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <ostream>
#include <set>
#include <streambuf>

#include "../cache/cache_manager.hpp"
#include "../firewall/firewall.hpp"
#include "../firewall/firewall_verifier.hpp"
#include "../cmd/test_routing.hpp"
#include "../dns/dnsmasq_gen.hpp"
#include "../dns/dns_router.hpp"
#include "../lists/list_streamer.hpp"
#include "../util/ipv6_support.hpp"
#include "../log/logger.hpp"
#include "../util/daemon_signals.hpp"
#include "../util/safe_exec.hpp"
#include "../util/time_utils.hpp"
#include "../dns/dns_probe_server.hpp" // IWYU pragma: keep
#include "scheduler.hpp"
#include "../ipc/control_protocol.hpp"

#ifdef WITH_API
#include "../api/handlers.hpp" // IWYU pragma: keep
#include "../api/server.hpp"
#include "../api/sse_broadcaster.hpp"
#endif

namespace keen_pbr3 {

namespace {

constexpr auto SIGUSR1_DEBOUNCE_DELAY = std::chrono::milliseconds{150};
constexpr auto INTERFACE_MONITOR_RECONNECT_RETRY_DELAY = std::chrono::seconds{5};
constexpr std::size_t kResolverStreamChunkBytes = static_cast<std::size_t>(16) * 1024U;

void send_all(int fd, const char* data, std::size_t size) {
    std::size_t written = 0;
    while (written < size) {
        const ssize_t count = send(fd, data + written, size - written, MSG_NOSIGNAL);
        if (count <= 0) {
            throw ipc::ControlProtocolError("resolver stream write failed: " +
                                            std::string(strerror(errno)));
        }
        written += static_cast<std::size_t>(count);
    }
}

class SocketStreamBuffer final : public std::streambuf {
public:
    explicit SocketStreamBuffer(int fd) : fd_(fd) {
        setp(buffer_.data(), buffer_.data() + buffer_.size());
    }

    ~SocketStreamBuffer() override { (void)sync(); }

protected:
    int overflow(int ch) override {
        if (flush_buffer() != 0) return traits_type::eof();
        if (ch != traits_type::eof()) {
            *pptr() = static_cast<char>(ch);
            pbump(1);
        }
        return ch;
    }

    std::streamsize xsputn(const char* data, std::streamsize size) override {
        std::streamsize written = 0;
        while (written < size) {
            if (pptr() == epptr() && flush_buffer() != 0) break;
            const auto capacity = static_cast<std::streamsize>(epptr() - pptr());
            const auto chunk = std::min(capacity, size - written);
            std::memcpy(pptr(), data + written, static_cast<std::size_t>(chunk));
            pbump(static_cast<int>(chunk));
            written += chunk;
        }
        return written;
    }

    int sync() override { return flush_buffer(); }

private:
    int flush_buffer() {
        const auto size = static_cast<std::size_t>(pptr() - pbase());
        if (size == 0) return 0;
        try {
            const std::uint32_t length = htonl(static_cast<std::uint32_t>(size));
            send_all(fd_, reinterpret_cast<const char*>(&length), sizeof(length));
            send_all(fd_, pbase(), size);
        } catch (...) {
            return -1;
        }
        setp(buffer_.data(), buffer_.data() + buffer_.size());
        return 0;
    }

    int fd_;
    std::array<char, kResolverStreamChunkBytes> buffer_{};
};

std::string resolver_runtime_reason(const RuntimeStateSnapshot& snapshot) {
    switch (snapshot.runtime_state) {
    case RuntimeState::starting: return "runtime_starting";
    case RuntimeState::stopped: return "runtime_stopped";
    case RuntimeState::broken: return "runtime_broken";
    case RuntimeState::shutting_down: return "runtime_shutting_down";
    default: return "daemon_error";
    }
}

std::int64_t steady_duration_ms(std::chrono::steady_clock::time_point started_at) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at).count();
}

} // namespace

std::string get_outbound_tag(const Outbound& ob) {
    return ob.tag;
}

const Outbound* find_outbound(const std::vector<Outbound>& outbounds,
                              const std::string& tag) {
    for (const auto& ob : outbounds) {
        if (ob.tag == tag) {
            return &ob;
        }
    }
    return nullptr;
}

Daemon::Daemon(Config config,
               std::string config_path,
               DaemonOptions opts,
               HookCommandExecutor hook_command_executor)
    : config_store_(config)
    , list_service_(config.daemon.value_or(DaemonConfig{}).cache_dir.value_or("/var/cache/keen-pbr"),
                    max_file_size_bytes(config))
    , config_(std::move(config))
    , config_path_(std::move(config_path))
    , opts_(std::move(opts))
    , firewall_(create_firewall(firewall_backend_preference(config_)))
    , interface_monitor_(std::make_unique<InterfaceMonitor>(
          [this](const std::string& interface_name, bool is_up) {
              handle_interface_state_change(interface_name, is_up);
          }))
    , netlink_()
    , route_table_(netlink_)
    , policy_rules_(netlink_)
    , firewall_state_()
    , url_tester_()
    , outbound_marks_(allocate_outbound_marks(config_.fwmark.value_or(FwmarkConfig{}),
                                              config_.outbounds.value_or(std::vector<Outbound>{})))
    , hook_command_executor_(std::move(hook_command_executor))
{
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

    setup_signals();
    setup_control_channel();
    setup_ipc_control_socket();

    const int64_t verify_max_bytes = config_.daemon.value_or(DaemonConfig{})
        .firewall_verify_max_bytes.value_or(static_cast<int64_t>(DEFAULT_FIREWALL_VERIFY_CAPTURE_MAX_BYTES));
    set_firewall_verifier_capture_max_bytes(static_cast<size_t>(verify_max_bytes));

    firewall_state_.set_outbound_marks(outbound_marks_);
    firewall_state_.set_fwmark_mask(fwmark_mask_value(config_.fwmark.value_or(FwmarkConfig{})));
    list_service_.ensure_dir();
    scheduler_ = std::make_unique<Scheduler>(*this);

#ifdef WITH_API
    dns_test_broadcaster_ = std::make_unique<SseBroadcaster>();
#endif
}

Daemon::~Daemon() {
    try {
        accept_posted_control_tasks_.store(false, std::memory_order_release);
        blocking_executor_.shutdown();

        if (control_fd_ >= 0) {
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, control_fd_, nullptr);
            close(control_fd_);
            control_fd_ = -1;
        }
        remove_ipc_control_socket();
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
    } catch (const std::exception& e) {
        Logger::instance().error("Daemon destruction cleanup failed: {}", e.what());
    } catch (...) {
        Logger::instance().error("Daemon destruction cleanup failed: unknown error");
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
        throw DaemonError("epoll_ctl add control_fd failed: " + std::string(strerror(errno)));
    }
}

void Daemon::setup_ipc_control_socket() {
    ipc_control_socket_path_ = KEEN_PBR_CONTROL_SOCKET;
    if (ipc_control_socket_path_.size() >= sizeof(sockaddr_un::sun_path)) {
        throw DaemonError("control socket path is too long");
    }

    std::filesystem::create_directories(std::filesystem::path(ipc_control_socket_path_).parent_path());
    struct stat existing {};
    if (lstat(ipc_control_socket_path_.c_str(), &existing) == 0) {
        if (!S_ISSOCK(existing.st_mode) || unlink(ipc_control_socket_path_.c_str()) != 0) {
            throw DaemonError("unsafe stale control socket path");
        }
    } else if (errno != ENOENT) {
        throw DaemonError("failed to inspect control socket path: " + std::string(strerror(errno)));
    }

    const group* control_group = getgrnam("keen-pbr");
    if (control_group == nullptr) {
        throw DaemonError("required control socket group keen-pbr does not exist");
    }
    ipc_control_group_id_ = control_group->gr_gid;

    ipc_control_fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (ipc_control_fd_ < 0) {
        throw DaemonError("control socket create failed: " + std::string(strerror(errno)));
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, ipc_control_socket_path_.c_str(), sizeof(address.sun_path) - 1U);
    if (bind(ipc_control_fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(ipc_control_fd_, 16) != 0 ||
        chown(ipc_control_socket_path_.c_str(), 0, ipc_control_group_id_) != 0 ||
        chmod(ipc_control_socket_path_.c_str(), 0660) != 0) {
        const std::string error = strerror(errno);
        remove_ipc_control_socket();
        throw DaemonError("control socket setup failed: " + error);
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = ipc_control_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, ipc_control_fd_, &event) != 0) {
        const std::string error = strerror(errno);
        remove_ipc_control_socket();
        throw DaemonError("failed to register control socket: " + error);
    }
}

void Daemon::remove_ipc_control_socket() noexcept {
    if (ipc_control_fd_ >= 0) {
        if (epoll_fd_ >= 0) (void)epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, ipc_control_fd_, nullptr);
        close(ipc_control_fd_);
        ipc_control_fd_ = -1;
    }
    ipc_control_group_id_ = static_cast<gid_t>(-1);
    if (!ipc_control_socket_path_.empty()) {
        (void)unlink(ipc_control_socket_path_.c_str());
        ipc_control_socket_path_.clear();
    }
}

void Daemon::handle_ipc_control_socket() {
    while (true) {
        const int client = accept4(ipc_control_fd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            throw DaemonError("control socket accept failed: " + std::string(strerror(errno)));
        }
        timeval timeout{5, 0};
        (void)setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        (void)setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        nlohmann::json request = nlohmann::json::object();
        nlohmann::json response;
        bool resolver_stream_dispatched = false;
        try {
            ucred peer{};
            socklen_t peer_length = sizeof(peer);
            if (getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &peer_length) != 0) {
                throw ipc::ControlProtocolError("unable to verify control peer");
            }
            std::uint32_t length = 0;
            if (recv(client, &length, sizeof(length), MSG_WAITALL) != sizeof(length)) {
                throw ipc::ControlProtocolError("truncated control request");
            }
            const std::size_t payload_size = ntohl(length);
            if (payload_size > ipc::kMaxControlMessageBytes) {
                throw ipc::ControlProtocolError("control message exceeds maximum size");
            }
            std::string frame(sizeof(length) + payload_size, '\0');
            std::memcpy(frame.data(), &length, sizeof(length));
            if (recv(client, frame.data() + sizeof(length), payload_size, MSG_WAITALL) !=
                static_cast<ssize_t>(payload_size)) {
                throw ipc::ControlProtocolError("truncated control request body");
            }
            request = ipc::decode_message(frame);
            ipc::validate_request_envelope(request);
            const std::string operation = request.at("operation").get<std::string>();
            if (ipc_resolver_hook_inflight_.load(std::memory_order_acquire) &&
                operation != "generate-resolver-config") {
                response = ipc::make_error_response(
                    request, "busy", "resolver hook is the only control operation currently allowed");
            } else {
            const bool root_peer = peer.uid == 0;
            const bool list_update_member = peer.gid == ipc_control_group_id_ && operation == "download";
            if (!root_peer && !list_update_member) {
                throw ipc::ControlProtocolError("control peer is not authorized for this operation");
            }
            if (operation != "status" && operation != "resolver-config-hash" &&
                operation != "download" && operation != "test-routing" &&
                operation != "generate-resolver-config") {
                response = ipc::make_error_response(request, "unsupported_operation",
                                                    "unsupported control operation");
            } else if (operation == "test-routing") {
                const std::string target = request.value("target", "");
                if (target.empty()) throw ipc::ControlProtocolError("test-routing requires a target");
                const auto result = compute_test_routing(config_store_.active_config(),
                                                         list_service_.cache_manager(), target);
                nlohmann::json entries = nlohmann::json::array();
                for (const auto& entry : result.entries) {
                    entries.push_back({{"ip", entry.ip},
                                       {"expected_outbound", entry.expected_outbound},
                                       {"actual_outbound", entry.actual_outbound},
                                       {"ok", entry.ok}});
                }
                response = {{"protocol_version", ipc::kControlProtocolVersion},
                            {"request_id", request.at("request_id")},
                            {"ok", !result.dns_error.has_value()},
                            {"result", {{"target", result.target},
                                        {"resolved_ips", result.resolved_ips},
                                        {"entries", std::move(entries)},
                                        {"warnings", result.warnings},
                                        {"dns_error", result.dns_error}}}};
            } else if (operation == "generate-resolver-config") {
                const auto runtime_snapshot = runtime_state_store_.snapshot();
                const RuntimeState runtime_state = runtime_state_machine_.state();
                if (runtime_state == RuntimeState::broken ||
                    runtime_state == RuntimeState::shutting_down ||
                    !runtime_snapshot.routing_runtime_active || !routing_runtime_active_) {
                    const std::string reason = runtime_state == RuntimeState::broken
                        ? "runtime_broken"
                        : (runtime_state == RuntimeState::shutting_down
                            ? "runtime_shutting_down" : resolver_runtime_reason(runtime_snapshot));
                    response = ipc::make_error_response(request, reason,
                                                        "resolver runtime is not active");
                    const std::string frame = ipc::encode_message(response);
                    (void)send(client, frame.data(), frame.size(), MSG_NOSIGNAL);
                    close(client);
                    continue;
                }
                // During a transactional apply dnsmasq's hook must receive the
                // candidate, while all ordinary readers still use ConfigStore's
                // committed snapshot. The candidate is never persisted here.
                const auto active_config = runtime_state_machine_.state() == RuntimeState::applying
                    ? config_ : config_store_.active_config();
                const auto dns_config = active_config.dns.value_or(DnsConfig{});
                const auto cache_dir = active_config.daemon.value_or(DaemonConfig{})
                    .cache_dir.value_or("/var/cache/keen-pbr");
                const auto type = request.value("resolver", "dnsmasq") == "dnsmasq-ipset"
                    ? ResolverType::DNSMASQ_IPSET
                    : (request.value("resolver", "dnsmasq") == "dnsmasq-nftset"
                        ? ResolverType::DNSMASQ_NFTSET
                        : (firewall_->backend() == FirewallBackend::nftables
                            ? ResolverType::DNSMASQ_NFTSET : ResolverType::DNSMASQ_IPSET));
                const auto request_id = request.at("request_id").get<std::string>();
                const bool queued = blocking_executor_.try_post(
                    "generate-resolver-config",
                    [client, active_config, dns_config, cache_dir, type, request_id] {
                        bool stream_started = false;
                        try {
                            CacheManager cache(cache_dir, max_file_size_bytes(active_config));
                            std::set<std::string> referenced_lists;
                            for (const auto& rule : active_config.route.value_or(RouteConfig{}).rules.value_or(
                                     std::vector<RouteRule>{})) {
                                if (!route_rule_enabled(rule)) continue;
                                for (const auto& list_name : route_rule_lists(rule)) {
                                    referenced_lists.insert(list_name);
                                }
                            }
                            for (const auto& rule : dns_config.rules.value_or(std::vector<DnsRule>{})) {
                                if (!dns_rule_enabled(rule)) continue;
                                for (const auto& list_name : rule.list) {
                                    referenced_lists.insert(list_name);
                                }
                            }
                            const auto lists = active_config.lists.value_or(
                                std::map<std::string, ListConfig>{});
                            for (const auto& list_name : referenced_lists) {
                                const auto list = lists.find(list_name);
                                if (list == lists.end()) continue;
                                if (list->second.url.has_value() && !cache.has_cache(list_name)) {
                                    throw ipc::ControlProtocolError("list_cache_missing");
                                }
                                if (list->second.file.has_value() &&
                                    !std::filesystem::is_regular_file(list->second.file.value())) {
                                    throw ipc::ControlProtocolError("active_list_cache_mismatch");
                                }
                            }
                            const auto header = ipc::encode_message(
                                {{"protocol_version", ipc::kControlProtocolVersion},
                                 {"request_id", request_id}, {"ok", true}, {"stream", true}});
                            send_all(client, header.data(), header.size());
                            stream_started = true;
                            SocketStreamBuffer buffer(client);
                            std::ostream output(&buffer);
                            output << "# keen-pbr resolver state: active\n";
                            const auto ipv6 = resolve_ipv6_support(active_config);
                            ListStreamer streamer(cache);
                            DnsServerRegistry registry(dns_config);
                            DnsmasqGenerator generator(
                                registry, streamer, active_config.route.value_or(RouteConfig{}), dns_config,
                                active_config.lists.value_or(std::map<std::string, ListConfig>{}), type,
                                KEEN_PBR3_VERSION_FULL_STRING, ipv6.enabled);
                            generator.generate(output);
                            output << "txt-record=resolver-state.keen.pbr," << std::time(nullptr)
                                   << "|active|runtime_active\n";
                            output.flush();
                            const std::uint32_t end_of_stream = 0;
                            send_all(client, reinterpret_cast<const char*>(&end_of_stream),
                                     sizeof(end_of_stream));
                        } catch (const std::exception& error) {
                            if (!stream_started) {
                                const auto response = ipc::make_error_response(
                                    {{"request_id", request_id}}, error.what(), error.what());
                                const auto frame = ipc::encode_message(response);
                                (void)send(client, frame.data(), frame.size(), MSG_NOSIGNAL);
                            } else {
                                Logger::instance().warn("resolver config stream failed: {}", error.what());
                            }
                        }
                        close(client);
                    });
                if (queued) {
                    resolver_stream_dispatched = true;
                    continue;
                }
                response = ipc::make_error_response(request, "daemon_error",
                                                    "resolver stream executor is unavailable");
            } else if (operation == "download") {
                bool expected = false;
                if (!ipc_mutation_inflight_.compare_exchange_strong(expected,
                                                                     true,
                                                                     std::memory_order_acq_rel)) {
                    response = ipc::make_error_response(request, "busy",
                                                        "another control mutation is in progress");
                    const std::string frame = ipc::encode_message(response);
                    (void)send(client, frame.data(), frame.size(), MSG_NOSIGNAL);
                    close(client);
                    continue;
                }
                struct MutationGate {
                    std::atomic<bool>& flag;
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
                    refresh = list_service_.refresh_remote_lists(config_, outbound_marks_,
                                                                  &relevant, nullptr, &dns_relevant);
                }
                response = {{"protocol_version", ipc::kControlProtocolVersion},
                            {"request_id", request.at("request_id")},
                            {"ok", refresh.failed_lists.empty()},
                            {"result", {{"refreshed_lists", refresh.refreshed_lists},
                                        {"changed_lists", refresh.changed_lists},
                                        {"failed_lists", refresh.failed_lists},
                                        {"reloaded", reloaded}}}};
            } else {
                const auto snapshot = runtime_state_store_.snapshot();
                const Config active_config = config_store_.active_config();
                const auto disk_config = inspect_disk_config_state(config_path_, active_config);
                nlohmann::json missing_cached_lists = nlohmann::json::array();
                const auto relevant_lists = collect_relevant_list_names(active_config);
                const auto& lists = active_config.lists.value_or(std::map<std::string, ListConfig>{});
                const auto& cache = list_service_.cache_manager();
                for (const auto& list_name : relevant_lists) {
                    const auto list = lists.find(list_name);
                    if (list != lists.end() && list->second.url.has_value() && !cache.has_cache(list_name)) {
                        missing_cached_lists.push_back(list_name);
                    }
                }
                response = {{"protocol_version", ipc::kControlProtocolVersion},
                            {"request_id", request.at("request_id")},
                            {"ok", true},
                            {"result", {{"runtime_state", runtime_state_name(snapshot.runtime_state)},
                                        {"runtime_state_reason", snapshot.runtime_state_reason},
                                        {"routing_runtime_active", snapshot.routing_runtime_active},
                                        {"resolver_config_hash", snapshot.resolver_config_hash},
                                        {"resolver_config_hash_actual", snapshot.resolver_config_hash_actual},
                                        {"resolver_config_hash_actual_ts", snapshot.resolver_config_hash_actual_ts},
                                        {"resolver_config_sync_state", snapshot.resolver_config_sync_state},
                                        {"resolver_config_probe_status", snapshot.resolver_config_probe_status},
                                        {"resolver_live_status", snapshot.resolver_live_status},
                                        {"resolver_last_probe_ts", snapshot.resolver_last_probe_ts},
                                        {"disk_config_mismatch", !disk_config.matches_active},
                                        {"disk_config_error", disk_config.error},
                                        {"missing_cached_lists", missing_cached_lists}}}};
            }
            }
        } catch (const std::exception& error) {
            response = ipc::make_error_response(request, "protocol_error", error.what());
        }
        if (!resolver_stream_dispatched) {
            const std::string frame = ipc::encode_message(response);
            (void)send(client, frame.data(), frame.size(), MSG_NOSIGNAL);
            close(client);
        }
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
    return event_loop_thread_id_.load(std::memory_order_relaxed) == std::this_thread::get_id();
}

void Daemon::enqueue_control_task(std::function<void()> task,
                                  bool wait_for_completion,
                                  const std::string& label) {
    if (!task) {
        return;
    }

    const auto effective_label = label.empty() ? std::string("control-task") : label;
    const TraceId trace_id = ensure_trace_id();
    auto run_inline = [task = std::move(task), effective_label, trace_id]() mutable {
        ScopedTraceContext trace_scope(trace_id);
        const auto started_at = std::chrono::steady_clock::now();
        Logger::instance().trace("control_task_start", "label={} mode=inline", effective_label);
        try {
            task();
            Logger::instance().trace("control_task_end",
                                     "label={} mode=inline duration_ms={}",
                                     effective_label,
                                     steady_duration_ms(started_at));
        } catch (const std::exception& e) {
            Logger::instance().trace("control_task_error",
                                     "label={} mode=inline duration_ms={} error={}",
                                     effective_label,
                                     steady_duration_ms(started_at),
                                     e.what());
            throw;
        } catch (...) {
            Logger::instance().trace("control_task_error",
                                     "label={} mode=inline duration_ms={} error=unknown",
                                     effective_label,
                                     steady_duration_ms(started_at));
            throw;
        }
    };

    if (!event_loop_active_.load(std::memory_order_acquire) ||
        event_loop_thread_id_.load(std::memory_order_relaxed) == std::thread::id{}) {
        run_inline();
        return;
    }

    if (event_loop_thread_id_.load(std::memory_order_relaxed) == std::this_thread::get_id()) {
        run_inline();
        return;
    }

    if (wait_for_completion) {
        auto done = std::make_shared<std::promise<void>>();
        auto fut = done->get_future();
        {
            KPBR_LOCK_GUARD(control_tasks_mutex_);
            control_tasks_.push_back(ControlTask{
                .callback = [cmd = std::move(run_inline), done]() mutable {
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
        Logger::instance().trace("control_task_enqueue",
                                 "label={} wait=true",
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
    Logger::instance().trace("control_task_enqueue",
                             "label={} wait=false",
                             effective_label);
    wake_control_loop();
}

void Daemon::post_control_task(std::function<void()> task, const std::string& label) {
    if (!task) return;
    if (!accept_posted_control_tasks_.load(std::memory_order_acquire)) {
        Logger::instance().trace("control_task_skip",
                                 "label={} reason=posted_tasks_disabled",
                                 label.empty() ? "post-control-task" : label);
        return;
    }

    const auto effective_label = label.empty() ? std::string("post-control-task") : label;
    const TraceId trace_id = ensure_trace_id();
    auto traced_task = [task = std::move(task), effective_label, trace_id]() mutable {
        ScopedTraceContext trace_scope(trace_id);
        const auto started_at = std::chrono::steady_clock::now();
        Logger::instance().trace("control_task_start", "label={} mode=posted", effective_label);
        try {
            task();
            Logger::instance().trace("control_task_end",
                                     "label={} mode=posted duration_ms={}",
                                     effective_label,
                                     steady_duration_ms(started_at));
        } catch (const std::exception& e) {
            Logger::instance().trace("control_task_error",
                                     "label={} mode=posted duration_ms={} error={}",
                                     effective_label,
                                     steady_duration_ms(started_at),
                                     e.what());
            throw;
        } catch (...) {
            Logger::instance().trace("control_task_error",
                                     "label={} mode=posted duration_ms={} error=unknown",
                                     effective_label,
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
                             "label={} wait=false mode=post",
                             effective_label);
    wake_control_loop();
}

void Daemon::enqueue_control_command(std::function<void()> command,
                                     bool wait_for_completion,
                                     const std::string& label) {
    enqueue_control_task(std::move(command), wait_for_completion, label);
}

void Daemon::handle_control_commands() {
    uint64_t counter = 0;
    while (read(control_fd_, &counter, sizeof(counter)) > 0) {
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        throw DaemonError("eventfd read failed: " + std::string(strerror(errno)));
    }

    std::vector<ControlTask> commands;
    {
        KPBR_LOCK_GUARD(control_tasks_mutex_);
        commands.swap(control_tasks_);
    }

    for (auto& command : commands) {
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
        throw DaemonError("epoll_ctl add signalfd failed: " + std::string(strerror(errno)));
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
    auto& log = Logger::instance();
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
            if (operation_coordinator_.busy()) {
                Logger::instance().info("SIGUSR1: config operation is active; deferring runtime reconcile");
                schedule_sigusr1_runtime_refresh();
                return;
            }
            Logger::instance().info("SIGUSR1: applying firewall refresh...");
            refresh_iproute_and_firewall_runtime();
            Logger::instance().info("SIGUSR1: firewall refresh complete.");
        },
        "sigusr1-runtime-refresh");
}

void Daemon::handle_sighup() {
    auto& log = Logger::instance();
    log.info("SIGHUP: full reload starting...");
    if (!operation_coordinator_.try_begin("sighup-reload")) {
        log.warn("SIGHUP: reload skipped because another config operation is in progress");
        return;
    }
    const bool enqueued = blocking_executor_.try_post(
        "sighup-config-transaction",
        [this] {
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
                result = apply_validated_config_via_control_task(
                    std::move(candidate), "", false);
            } catch (const std::exception& error) {
                result.error = error.what();
            }
            post_control_task(
                [this, result = std::move(result)] {
                    operation_coordinator_.finish();
                    if (result.error.empty()) {
                        Logger::instance().info("SIGHUP: full reload complete.");
                    } else {
                        Logger::instance().error("SIGHUP: reload failed: {}", result.error);
                    }
                },
                "sighup-config-transaction-complete");
        });
    if (!enqueued) {
        operation_coordinator_.finish();
        log.error("SIGHUP: reload could not be scheduled");
    }
}

void Daemon::refresh_iproute_and_firewall_runtime() {
    auto& log = Logger::instance();
    try {
        reconcile_static_routing();
        apply_firewall(FirewallApplyMode::PreserveSets);
        publish_runtime_state();
        log.info("Runtime iproute and firewall refresh complete.");
    } catch (const std::exception& e) {
        log.error("Runtime iproute and firewall refresh failed: {}", e.what());
    }
}

bool Daemon::is_interface_outbound_in_use(const std::string& interface_name) const {
    const auto outbounds = config_.outbounds.value_or(std::vector<Outbound>{});
    return std::any_of(outbounds.begin(), outbounds.end(), [&interface_name](const Outbound& outbound) {
        return outbound.type == OutboundType::INTERFACE &&
               outbound.interface.has_value() &&
               outbound.interface.value() == interface_name;
    });
}

void Daemon::handle_interface_state_change(const std::string& interface_name, bool is_up) {
    auto& log = Logger::instance();
    if (!is_interface_outbound_in_use(interface_name)) {
        return;
    }

    log.info("Interface {} state changed to {}, iproute and firewall refresh triggered",
             interface_name,
             is_up ? "UP" : "DOWN");
    refresh_iproute_and_firewall_runtime();
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
        Logger::instance().error("Interface monitor fd reported epoll error/hangup");
        reconnect_interface_monitor();
        return;
    }

    try {
        interface_monitor_->handle_events();
    } catch (const std::exception& e) {
        Logger::instance().error("Interface monitor event handling failed: {}", e.what());
        reconnect_interface_monitor();
    }
}

void Daemon::register_interface_monitor_fd() {
    if (!interface_monitor_) {
        return;
    }

    const int fd = interface_monitor_->fd();
    add_fd(fd,
           EPOLLIN,
           [this](uint32_t events) { handle_interface_monitor_events(events); },
           true,
           "interface-monitor");
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
    enqueue_control_task([this]() {
        if (!interface_monitor_) {
            return;
        }

        unregister_interface_monitor_fd();

        try {
            interface_monitor_->reconnect();
            register_interface_monitor_fd();
            Logger::instance().warn("Interface monitor reconnected after netlink error");
        } catch (const std::exception& e) {
            Logger::instance().error("Interface monitor reconnect failed: {}", e.what());
            schedule_interface_monitor_reconnect_retry();
        }
    }, false, "interface-monitor-reconnect");
}

void Daemon::add_fd(int fd,
                    uint32_t events,
                    FdCallback cb,
                    bool wait_for_completion,
                    const std::string& label) {
    enqueue_control_task([this, fd, events, cb = std::move(cb)]() mutable {
        struct epoll_event ev{};
        ev.events = events;
        ev.data.fd = fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
            throw DaemonError("epoll_ctl add fd failed: " + std::string(strerror(errno)));
        }

        KPBR_LOCK_GUARD(fd_entries_mutex_);
        fd_entries_.push_back({fd, std::move(cb)});
    }, wait_for_completion, label.empty() ? "add-fd" : label);
}

void Daemon::remove_fd(int fd,
                       bool wait_for_completion,
                       const std::string& label) {
    enqueue_control_task([this, fd]() {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);

        KPBR_LOCK_GUARD(fd_entries_mutex_);
        fd_entries_.erase(
            std::remove_if(fd_entries_.begin(), fd_entries_.end(),
                           [fd](const FdEntry& e) { return e.fd == fd; }),
            fd_entries_.end());
    }, wait_for_completion, label.empty() ? "remove-fd" : label);
}

void Daemon::dispatch_event_fd(int fd, uint32_t events) {
    if (fd == ipc_control_fd_) {
        if ((events & EPOLLIN) != 0U) handle_ipc_control_socket();
        return;
    }
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
        for (auto& entry : fd_entries_) {
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

void Daemon::run() {
    auto& log = Logger::instance();

    write_pid_file();

    setup_static_routing();
    log.info("Static routing tables and ip rules installed.");

    log.info("Startup lists: checking local cache; only missing remote lists will be downloaded.");
    const auto relevant_lists = collect_relevant_list_names(config_);
    const auto dns_relevant_lists = collect_dns_relevant_list_names(config_);
    const RemoteListsRefreshResult refresh_result = list_service_.download_uncached(
        config_, outbound_marks_, &relevant_lists, &dns_relevant_lists);

    if (!refresh_result.cached_lists.empty()) {
        log.info("Startup lists: using cached list(s): {}", format_list_names(refresh_result.cached_lists));
    }
    if (!refresh_result.changed_lists.empty()) {
        log.info("Startup lists: downloaded missing list(s): {}", format_list_names(refresh_result.changed_lists));
    } else if (refresh_result.refreshed_lists.empty() && refresh_result.failed_lists.empty()) {
        log.info("Startup lists: all remote lists are available locally; no downloads needed.");
    }
    if (!refresh_result.unchanged_lists.empty()) {
        log.info("Startup lists: downloaded list(s) were unchanged: {}",
                 format_list_names(refresh_result.unchanged_lists));
    }
    if (!refresh_result.failed_lists.empty()) {
        log.warn("Startup lists: failed to download missing list(s): {}",
                 format_list_names(refresh_result.failed_lists));
    }

    register_urltest_outbounds();
    apply_firewall(FirewallApplyMode::Destructive);
    log.info("Firewall rules and routing applied.");

    schedule_lists_autoupdate();

    if (refresh_result.any_dns_relevant_changed()) {
        log.info("Startup lists: DNS-relevant list(s) changed: {}",
                 format_list_names(refresh_result.dns_relevant_changed_lists));
    }
    // A matching stale TXT is not proof that the newly started daemon has
    // installed its resolver configuration, so startup always restarts it.
    apply_started_ts_.store(unix_timestamp_now_seconds(), std::memory_order_release);
    if (!run_system_resolver_hook_reload()) {
        throw DaemonError("system resolver reload hook failed during startup");
    }
    update_resolver_config_hash();
    const std::string expected_resolver_hash =
        resolver_sync_.snapshot(unix_timestamp_now_seconds()).expected_hash;
    std::string resolver_confirmation_error;
    const bool resolver_confirmed = wait_for_resolver_config_hash_confirmation(
        config_, expected_resolver_hash,
        apply_started_ts_.load(std::memory_order_acquire), resolver_confirmation_error);
    refresh_resolver_config_hash_actual_async();
    schedule_resolver_config_hash_actual_refresh();
    if (resolver_confirmed) {
        transition_runtime_or_throw(RuntimeState::running, "startup complete");
    } else {
        std::string ignored_error;
        (void)runtime_state_machine_.transition(
            RuntimeState::broken, "startup resolver confirmation failed", ignored_error);
        log.error("Startup resolver confirmation failed: {}", resolver_confirmation_error);
    }
    publish_runtime_state();

    setup_dns_probe();

    register_interface_monitor_fd();

#ifdef WITH_API
    setup_api();
#endif

    log.info("Daemon running. PID: {}", getpid());

    running_.store(true, std::memory_order_release);
    event_loop_thread_id_.store(std::this_thread::get_id(), std::memory_order_relaxed);
    event_loop_active_.store(true, std::memory_order_release);
    accept_posted_control_tasks_.store(true, std::memory_order_release);

    run_event_loop();

    event_loop_active_.store(false, std::memory_order_release);
    event_loop_thread_id_.store(std::thread::id{}, std::memory_order_relaxed);
    accept_posted_control_tasks_.store(false, std::memory_order_release);
    blocking_executor_.shutdown();

    log.info("Shutting down...");
    transition_runtime_or_throw(RuntimeState::shutting_down, "daemon shutdown");

#ifdef WITH_API
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
    const uint32_t mark_mask = fwmark_mask_value(config_.fwmark.value_or(FwmarkConfig{}));
    std::set<uint32_t> owned_marks;
    for (const auto& [tag, mark] : outbound_marks_) {
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

void Daemon::stop() {
    running_.store(false, std::memory_order_release);
}

bool Daemon::running() const {
    return running_.load(std::memory_order_acquire);
}

void Daemon::write_pid_file() {
    const auto pid_file = config_.daemon.value_or(DaemonConfig{}).pid_file.value_or("");
    try {
        pid_file_.acquire(pid_file);
    } catch (const std::exception& error) {
        throw DaemonError(error.what());
    }
}

void Daemon::remove_pid_file() {
    pid_file_.remove();
}

} // namespace keen_pbr3
