#include "daemon.hpp"
#include <algorithm>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <signal.h>
#include <sstream>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sys/file.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "../dns/dnsmasq_gen.hpp"
#include "../dns/dns_probe_server.hpp"
#include "../dns/dns_router.hpp"
#include "../dns/dns_server.hpp"
#include "../dns/dns_txt_client.hpp"
#include "../firewall/firewall.hpp"
#include "../firewall/firewall_verifier.hpp"
#include "../lists/list_entry_visitor.hpp"
#include "../lists/list_set_usage.hpp"
#include "../lists/list_streamer.hpp"
#include "../log/logger.hpp"
#include "../routing/urltest_manager.hpp"
#include "../config/routing_state.hpp"
#include "../config/addr_spec.hpp"
#include "../health/runtime_interface_inventory.hpp"
#include "../health/runtime_outbound_state.hpp"
#include "../util/daemon_signals.hpp"
#include "../util/cron.hpp"
#include "scheduler.hpp"
#include "system_resolver_hook.hpp"

#ifndef KEEN_PBR_FRONTEND_ROOT
#define KEEN_PBR_FRONTEND_ROOT "/usr/share/keen-pbr/frontend"
#endif

#ifdef WITH_API
#include "../api/handlers.hpp"
#include "../api/server.hpp"
#include "../api/sse_broadcaster.hpp"
#endif

namespace keen_pbr3 {

namespace {

std::string format_list_names(const std::vector<std::string>& list_names) {
    if (list_names.empty()) {
        return "(none)";
    }

    std::ostringstream out;
    for (size_t i = 0; i < list_names.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << list_names[i];
    }
    return out.str();
}

std::int64_t steady_duration_ms(std::chrono::steady_clock::time_point started_at) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at).count();
}

std::int64_t unix_timestamp_now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

#ifdef WITH_API
const char* config_operation_state_name(ConfigOperationState state) {
    switch (state) {
    case ConfigOperationState::Idle:
        return "idle";
    case ConfigOperationState::Saving:
        return "saving";
    case ConfigOperationState::Reloading:
        return "reloading";
    }
    return "unknown";
}

std::optional<api::ResolverConfigSyncState> classify_resolver_config_sync_state(
    const std::optional<std::int64_t>& actual_ts,
    const std::optional<std::int64_t>& apply_started_ts,
    std::int64_t now_ts,
    bool hash_equal) {
    if (!actual_ts.has_value() || !apply_started_ts.has_value()) {
        return std::nullopt;
    }
    constexpr std::int64_t kConvergingWindowSeconds = 15;
    if (*actual_ts < *apply_started_ts) {
        if ((now_ts - *apply_started_ts) <= kConvergingWindowSeconds) {
            return api::ResolverConfigSyncState::CONVERGING;
        }
        return hash_equal
            ? std::optional<api::ResolverConfigSyncState>(api::ResolverConfigSyncState::CONVERGED)
            : std::optional<api::ResolverConfigSyncState>(api::ResolverConfigSyncState::STALE);
    }
    return hash_equal
        ? std::optional<api::ResolverConfigSyncState>(api::ResolverConfigSyncState::CONVERGED)
        : std::optional<api::ResolverConfigSyncState>(api::ResolverConfigSyncState::STALE);
}
#endif

} // namespace

// Helper to get tag from an outbound
std::string get_outbound_tag(const Outbound& ob) {
    return ob.tag;
}

// Find an outbound by tag, returning pointer or nullptr
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

    // Initialize epoll
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        throw DaemonError("epoll_create1 failed: " + std::string(strerror(errno)));
    }

    setup_signals();
    setup_control_channel();

    const int64_t verify_max_bytes = config_.daemon.value_or(DaemonConfig{})
        .firewall_verify_max_bytes.value_or(static_cast<int64_t>(DEFAULT_FIREWALL_VERIFY_CAPTURE_MAX_BYTES));
    set_firewall_verifier_capture_max_bytes(static_cast<size_t>(verify_max_bytes));

    // Set outbound marks in firewall state
    firewall_state_.set_outbound_marks(outbound_marks_);

    // Ensure cache directory exists
    list_service_.ensure_dir();

    // Create scheduler (needs Daemon& for epoll fd registration)
    scheduler_ = std::make_unique<Scheduler>(*this);

    // UrltestManager created later during startup (register_urltest_outbounds)

#ifdef WITH_API
    dns_test_broadcaster_ = std::make_unique<SseBroadcaster>();
#endif
}

Daemon::~Daemon() {
    accept_posted_control_tasks_.store(false, std::memory_order_release);
    blocking_executor_.shutdown();

    if (control_fd_ >= 0) {
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, control_fd_, nullptr);
        close(control_fd_);
        control_fd_ = -1;
    }
    if (signal_fd_ >= 0) {
        // Remove from epoll before closing
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, signal_fd_, nullptr);
        close(signal_fd_);
        signal_fd_ = -1;
    }
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }

    // Restore the thread signal mask when the daemon is torn down.
    unblock_daemon_signals_for_current_thread();
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

    // Always queue, even when called from the event loop thread.
    // This guarantees the task runs after the current iteration finishes and
    // any locks held by the caller have been released.
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
    // Block SIGTERM, SIGINT, SIGUSR1, SIGHUP so they can be handled via signalfd
    block_daemon_signals_for_current_thread();
    sigset_t mask = daemon_signal_mask();

    // Create signalfd for the blocked signals
    signal_fd_ = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signal_fd_ < 0) {
        throw DaemonError("signalfd failed: " + std::string(strerror(errno)));
    }

    // Add signalfd to epoll
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
    log.info("SIGUSR1: verifying routing tables and triggering URL tests...");

    refresh_iproute_and_firewall_runtime();

    // Trigger immediate URL tests for all urltest outbounds
    if (urltest_manager_) {
        for (const auto& ob : config_.outbounds.value_or(std::vector<Outbound>{})) {
            if (ob.type == OutboundType::URLTEST) {
                urltest_manager_->trigger_immediate_test(ob.tag);
            }
        }
    }

    log.info("SIGUSR1: complete.");
}

void Daemon::handle_sighup() {
    auto& log = Logger::instance();
    log.info("SIGHUP: full reload starting...");
    try {
        reload_from_disk();
        log.info("SIGHUP: full reload complete.");
    } catch (const std::exception& e) {
        log.error("SIGHUP: reload failed: {}", e.what());
    }
}

