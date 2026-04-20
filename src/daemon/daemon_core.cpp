#include "daemon.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <sys/file.h>
#include <sys/signalfd.h>
#include <thread>
#include <unistd.h>

#include "../firewall/firewall.hpp"
#include "../firewall/firewall_verifier.hpp"
#include "../log/logger.hpp"
#include "../util/daemon_signals.hpp"
#include "../dns/dns_probe_server.hpp" // IWYU pragma: keep
#include "scheduler.hpp"

#ifdef WITH_API
#include "../api/handlers.hpp" // IWYU pragma: keep
#include "../api/server.hpp"
#include "../api/sse_broadcaster.hpp"
#endif

namespace keen_pbr3 {

namespace {

constexpr auto SIGUSR1_DEBOUNCE_DELAY = std::chrono::milliseconds{150};

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

    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        throw DaemonError("epoll_create1 failed: " + std::string(strerror(errno)));
    }

    setup_signals();
    setup_control_channel();

    const int64_t verify_max_bytes = config_.daemon.value_or(DaemonConfig{})
        .firewall_verify_max_bytes.value_or(static_cast<int64_t>(DEFAULT_FIREWALL_VERIFY_CAPTURE_MAX_BYTES));
    set_firewall_verifier_capture_max_bytes(static_cast<size_t>(verify_max_bytes));

    firewall_state_.set_outbound_marks(outbound_marks_);
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
            Logger::instance().info("SIGUSR1: applying firewall refresh...");
            refresh_iproute_and_firewall_runtime();
            
            if (urltest_manager_) {
                Logger::instance().info("SIGUSR1: probing urltest endpoints...");
                for (const auto& ob : config_.outbounds.value_or(std::vector<Outbound>{})) {
                    if (ob.type == OutboundType::URLTEST) {
                        urltest_manager_->trigger_immediate_test(ob.tag);
                    }
                }
            }
            Logger::instance().info("SIGUSR1: firewall refresh complete.");
        },
        "sigusr1-runtime-refresh");
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

} // namespace keen_pbr3