void Daemon::refresh_iproute_and_firewall_runtime() {
    auto& log = Logger::instance();
    try {
        route_table_.clear();
        policy_rules_.clear();
        setup_static_routing();
        apply_firewall();
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
    if ((events & EPOLLIN) == 0) {
        return;
    }

    if (!interface_monitor_) {
        return;
    }

    try {
        interface_monitor_->handle_events();
    } catch (const std::exception& e) {
        Logger::instance().error("Interface monitor event handling failed: {}", e.what());
    }
}

void Daemon::run_system_resolver_hook_reload() {
    auto& log = Logger::instance();

    std::string command;
    int exit_code = 0;
    const bool ok = execute_system_resolver_reload_hook(
        config_,
        hook_command_executor_,
        command,
        exit_code);

    if (command.empty()) {
        return;
    }

    if (!ok) {
        log.warn("System resolver reload hook failed (exit code: {}): {}",
                 exit_code,
                 command);
        return;
    }

    log.info("System resolver reload hook complete: {}", command);
}

bool Daemon::routing_runtime_active() const {
    return runtime_state_store_.snapshot().routing_runtime_active;
}

void Daemon::stop_routing_runtime() {
    auto& log = Logger::instance();
    if (!routing_runtime_active_) {
        return;
    }

    runtime_generation_.fetch_add(1, std::memory_order_acq_rel);

    if (urltest_manager_) {
        urltest_manager_->clear();
    }
    route_table_.clear();
    policy_rules_.clear();
    firewall_->cleanup();

    if (config_.dns.has_value() && config_.dns->system_resolver.has_value()) {
        const auto args = build_system_resolver_hook_args(config_, "deactivate");
        const int exit_code = hook_command_executor_(args);
        if (exit_code != 0) {
            throw DaemonError("System resolver deactivate hook failed with exit code " +
                              std::to_string(exit_code));
        }
    }

    routing_runtime_active_ = false;
    refresh_resolver_config_hash_actual_async();
    publish_runtime_state();
    log.info("Routing runtime stopped.");
}

void Daemon::start_routing_runtime() {
    auto& log = Logger::instance();
    if (routing_runtime_active_) {
        return;
    }

    runtime_generation_.fetch_add(1, std::memory_order_acq_rel);

    setup_static_routing();
    register_urltest_outbounds();
    apply_firewall();

    if (config_.dns.has_value() && config_.dns->system_resolver.has_value()) {
        auto args = build_system_resolver_hook_args(config_, "ensure-runtime-prereqs");
        int exit_code = hook_command_executor_(args);
        if (exit_code != 0) {
            throw DaemonError("System resolver ensure-runtime-prereqs hook failed with exit code " +
                              std::to_string(exit_code));
        }
        args = build_system_resolver_hook_args(config_, "activate");
        exit_code = hook_command_executor_(args);
        if (exit_code != 0) {
            throw DaemonError("System resolver activate hook failed with exit code " +
                              std::to_string(exit_code));
        }
    }

    routing_runtime_active_ = true;
    refresh_resolver_config_hash_actual_async();
    publish_runtime_state();
    log.info("Routing runtime started.");
}

void Daemon::restart_routing_runtime() {
    if (!routing_runtime_active_) {
        throw DaemonError("Routing runtime is stopped");
    }

    stop_routing_runtime();
    start_routing_runtime();
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

void Daemon::run() {
    // --- Startup sequence ---
    auto& log = Logger::instance();

    write_pid_file();

    log.info("Loading lists...");
    (void)list_service_.refresh_remote_lists(config_, outbound_marks_);

    setup_static_routing();
    log.info("Static routing tables and ip rules installed.");

    register_urltest_outbounds();

    apply_firewall();
    log.info("Firewall rules and routing applied.");

    schedule_lists_autoupdate();

    update_resolver_config_hash();
    refresh_resolver_config_hash_actual_async();
    schedule_resolver_config_hash_actual_refresh();
    publish_runtime_state();

    setup_dns_probe();

    if (interface_monitor_) {
        add_fd(interface_monitor_->fd(),
               EPOLLIN,
               [this](uint32_t events) { handle_interface_monitor_events(events); },
               true,
               "interface-monitor");
    }

#ifdef WITH_API
    setup_api();
#endif

    log.info("Daemon running. PID: {}", getpid());

    // --- Event loop ---
    running_.store(true, std::memory_order_release);
    event_loop_thread_id_.store(std::this_thread::get_id(), std::memory_order_relaxed);
    event_loop_active_.store(true, std::memory_order_release);
    accept_posted_control_tasks_.store(true, std::memory_order_release);

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
            int fd = events[i].data.fd;

            if (fd == signal_fd_) {
                handle_signal();
                continue;
            }
            if (fd == control_fd_) {
                handle_control_commands();
                continue;
            }

            // Dispatch to registered fd callbacks
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
                callback(events[i].events);
            }
        }
    }

    event_loop_active_.store(false, std::memory_order_release);
    event_loop_thread_id_.store(std::thread::id{}, std::memory_order_relaxed);
    accept_posted_control_tasks_.store(false, std::memory_order_release);
    blocking_executor_.shutdown();

    // --- Shutdown sequence ---
    log.info("Shutting down...");

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
    route_table_.clear();
    policy_rules_.clear();
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
    if (pid_file.empty()) return;
    std::filesystem::create_directories(std::filesystem::path(pid_file).parent_path());

    int fd = open(pid_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        throw DaemonError("Cannot open PID file: " + pid_file + ": " + std::strerror(errno));
    }
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        close(fd);
        if (errno == EWOULDBLOCK) {
            throw DaemonError("Another instance is already running (PID file locked): " + pid_file);
        }
        throw DaemonError("Cannot lock PID file: " + pid_file + ": " + std::strerror(errno));
    }
    dprintf(fd, "%d\n", getpid());
    pid_file_fd_ = fd;
}

void Daemon::remove_pid_file() {
    if (pid_file_fd_ >= 0) {
        close(pid_file_fd_);
        pid_file_fd_ = -1;
    }
    const auto pid_file = config_.daemon.value_or(DaemonConfig{}).pid_file.value_or("");
    if (!pid_file.empty()) {
        std::filesystem::remove(pid_file);
    }
}

void Daemon::setup_static_routing() {
    populate_routing_state(
        config_,
        outbound_marks_,
        route_table_,
        policy_rules_,
        [this](const Outbound& outbound) {
            return is_interface_outbound_reachable(outbound, netlink_);
        },
        &firewall_state_.get_urltest_selections());
}

void Daemon::apply_firewall() {
    ListStreamer list_streamer(list_service_.cache_manager());
    auto rule_states =
        build_fw_rule_states(config_, outbound_marks_, &firewall_state_.get_urltest_selections());
    const RouteConfig route_config = config_.route.value_or(RouteConfig{});

    // Clean existing firewall state before rebuilding
    firewall_->cleanup();
    firewall_->set_global_prefilter(build_firewall_global_prefilter(config_));

    const auto& all_outbounds = config_.outbounds.value_or(std::vector<Outbound>{});
    static const std::map<std::string, ListConfig> empty_lists;
    const auto& lists_map = config_.lists ? *config_.lists : empty_lists;
    const auto& route_rules = route_config.rules.value_or(std::vector<RouteRule>{});
    std::map<std::string, ListSetUsage> list_usage_cache;

    for (size_t rule_idx = 0; rule_idx < route_rules.size(); ++rule_idx) {
        const auto& rule = route_rules[rule_idx];
        RuleState& rs = rule_states[rule_idx];

        if (rs.action_type == RuleActionType::Skip) {
            continue;
        }

        // Keep expected firewall state aligned with what we actually create.
        // build_fw_rule_states() pre-populates all static/dynamic set variants,
        // but apply_firewall() intentionally skips sets that would be always empty.
        rs.set_names.clear();

        const bool is_blackhole = (rs.action_type == RuleActionType::Drop);
        const bool is_pass = (rs.action_type == RuleActionType::Pass);

        // Create ipsets and stream entries for each list in the rule
        for (const auto& list_name : rule.list) {
            auto list_cfg_it = lists_map.find(list_name);
            if (list_cfg_it == lists_map.end()) continue;

            const auto& list_cfg = list_cfg_it->second;
            auto usage_it = list_usage_cache.find(list_name);
            if (usage_it == list_usage_cache.end()) {
                usage_it = list_usage_cache.emplace(
                    list_name,
                    analyze_list_set_usage(list_name, list_cfg, list_streamer)).first;
            }
            const auto& usage = usage_it->second;

            // Static sets: permanent IP/CIDR entries (no timeout)
            const std::string set4 = "kpbr4_"  + list_name;
            const std::string set6 = "kpbr6_"  + list_name;
            // Dynamic sets: dnsmasq-resolved entries (TTL from ttl_ms)
            const std::string set4d = "kpbr4d_" + list_name;
            const std::string set6d = "kpbr6d_" + list_name;

            if (usage.has_static_entries) {
                firewall_->create_ipset(set4, AF_INET, 0);
                firewall_->create_ipset(set6, AF_INET6, 0);
                rs.set_names.push_back(set4);
                rs.set_names.push_back(set6);

                auto loader4 = firewall_->create_batch_loader(set4);
                auto loader6 = firewall_->create_batch_loader(set6);
                FunctionalVisitor splitter([&](EntryType type, std::string_view entry) {
                    if (type == EntryType::Domain) return;
                    bool is_v6 = entry.find(':') != std::string_view::npos;
                    if (is_v6) loader6->on_entry(type, entry);
                    else       loader4->on_entry(type, entry);
                });
                list_streamer.stream_list(list_name, list_cfg, splitter);
                loader4->finish();
                loader6->finish();
            }

            if (usage.has_domain_entries) {
                firewall_->create_ipset(set4d, AF_INET, usage.dynamic_timeout);
                firewall_->create_ipset(set6d, AF_INET6, usage.dynamic_timeout);
                rs.set_names.push_back(set4d);
                rs.set_names.push_back(set6d);
            }

            // Build proto/port/addr filter from route rule.
            // Strip leading '!' to extract negation flags.
            auto strip_neg = [](const std::string& s) -> std::pair<std::string, bool> {
                if (!s.empty() && s[0] == '!') return {s.substr(1), true};
                return {s, false};
            };

            ProtoPortFilter filter;
            filter.proto = rule.proto.value_or("");

            {
                auto [port, neg]   = strip_neg(rule.src_port.value_or(""));
                filter.src_port        = port;
                filter.negate_src_port = neg;
            }
            {
                auto [port, neg]   = strip_neg(rule.dest_port.value_or(""));
                filter.dst_port        = port;
                filter.negate_dst_port = neg;
            }
            {
                AddrSpec s = parse_addr_spec(rule.src_addr.value_or(""));
                filter.negate_src_addr = s.negate;
                filter.src_addr        = std::move(s.addrs);
            }
            {
                AddrSpec s = parse_addr_spec(rule.dest_addr.value_or(""));
                filter.negate_dst_addr = s.negate;
                filter.dst_addr        = std::move(s.addrs);
            }

            // Create mark or drop rules for both static and dynamic sets (OR semantics)
            if (is_blackhole) {
                if (usage.has_static_entries) {
                    firewall_->create_drop_rule(set4, filter);
                    firewall_->create_drop_rule(set6, filter);
                }
                if (usage.has_domain_entries) {
                    firewall_->create_drop_rule(set4d, filter);
                    firewall_->create_drop_rule(set6d, filter);
                }
            } else if (is_pass) {
                if (usage.has_static_entries) {
                    firewall_->create_pass_rule(set4, filter);
                    firewall_->create_pass_rule(set6, filter);
                }
                if (usage.has_domain_entries) {
                    firewall_->create_pass_rule(set4d, filter);
                    firewall_->create_pass_rule(set6d, filter);
                }
            } else if (rs.fwmark != 0) {
                if (usage.has_static_entries) {
                    firewall_->create_mark_rule(set4, rs.fwmark, filter);
                    firewall_->create_mark_rule(set6, rs.fwmark, filter);
                }
                if (usage.has_domain_entries) {
                    firewall_->create_mark_rule(set4d, rs.fwmark, filter);
                    firewall_->create_mark_rule(set6d, rs.fwmark, filter);
                }
            }
        }
    }

    // DNS server detour: mark port-53 traffic for servers with a detour outbound
    if (config_.dns.has_value()) {
        const auto& dns_servers =
            config_.dns->servers.value_or(std::vector<DnsServer>{});
        const DnsServerRegistry dns_registry(config_.dns.value_or(DnsConfig{}));
        for (const auto& srv : dns_servers) {
            if (!srv.detour.has_value()) continue;

            const Outbound* detour_ob = find_outbound(all_outbounds, srv.detour.value());
            if (!detour_ob) continue;

            // Resolve URLTEST → selected child
            std::string effective_tag = detour_ob->tag;
            if (detour_ob->type == OutboundType::URLTEST) {
                auto selections = firewall_state_.get_urltest_selections();
                auto sel_it = selections.find(effective_tag);
                if (sel_it != selections.end() && !sel_it->second.empty()) {
                    const Outbound* child = find_outbound(all_outbounds, sel_it->second);
                    if (child) effective_tag = child->tag;
                }
            }

            auto mark_it = outbound_marks_.find(effective_tag);
            if (mark_it == outbound_marks_.end()) continue;

            const DnsServerConfig* resolved_server = dns_registry.get_server(srv.tag);
            if (!resolved_server) {
                throw DaemonError("DNS server tag not found during detour setup: " + srv.tag);
            }
            ProtoPortFilter filter;
            filter.proto    = "tcp/udp";
            filter.dst_port = std::to_string(resolved_server->port);
            filter.dst_addr = {resolved_server->resolved_ip};
            firewall_->create_direct_mark_rule(mark_it->second, filter);
        }
    }

    firewall_->apply();
    firewall_state_.set_rules(std::move(rule_states));
}

void Daemon::download_uncached_lists() {
    list_service_.download_uncached(config_, outbound_marks_);
}

void Daemon::register_urltest_outbounds() {
    const auto runtime_generation = runtime_generation_.load(std::memory_order_acquire);
    urltest_manager_ = std::make_unique<UrltestManager>(
        url_tester_,
        outbound_marks_,
        *scheduler_,
        blocking_executor_,
        [this](const std::string& urltest_tag, const std::string& new_child_tag) {
            post_control_task([this, urltest_tag, new_child_tag]() {
                auto& log = Logger::instance();
                log.info("Urltest '{}' selected outbound: '{}'", urltest_tag, new_child_tag);
                firewall_state_.set_urltest_selection(urltest_tag, new_child_tag);
                try {
                    route_table_.clear();
                    policy_rules_.clear();
                    setup_static_routing();
                    apply_firewall();
                    publish_runtime_state();
                    log.info("Routing and firewall rebuilt after urltest change.");
                } catch (const std::exception& e) {
                    log.error("Error rebuilding routing/firewall after urltest change: {}", e.what());
                }
            }, "urltest-selection-change:" + urltest_tag);
        },
        [this, runtime_generation](const std::string& urltest_tag,
                                   std::uint64_t probe_generation,
                                   std::map<std::string, URLTestResult> results,
                                   TraceId trace_id) mutable {
            Logger::instance().trace("urltest_commit_enqueue",
                                     "tag={} generation={} runtime_generation={}",
                                     urltest_tag,
                                     probe_generation,
                                     runtime_generation);
            post_control_task(
                [this,
                 urltest_tag,
                 probe_generation,
                 results = std::move(results),
                 trace_id,
                 runtime_generation]() mutable {
                    ScopedTraceContext trace_scope(trace_id);
                    if (runtime_generation != runtime_generation_.load(std::memory_order_acquire)) {
                        Logger::instance().trace("urltest_commit_skip",
                                                 "tag={} generation={} reason=stale_runtime",
                                                 urltest_tag,
                                                 probe_generation);
                        return;
                    }
                    if (!urltest_manager_) {
                        Logger::instance().trace("urltest_commit_skip",
                                                 "tag={} generation={} reason=missing_manager",
                                                 urltest_tag,
                                                 probe_generation);
                        return;
                    }
                    urltest_manager_->commit_probe_results(urltest_tag,
                                                           probe_generation,
                                                           std::move(results));
                    publish_runtime_state();
                },
                "urltest-commit:" + urltest_tag);
        });

    for (const auto& ob : config_.outbounds.value_or(std::vector<Outbound>{})) {
        if (ob.type == OutboundType::URLTEST) {
            urltest_manager_->register_urltest(ob);
        }
    }
}

void Daemon::schedule_lists_autoupdate() {
    if (!config_.lists_autoupdate) return;
    if (!config_.lists_autoupdate->enabled.value_or(false)) return;
    const auto& expr = config_.lists_autoupdate->cron.value_or("");
    auto next = cron_next(expr);
    const auto now = std::chrono::system_clock::now();
    auto delay = std::chrono::ceil<std::chrono::seconds>(next - now);
    if (delay.count() < 1) delay = std::chrono::seconds{1};
    lists_autoupdate_task_id_ = scheduler_->schedule_oneshot(
        delay,
        [this]() {
            refresh_lists_and_maybe_reload_async();
        },
        "lists-autoupdate");
    Logger::instance().info("Lists autoupdate scheduled (next: ~{}s)", delay.count());
}

ListsRefreshExecutionResult Daemon::execute_remote_list_refresh(
    const std::set<std::string>* target_lists) {
    auto& log = Logger::instance();
    ListsRefreshExecutionResult result;
    const auto relevant_lists = collect_relevant_list_names(config_);
    result.refresh_result =
        list_service_.refresh_remote_lists(config_, outbound_marks_, &relevant_lists, target_lists);

    if (should_reload_runtime_after_list_refresh(routing_runtime_active_, result.refresh_result)) {
        log.info("Lists refresh: relevant list(s) changed ({}), reloading runtime",
                 format_list_names(result.refresh_result.relevant_changed_lists));
        apply_config(config_, false);
        result.reloaded = true;
        return result;
    }

    if (result.refresh_result.any_relevant_changed()) {
        log.info("Lists refresh: relevant list(s) changed ({}), but runtime is stopped",
                 format_list_names(result.refresh_result.relevant_changed_lists));
    } else if (result.refresh_result.any_changed()) {
        log.info("Lists refresh: updated list(s) did not affect runtime config: {}",
                 format_list_names(result.refresh_result.changed_lists));
    } else {
        log.info("Lists refresh: no list updates");
    }

    return result;
}

void Daemon::refresh_lists_and_maybe_reload() {
    auto& log = Logger::instance();
    log.info("Lists autoupdate: checking for updated lists");

    try {
        const auto result = execute_remote_list_refresh();
        if (!result.reloaded) {
            schedule_lists_autoupdate();
        }
    } catch (const std::exception& e) {
        log.error("Lists autoupdate failed: {}", e.what());
        schedule_lists_autoupdate();
    }
}

void Daemon::refresh_lists_and_maybe_reload_async() {
    auto& log = Logger::instance();
    log.info("Lists autoupdate: checking for updated lists");

    bool expected = false;
    if (!remote_list_refresh_inflight_.compare_exchange_strong(expected,
                                                               true,
                                                               std::memory_order_acq_rel)) {
        Logger::instance().trace("lists_refresh_skip",
                                 "source=autoupdate reason=inflight");
        return;
    }

    const Config config_snapshot = config_;
    const OutboundMarkMap marks_snapshot = outbound_marks_;
    const bool runtime_active_snapshot = routing_runtime_active_;
    const auto relevant_lists = collect_relevant_list_names(config_snapshot);
    const auto generation = runtime_generation_.load(std::memory_order_acquire);
    const TraceId trace_id = ensure_trace_id();

    const bool enqueued = blocking_executor_.try_post(
        "lists-autoupdate",
        [this,
         config_snapshot,
         marks_snapshot,
         runtime_active_snapshot,
         relevant_lists,
         generation,
         trace_id]() mutable {
            ScopedTraceContext trace_scope(trace_id);
            std::optional<RemoteListsRefreshResult> refresh_result;
            std::string error;

            Logger::instance().trace("lists_refresh_start",
                                     "source=autoupdate generation={}",
                                     generation);
            try {
                refresh_result = list_service_.refresh_remote_lists(config_snapshot,
                                                                   marks_snapshot,
                                                                   &relevant_lists);
            } catch (const std::exception& e) {
                error = e.what();
            }

            post_control_task(
                [this,
                 config_snapshot,
                 runtime_active_snapshot,
                 generation,
                 refresh_result = std::move(refresh_result),
                 error = std::move(error),
                 trace_id]() mutable {
                    ScopedTraceContext trace_scope_inner(trace_id);
                    remote_list_refresh_inflight_.store(false, std::memory_order_release);

                    if (generation != runtime_generation_.load(std::memory_order_acquire)) {
                        Logger::instance().trace("lists_refresh_skip",
                                                 "source=autoupdate generation={} reason=stale_runtime",
                                                 generation);
                        schedule_lists_autoupdate();
                        return;
                    }

                    if (!error.empty()) {
                        Logger::instance().error("Lists autoupdate failed: {}", error);
                        schedule_lists_autoupdate();
                        return;
                    }

                    ListsRefreshExecutionResult result;
                    result.refresh_result = std::move(*refresh_result);

                    if (should_reload_runtime_after_list_refresh(runtime_active_snapshot,
                                                                result.refresh_result)) {
                        Logger::instance().info(
                            "Lists refresh: relevant list(s) changed ({}), reloading runtime",
                            format_list_names(result.refresh_result.relevant_changed_lists));
                        try {
                            apply_config(config_snapshot, false);
                            result.reloaded = true;
                        } catch (const std::exception& e) {
                            Logger::instance().error("Lists autoupdate reload failed: {}", e.what());
                            schedule_lists_autoupdate();
                            return;
                        }
                    } else if (result.refresh_result.any_relevant_changed()) {
                        Logger::instance().info(
                            "Lists refresh: relevant list(s) changed ({}), but runtime is stopped",
                            format_list_names(result.refresh_result.relevant_changed_lists));
                    } else if (result.refresh_result.any_changed()) {
                        Logger::instance().info(
                            "Lists refresh: updated list(s) did not affect runtime config: {}",
                            format_list_names(result.refresh_result.changed_lists));
                    } else {
                        Logger::instance().info("Lists refresh: no list updates");
                    }

                    if (!result.reloaded) {
                        schedule_lists_autoupdate();
                    }
                },
                "lists-refresh-commit");
        },
        trace_id);

    if (!enqueued) {
        remote_list_refresh_inflight_.store(false, std::memory_order_release);
        Logger::instance().trace("lists_refresh_skip",
                                 "source=autoupdate reason=executor_unavailable");
        schedule_lists_autoupdate();
    }
}

void Daemon::update_resolver_config_hash() {
    ListStreamer streamer(list_service_.cache_manager());
    const DnsConfig dns_cfg = config_.dns.value_or(DnsConfig{});
    const ResolverType resolver_type = resolver_type_from_dns_config(dns_cfg);
    DnsServerRegistry dns_registry(dns_cfg);
    resolver_config_hash_ = DnsmasqGenerator::compute_config_hash(
        dns_registry,
        streamer,
        config_.route.value_or(RouteConfig{}),
        dns_cfg,
        config_.lists.value_or(std::map<std::string, ListConfig>{}),
        resolver_type);
    Logger::instance().info("Resolver config hash: {}", resolver_config_hash_);
}

void Daemon::update_resolver_config_hash_actual() {
    resolver_config_hash_actual_.clear();
    resolver_config_hash_actual_ts_.reset();

    const auto dns_cfg_opt = config_.dns;
    if (!dns_cfg_opt.has_value() || !dns_cfg_opt->system_resolver.has_value()) {
        return;
    }

    const std::string& resolver_addr = dns_cfg_opt->system_resolver->address;
    if (resolver_addr.empty()) {
        return;
    }

    try {
        std::string error;
        auto txt = query_dns_txt_record(
            resolver_addr, "config-hash.keen.pbr", std::chrono::milliseconds(2000), &error);
        if (!txt.has_value()) {
            if (!error.empty()) {
                Logger::instance().warn(
                    "Resolver config hash TXT query failed via {}: {}",
                    resolver_addr,
                    error);
            }
            return;
        }

        const ResolverConfigHashTxtValue parsed = parse_resolver_config_hash_txt(*txt);
        resolver_config_hash_actual_ = parsed.hash;
        resolver_config_hash_actual_ts_ = parsed.ts;
        Logger::instance().info("Resolver config hash (actual): {}",
                                resolver_config_hash_actual_);
    } catch (const std::exception& e) {
        Logger::instance().warn("Resolver config hash TXT query failed via {}: {}",
                                resolver_addr,
                                e.what());
    }
}

RuntimeStateSnapshot Daemon::build_runtime_state_snapshot() const {
    RuntimeStateSnapshot snapshot;
    snapshot.firewall_state = firewall_state_;
    snapshot.route_specs = route_table_.get_routes();
    snapshot.policy_rule_specs = policy_rules_.get_rules();
    snapshot.resolver_config_hash = resolver_config_hash_;
    snapshot.resolver_config_hash_actual = resolver_config_hash_actual_;
    snapshot.resolver_config_hash_actual_ts = resolver_config_hash_actual_ts_;
    snapshot.routing_runtime_active = routing_runtime_active_;

    if (urltest_manager_) {
        for (const auto& outbound : config_.outbounds.value_or(std::vector<Outbound>{})) {
            if (outbound.type != OutboundType::URLTEST) {
                continue;
            }
            auto state = urltest_manager_->get_state(outbound.tag);
            if (state.has_value()) {
                snapshot.urltest_states.emplace(outbound.tag, std::move(*state));
            }
        }
    }

    return snapshot;
}

void Daemon::publish_runtime_state() {
    Logger::instance().trace("runtime_state_publish", "routing_runtime_active={}",
                             routing_runtime_active_ ? "true" : "false");
    runtime_state_store_.publish(build_runtime_state_snapshot());
}

void Daemon::schedule_resolver_config_hash_actual_refresh() {
    if (resolver_config_hash_actual_task_id_ >= 0) {
        scheduler_->cancel(resolver_config_hash_actual_task_id_);
    }
    resolver_config_hash_actual_task_id_ = scheduler_->schedule_repeating(
        std::chrono::seconds{300},
        [this]() {
            maybe_schedule_resolver_config_hash_actual_refresh();
        },
        "resolver-config-hash-actual");
}

void Daemon::refresh_resolver_config_hash_actual_async() {
    const auto dns_cfg_opt = config_.dns;
    if (!dns_cfg_opt.has_value() || !dns_cfg_opt->system_resolver.has_value()) {
        resolver_config_hash_actual_.clear();
        resolver_config_hash_actual_ts_.reset();
        publish_runtime_state();
        return;
    }

    const std::string resolver_addr = dns_cfg_opt->system_resolver->address;
    if (resolver_addr.empty()) {
        resolver_config_hash_actual_.clear();
        resolver_config_hash_actual_ts_.reset();
        publish_runtime_state();
        return;
    }

    bool expected = false;
    if (!resolver_hash_refresh_inflight_.compare_exchange_strong(expected,
                                                                 true,
                                                                 std::memory_order_acq_rel)) {
        Logger::instance().trace("resolver_hash_refresh_skip", "reason=inflight");
        return;
    }

    const auto generation = runtime_generation_.load(std::memory_order_acquire);
    const TraceId trace_id = ensure_trace_id();
    const bool enqueued = blocking_executor_.try_post(
        "resolver-config-hash-actual",
        [this, resolver_addr, generation, trace_id]() mutable {
            ScopedTraceContext trace_scope(trace_id);
            std::string error;
            ResolverConfigHashTxtValue parsed_value;
            bool has_parsed_value = false;

            Logger::instance().trace("resolver_hash_refresh_start",
                                     "resolver={} generation={}",
                                     resolver_addr,
                                     generation);
            try {
                auto txt = query_dns_txt_record(
                    resolver_addr,
                    "config-hash.keen.pbr",
                    std::chrono::milliseconds(2000),
                    &error);
                if (txt.has_value()) {
                    parsed_value = parse_resolver_config_hash_txt(*txt);
                    has_parsed_value = true;
                }
            } catch (const std::exception& e) {
                error = e.what();
            }

            post_control_task(
                [this,
                 resolver_addr,
                 generation,
                 parsed_value = std::move(parsed_value),
                 has_parsed_value,
                 error = std::move(error),
                 trace_id]() mutable {
                    ScopedTraceContext trace_scope_inner(trace_id);
                    resolver_hash_refresh_inflight_.store(false, std::memory_order_release);

                    if (generation != runtime_generation_.load(std::memory_order_acquire)) {
                        Logger::instance().trace("resolver_hash_refresh_skip",
                                                 "resolver={} generation={} reason=stale_runtime",
                                                 resolver_addr,
                                                 generation);
                        return;
                    }

                    if (has_parsed_value) {
                        const std::int64_t apply_started_ts =
                            apply_started_ts_.load(std::memory_order_acquire);
                        if (apply_started_ts > 0 &&
                            parsed_value.ts.has_value() &&
                            *parsed_value.ts < apply_started_ts) {
                            Logger::instance().verbose(
                                "Resolver config hash TXT is older than current apply; clearing cached actual value "
                                "(resolver={}, txt_ts={}, apply_started_ts={})",
                                resolver_addr,
                                *parsed_value.ts,
                                apply_started_ts);
                            has_parsed_value = false;
                        }
                    }

                    if (has_parsed_value) {
                        resolver_config_hash_actual_ = parsed_value.hash;
                        resolver_config_hash_actual_ts_ = parsed_value.ts;
                        Logger::instance().info("Resolver config hash (actual): {}",
                                                resolver_config_hash_actual_);
                    } else {
                        resolver_config_hash_actual_.clear();
                        resolver_config_hash_actual_ts_.reset();
                        if (!error.empty()) {
                            Logger::instance().warn(
                                "Resolver config hash TXT query failed via {}: {}; cleared cached actual value",
                                resolver_addr,
                                error);
                        }
                    }
                    publish_runtime_state();
                },
                "resolver-hash-refresh-commit");
        },
        trace_id);

    if (!enqueued) {
        resolver_hash_refresh_inflight_.store(false, std::memory_order_release);
        Logger::instance().trace("resolver_hash_refresh_skip",
                                 "reason=executor_unavailable");
    }
}

void Daemon::maybe_schedule_resolver_config_hash_actual_refresh() {
    if (resolver_hash_refresh_inflight_.load(std::memory_order_acquire)) {
        Logger::instance().trace("resolver_hash_refresh_skip", "reason=inflight");
        return;
    }
    refresh_resolver_config_hash_actual_async();
}

PreparedRuntimeInputs Daemon::prepare_runtime_inputs(const Config& config,
                                                     bool refresh_remote_lists) {
    TraceSpan span("prepare-runtime-inputs");
    validate_config(config);

    PreparedRuntimeInputs prepared;
    prepared.config = config;
    prepared.outbound_marks = allocate_outbound_marks(
        config.fwmark.value_or(FwmarkConfig{}),
        config.outbounds.value_or(std::vector<Outbound>{}));

    if (refresh_remote_lists) {
        (void)list_service_.refresh_remote_lists(prepared.config, prepared.outbound_marks);
        prepared.remote_lists_refreshed = true;
    }

    return prepared;
}

void Daemon::apply_prepared_runtime_inputs(PreparedRuntimeInputs prepared) {
    if (event_loop_active_.load(std::memory_order_acquire) && !is_event_loop_thread()) {
        throw DaemonError("apply_prepared_runtime_inputs must run on the control/event-loop thread");
    }

    runtime_generation_.fetch_add(1, std::memory_order_acq_rel);

    if (lists_autoupdate_task_id_ >= 0) {
        scheduler_->cancel(lists_autoupdate_task_id_);
        lists_autoupdate_task_id_ = -1;
    }
    if (resolver_config_hash_actual_task_id_ >= 0) {
        scheduler_->cancel(resolver_config_hash_actual_task_id_);
        resolver_config_hash_actual_task_id_ = -1;
    }

    outbound_marks_ = std::move(prepared.outbound_marks);
    config_ = std::move(prepared.config);
    firewall_state_.set_outbound_marks(outbound_marks_);

    setup_static_routing();
    teardown_dns_probe();

    if (urltest_manager_) {
        urltest_manager_->clear();
    }
    route_table_.clear();
    policy_rules_.clear();
    firewall_->cleanup();

    setup_static_routing();
    register_urltest_outbounds();
    apply_firewall();
    schedule_lists_autoupdate();
    update_resolver_config_hash();
    setup_dns_probe();
    run_system_resolver_hook_reload();
    refresh_resolver_config_hash_actual_async();
    schedule_resolver_config_hash_actual_refresh();

    config_store_.replace_active(config_, outbound_marks_);
    publish_runtime_state();
}

void Daemon::apply_config(Config config, bool refresh_remote_lists) {
    // Safety invariant:
    // - During runtime, apply_config must run on the control/event-loop thread.
    //   This guarantees scheduler fd mutations that internally enqueue control
    //   commands cannot deadlock behind a concurrent event-loop callback.
    // - Before the loop starts, inline startup apply is allowed.
    if (event_loop_active_.load(std::memory_order_acquire) && !is_event_loop_thread()) {
        throw DaemonError("apply_config must run on the control/event-loop thread");
    }

    apply_prepared_runtime_inputs(prepare_runtime_inputs(config, refresh_remote_lists));
}


void Daemon::apply_config_with_rollback(const Config& next_config, bool& rolled_back) {
    Config previous_config = config_;

    try {
        apply_config(next_config);
        rolled_back = false;
    } catch (...) {
        try {
            apply_config(previous_config);
            rolled_back = true;
        } catch (const std::exception& rollback_error) {
            Logger::instance().error("Rollback to previous config failed: {}", rollback_error.what());
            rolled_back = false;
        } catch (...) {
            Logger::instance().error("Rollback to previous config failed: unknown error");
            rolled_back = false;
        }
        throw;
    }
}

void Daemon::reload_from_disk() {
    std::ifstream ifs(config_path_);
    if (!ifs.is_open()) {
        throw DaemonError("Cannot open config file: " + config_path_);
    }

    std::ostringstream ss;
    ss << ifs.rdbuf();
    Config next_config = parse_config(ss.str());
    validate_config(next_config);
    apply_config(std::move(next_config));
}

#ifdef WITH_API
void Daemon::setup_api() {
    if (!config_.api || !config_.api->enabled.value_or(false) || opts_.no_api) return;

    api_server_ = std::make_unique<ApiServer>(*config_.api);
    const auto finish_config_operation = [this]() {
        KPBR_LOCK_GUARD(config_op_mutex_);
        config_op_state_.store(ConfigOperationState::Idle, std::memory_order_release);
        Logger::instance().trace("config_operation_state",
                                 "state={} reason=finish",
                                 config_operation_state_name(ConfigOperationState::Idle));
        config_op_cv_.notify_all();
    };
    // ApiContext provides synchronized access to Daemon-owned runtime state.
    api_ctx_ = std::make_unique<ApiContext>(ApiContext{
        config_path_,
        *dns_test_broadcaster_,
        [this]() {
            return config_store_.visible_config();
        },
        [this]() {
            return config_store_.config_is_draft();
        },
        [this](Config staged_config, std::string staged_config_json) {
            config_store_.stage_config(std::move(staged_config), std::move(staged_config_json));
        },
        [this]() -> std::optional<std::pair<Config, std::string>> {
            return config_store_.staged_snapshot();
        },
        [this]() {
            config_store_.clear_staged();
        },
        [this](const Config& config) {
            validate_config(config);

            const auto marks = allocate_outbound_marks(
                config.fwmark.value_or(FwmarkConfig{}),
                config.outbounds.value_or(std::vector<Outbound>{}));
            const auto runtime_snapshot = runtime_state_store_.snapshot();
            const auto& urltest_selections = runtime_snapshot.firewall_state.get_urltest_selections();

            (void)build_fw_rule_states(config, marks, &urltest_selections);

            ListStreamer streamer(list_service_.cache_manager());
            const DnsConfig dns_cfg = config.dns.value_or(DnsConfig{});
            const ResolverType resolver_type = resolver_type_from_dns_config(dns_cfg);
            DnsServerRegistry dns_registry(dns_cfg);
            (void)DnsmasqGenerator::compute_config_hash(
                dns_registry,
                streamer,
                config.route.value_or(RouteConfig{}),
                dns_cfg,
                config.lists.value_or(std::map<std::string, ListConfig>{}),
                resolver_type);
        },
        [this]() {
            const auto runtime_snapshot = runtime_state_store_.snapshot();
            ServiceHealthState service_health;
            service_health.status = runtime_snapshot.routing_runtime_active
                ? api::HealthResponseStatus::RUNNING
                : api::HealthResponseStatus::STOPPED;
            service_health.resolver_config_hash = runtime_snapshot.resolver_config_hash;
            service_health.resolver_config_hash_actual = runtime_snapshot.resolver_config_hash_actual;
            service_health.resolver_config_hash_actual_ts = runtime_snapshot.resolver_config_hash_actual_ts;
            const std::int64_t apply_started_ts = apply_started_ts_.load(std::memory_order_acquire);
            if (apply_started_ts > 0) {
                service_health.apply_started_ts = apply_started_ts;
            }
            service_health.resolver_config_sync_state = classify_resolver_config_sync_state(
                runtime_snapshot.resolver_config_hash_actual_ts,
                service_health.apply_started_ts,
                unix_timestamp_now_seconds(),
                runtime_snapshot.resolver_config_hash ==
                    runtime_snapshot.resolver_config_hash_actual);
            service_health.config_is_draft = config_store_.config_is_draft();
            return service_health;
        },
        [this]() {
            const auto runtime_snapshot = runtime_state_store_.snapshot();

            return build_routing_health_report(
                firewall_->backend(),
                runtime_snapshot.firewall_state,
                runtime_snapshot.route_specs,
                runtime_snapshot.policy_rule_specs,
                netlink_);
        },
        [this]() {
            const Config config_snapshot = config_store_.active_config();
            const auto runtime_snapshot = runtime_state_store_.snapshot();

            return build_runtime_outbounds_response(
                config_snapshot,
                netlink_,
                [&runtime_snapshot](const std::string& tag) -> std::optional<UrltestState> {
                    auto it = runtime_snapshot.urltest_states.find(tag);
                    if (it == runtime_snapshot.urltest_states.end()) {
                        return std::nullopt;
                    }
                    return it->second;
                });
        },
        [this]() {
            return build_runtime_interface_inventory_response(netlink_);
        },
        [this](const Config& config) {
            return build_list_refresh_state_map(config, list_service_.cache_manager());
        },
        [this](const std::string& target) {
            const Config visible_config = config_store_.visible_config();
            return compute_test_routing(visible_config, list_service_.cache_manager(), target);
        },
        [this]() {
            KPBR_LOCK_GUARD(config_op_mutex_);
            if (config_op_state_.load(std::memory_order_acquire) != ConfigOperationState::Idle) {
                throw ApiError("Another config operation is already in progress", 409);
            }
            config_op_state_.store(ConfigOperationState::Saving, std::memory_order_release);
            Logger::instance().trace("config_operation_state",
                                     "state={} reason=begin-save",
                                     config_operation_state_name(ConfigOperationState::Saving));
        },
        finish_config_operation,
        [this](Config config, std::string saved_config_json) -> ConfigApplyResult {
            auto result = std::make_shared<ConfigApplyResult>();
            auto prepared = std::make_shared<PreparedRuntimeInputs>();
            auto rollback_prepared = std::make_shared<PreparedRuntimeInputs>();
            result->apply_started_ts = unix_timestamp_now_seconds();
            apply_started_ts_.store(*result->apply_started_ts, std::memory_order_release);

            try {
                *prepared = prepare_runtime_inputs(config, true);
                *rollback_prepared = prepare_runtime_inputs(config_store_.active_config(), false);
            } catch (const std::exception& e) {
                result->error = e.what();
                Logger::instance().error("Prepare staged config task failed: {}", e.what());
                return *result;
            }

            enqueue_control_task(
                [this,
                 result,
                 prepared,
                 rollback_prepared,
                 saved_config_json = std::move(saved_config_json)]() mutable {
                    try {
                        apply_prepared_runtime_inputs(std::move(*prepared));
                        result->applied = true;
                        result->rolled_back = false;
                        config_store_.clear_staged_if_matches(saved_config_json);
                    } catch (const std::exception& e) {
                        result->error = e.what();
                        Logger::instance().error("Apply staged config task failed: {}", e.what());

                        try {
                            apply_prepared_runtime_inputs(std::move(*rollback_prepared));
                            result->rolled_back = true;
                        } catch (const std::exception& rollback_error) {
                            result->rolled_back = false;
                            Logger::instance().error("Rollback to previous config failed: {}",
                                                     rollback_error.what());
                        } catch (...) {
                            result->rolled_back = false;
                            Logger::instance().error("Rollback to previous config failed: unknown error");
                        }
                    }
                },
                true,
                "api-apply-config");
            return *result;
        },
        [this, finish_config_operation]() {
            KPBR_UNIQUE_LOCK(lock, config_op_mutex_);
            if (config_op_state_.load(std::memory_order_acquire) != ConfigOperationState::Idle) {
                throw ApiError("Another config operation is already in progress", 409);
            }
            if (runtime_state_store_.snapshot().routing_runtime_active) {
                throw ApiError("Routing runtime is already started", 409);
            }
            config_op_state_.store(ConfigOperationState::Reloading, std::memory_order_release);
            Logger::instance().trace("config_operation_state",
                                     "state={} reason=start-runtime",
                                     config_operation_state_name(ConfigOperationState::Reloading));
            lock.unlock();

            try {
                enqueue_control_task([this]() {
                    try {
                        start_routing_runtime();
                    } catch (const std::exception& e) {
                        Logger::instance().error("Start routing runtime task failed: {}", e.what());
                        throw;
                    }
                }, true, "api-start-runtime");
            } catch (...) {
                finish_config_operation();
                throw;
            }

            finish_config_operation();
        },
        [this, finish_config_operation]() {
            KPBR_UNIQUE_LOCK(lock, config_op_mutex_);
            if (config_op_state_.load(std::memory_order_acquire) != ConfigOperationState::Idle) {
                throw ApiError("Another config operation is already in progress", 409);
            }
            if (!runtime_state_store_.snapshot().routing_runtime_active) {
                throw ApiError("Routing runtime is already stopped", 409);
            }
            config_op_state_.store(ConfigOperationState::Reloading, std::memory_order_release);
            Logger::instance().trace("config_operation_state",
                                     "state={} reason=stop-runtime",
                                     config_operation_state_name(ConfigOperationState::Reloading));
            lock.unlock();

            try {
                enqueue_control_task([this]() {
                    try {
                        stop_routing_runtime();
                    } catch (const std::exception& e) {
                        Logger::instance().error("Stop routing runtime task failed: {}", e.what());
                        throw;
                    }
                }, true, "api-stop-runtime");
            } catch (...) {
                finish_config_operation();
                throw;
            }

            finish_config_operation();
        },
        [this, finish_config_operation]() {
            KPBR_UNIQUE_LOCK(lock, config_op_mutex_);
            if (config_op_state_.load(std::memory_order_acquire) != ConfigOperationState::Idle) {
                throw ApiError("Another config operation is already in progress", 409);
            }
            if (!runtime_state_store_.snapshot().routing_runtime_active) {
                throw ApiError("Routing runtime is stopped; start it first", 409);
            }
            config_op_state_.store(ConfigOperationState::Reloading, std::memory_order_release);
            Logger::instance().trace("config_operation_state",
                                     "state={} reason=restart-runtime",
                                     config_operation_state_name(ConfigOperationState::Reloading));
            lock.unlock();

            try {
                enqueue_control_task([this]() {
                    try {
                        restart_routing_runtime();
                    } catch (const std::exception& e) {
                        Logger::instance().error("Restart routing runtime task failed: {}", e.what());
                        throw;
                    }
                }, true, "api-restart-runtime");
            } catch (...) {
                finish_config_operation();
                throw;
            }

            finish_config_operation();
        },
        [this, finish_config_operation](std::optional<std::string> requested_name) {
            KPBR_UNIQUE_LOCK(lock, config_op_mutex_);
            if (config_op_state_.load(std::memory_order_acquire) != ConfigOperationState::Idle) {
                throw ApiError("Another config operation is already in progress", 409);
            }
            if (config_store_.config_is_draft()) {
                throw ApiError("List refresh is unavailable while a draft config is staged", 409);
            }
            config_op_state_.store(ConfigOperationState::Reloading, std::memory_order_release);
            Logger::instance().trace("config_operation_state",
                                     "state={} reason=refresh-lists",
                                     config_operation_state_name(ConfigOperationState::Reloading));
            lock.unlock();

            const Config config_snapshot = config_store_.active_config();
            const auto marks_snapshot = allocate_outbound_marks(
                config_snapshot.fwmark.value_or(FwmarkConfig{}),
                config_snapshot.outbounds.value_or(std::vector<Outbound>{}));
            const bool runtime_active_snapshot = runtime_state_store_.snapshot().routing_runtime_active;
            const auto target_selection = select_remote_list_targets(config_snapshot, requested_name);
            if (!target_selection.ok()) {
                finish_config_operation();
                switch (target_selection.error) {
                case RemoteListTargetSelectionError::NotFound:
                    throw ApiError("Requested list was not found", 404);
                case RemoteListTargetSelectionError::NotRemote:
                    throw ApiError("Requested list is not URL-backed", 400);
                case RemoteListTargetSelectionError::None:
                    break;
                }
            }

            try {
                const std::set<std::string> relevant_lists =
                    collect_relevant_list_names(config_snapshot);
                const std::set<std::string> target_lists(target_selection.list_names.begin(),
                                                         target_selection.list_names.end());
                RemoteListsRefreshResult refresh_result = list_service_.refresh_remote_lists(
                    config_snapshot,
                    marks_snapshot,
                    &relevant_lists,
                    requested_name ? &target_lists : nullptr);

                bool reloaded = false;
                bool stale_runtime = false;
                const auto generation = runtime_generation_.load(std::memory_order_acquire);

                enqueue_control_task(
                    [this,
                     &reloaded,
                     &stale_runtime,
                     config_snapshot,
                     generation,
                     runtime_active_snapshot,
                     refresh_result]() mutable {
                        if (generation != runtime_generation_.load(std::memory_order_acquire)) {
                            stale_runtime = true;
                            Logger::instance().trace("lists_refresh_skip",
                                                     "source=api reason=stale_runtime generation={}",
                                                     generation);
                            return;
                        }

                        if (should_reload_runtime_after_list_refresh(runtime_active_snapshot,
                                                                    refresh_result)) {
                            apply_config(config_snapshot, false);
                            reloaded = true;
                        }
                    },
                    true,
                    "api-refresh-lists-commit");

                ListRefreshOperationResult operation_result;
                operation_result.refreshed_lists = std::move(refresh_result.refreshed_lists);
                operation_result.changed_lists = std::move(refresh_result.changed_lists);
                operation_result.reloaded = reloaded;

                finish_config_operation();

                if (!target_selection.ok()) {
                    return operation_result;
                }
                if (!operation_result.refreshed_lists.size()) {
                    operation_result.message = "No URL-backed lists to refresh";
                } else if (stale_runtime) {
                    operation_result.message =
                        "Lists refreshed; runtime changed before reload could be applied";
                } else if (operation_result.changed_lists.empty()) {
                    operation_result.message = "Lists refreshed; no updates found";
                } else if (operation_result.reloaded) {
                    operation_result.message = "Lists refreshed and runtime reloaded";
                } else if (refresh_result.any_relevant_changed()) {
                    operation_result.message =
                        "Lists refreshed; runtime is stopped so changes will apply on next start";
                } else {
                    operation_result.message = "Lists refreshed";
                }

                return operation_result;
            } catch (...) {
                finish_config_operation();
                throw;
            }
        },
    });
    register_api_handlers(*api_server_, *api_ctx_);

    const std::filesystem::path frontend_root(KEEN_PBR_FRONTEND_ROOT);
    const std::filesystem::path frontend_index = frontend_root / "index.html";
    std::filesystem::path frontend_index_gzip = frontend_index;
    frontend_index_gzip += ".gz";
    const bool has_frontend_root =
        std::filesystem::is_directory(frontend_root) &&
        (std::filesystem::is_regular_file(frontend_index) ||
         std::filesystem::is_regular_file(frontend_index_gzip));
    if (!has_frontend_root) {
        Logger::instance().warn(
            "API enabled but frontend root is unavailable: {} (missing directory or index.html(.gz)). API endpoints will remain available.",
            frontend_root.string());
    } else if (!api_server_->register_static_root(frontend_root.string())) {
        Logger::instance().warn(
            "Failed to register frontend static root: {}. API endpoints will remain available.",
            frontend_root.string());
    } else {
        Logger::instance().info("Frontend static root: {}", frontend_root.string());
    }

    const std::string listen_addr = config_.api->listen.value_or("0.0.0.0:12121");
    Logger::instance().info("Starting REST API on {}", listen_addr);
    try {
        api_server_->start();
        Logger::instance().info("REST API listening on {}", listen_addr);
    } catch (const ApiError& e) {
        Logger::instance().error("REST API startup failed on {}: {}", listen_addr, e.what());
        throw;
    } catch (const std::exception& e) {
        Logger::instance().error("Unexpected REST API startup failure on {}: {}",
                                 listen_addr,
                                 e.what());
        throw;
    }
}
#endif

void Daemon::setup_dns_probe() {
    teardown_dns_probe();

    if (!config_.dns || !config_.dns->dns_test_server.has_value()) {
        return;
    }

    const auto& test_cfg = *config_.dns->dns_test_server;
    const std::string* answer_ip = test_cfg.answer_ipv4 ? &*test_cfg.answer_ipv4 : nullptr;
    auto settings = parse_dns_probe_server_settings(test_cfg.listen, answer_ip);

    auto on_query = [this](const DnsProbeEvent& event) {
#ifdef WITH_API
        if (dns_test_broadcaster_) {
            nlohmann::json payload = {
                {"type", "DNS"},
                {"domain", event.domain},
                {"source_ip", event.source_ip},
                {"ecs", event.ecs.has_value() ? nlohmann::json(*event.ecs) : nlohmann::json(nullptr)},
            };
            dns_test_broadcaster_->publish(payload.dump());
        }
#else
        (void)event;
#endif
    };

    dns_probe_server_ = std::make_unique<DnsProbeServer>(settings, std::move(on_query));

    add_fd(dns_probe_server_->udp_fd(), EPOLLIN, [this](uint32_t events) {
        if ((events & EPOLLIN) && dns_probe_server_) {
            dns_probe_server_->handle_udp_readable();
        }
    });

    add_fd(dns_probe_server_->tcp_fd(), EPOLLIN, [this](uint32_t events) {
        if (!(events & EPOLLIN) || !dns_probe_server_) {
            return;
        }

        for (int client_fd : dns_probe_server_->accept_tcp_clients()) {
            add_fd(client_fd, EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR,
                   [this, client_fd](uint32_t client_events) {
                if (!dns_probe_server_) {
                    remove_fd(client_fd);
                    close(client_fd);
                    return;
                }

                bool keep_alive = false;
                if (client_events & EPOLLIN) {
                    keep_alive = dns_probe_server_->handle_tcp_client_readable(client_fd);
                }
                if (client_events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    keep_alive = false;
                }

                if (!keep_alive) {
                    dns_probe_server_->remove_tcp_client(client_fd);
                    remove_fd(client_fd);
                    close(client_fd);
                }
            });
        }
    });

    add_fd(dns_probe_server_->tcp_idle_timer_fd(), EPOLLIN, [this](uint32_t events) {
        if (!(events & EPOLLIN) || !dns_probe_server_) {
            return;
        }
        for (int client_fd : dns_probe_server_->handle_tcp_idle_timeout()) {
            dns_probe_server_->remove_tcp_client(client_fd);
            remove_fd(client_fd);
            close(client_fd);
        }
    });

    Logger::instance().info("DNS test server listening on {}", settings.listen);
}

void Daemon::teardown_dns_probe() {
    if (!dns_probe_server_) {
        return;
    }

    for (int fd : dns_probe_server_->all_fds()) {
        remove_fd(fd);
    }
    dns_probe_server_.reset();
}

} // namespace keen_pbr3
