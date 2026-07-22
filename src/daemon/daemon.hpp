#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include "../config/config.hpp"
#include "../dns/dns_txt_client.hpp"
#include "config_store.hpp"
#include "pid_file.hpp"
#include "../health/url_tester.hpp"
#include "../routing/interface_monitor.hpp"
#include "../routing/firewall_state.hpp"
#include "../routing/netlink.hpp"
#include "../routing/policy_rule.hpp"
#include "../routing/route_table.hpp"
#include "../firewall/firewall.hpp"
#include "../util/blocking_executor.hpp"
#include "../util/traced_mutex.hpp"
#include "list_service.hpp"
#include "runtime_state_store.hpp"
#include "resolver_sync_state_machine.hpp"
#include "system_resolver_hook.hpp"
#include "../runtime/conntrack_manager.hpp"
#include "../runtime/resolver_coordinator.hpp"
#include "../runtime/runtime_state_machine.hpp"
#include "../runtime/operation_coordinator.hpp"
#include "../runtime/lifecycle_operation.hpp"

namespace keen_pbr3 {

class Firewall;
class Scheduler;
class UrltestManager;
class DnsProbeServer;
struct DnsProbeEvent;
enum class ResolverType;

#ifdef WITH_API
enum class ConfigOperationState : uint8_t;
class ApiServer;
struct ApiContext;
class SseBroadcaster;
class StatusStream;
struct ConfigApplyResult;
struct LifecycleRequest;
struct ListRefreshOperationResult;
#endif

class DaemonError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Callback for file descriptor events
using FdCallback = std::function<void(uint32_t events)>;

// Options controlling daemon runtime behavior
struct DaemonOptions {
    bool no_api{false};
};

struct ListsRefreshExecutionResult {
    RemoteListsRefreshResult refresh_result;
    bool reloaded{false};
};

struct PreparedRuntimeInputs {
    Config config;
    OutboundMarkMap outbound_marks;
    bool remote_lists_refreshed{false};
};

struct ResolverGenerationSnapshot {
    Config config;
    ResolverType resolver_type;
    bool ipv6_enabled{true};
    std::string expected_hash;
    std::uint64_t generation{0};
};

// Helper to get tag from any outbound variant
std::string get_outbound_tag(const Outbound& ob);

// Find an outbound by tag, returning pointer or nullptr
const Outbound* find_outbound(const std::vector<Outbound>& outbounds,
                               const std::string& tag);

// Epoll-based daemon that owns all runtime subsystems.
// Handles signal dispatch, routing, firewall, urltest, and API lifecycle.
class Daemon {
public:
    Daemon(Config config,
           std::string config_path,
           DaemonOptions opts,
           HookCommandExecutor hook_command_executor = default_hook_command_executor);
    ~Daemon();

    // Non-copyable, non-movable
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;
    Daemon(Daemon&&) = delete;
    Daemon& operator=(Daemon&&) = delete;

    // Register an additional file descriptor for epoll monitoring.
    void add_fd(int fd,
                uint32_t events,
                FdCallback cb,
                bool wait_for_completion = true,
                const std::string& label = "");

    // Remove a previously registered file descriptor.
    void remove_fd(int fd,
                   bool wait_for_completion = true,
                   const std::string& label = "");

    // Serialize execution of control operations in event loop.
    void enqueue_control_task(std::function<void()> task,
                              bool wait_for_completion = false,
                              const std::string& label = "");

    // Backward-compatible alias for enqueue_control_task.
    void enqueue_control_command(std::function<void()> command,
                                 bool wait_for_completion = false,
                                 const std::string& label = "");

    // Post a task to the event loop, always deferred to the next iteration.
    // Unlike enqueue_control_task, never executes inline even when called from
    // the event loop thread. Safe to call while holding any lock — the posted
    // task only runs after the current event-loop iteration completes and all
    // caller locks have been released. Use this for callbacks that must not
    // run re-entrantly inside the current controller action.
    void post_control_task(std::function<void()> task,
                           const std::string& label = "");

    // Run the daemon lifecycle: startup, event loop, shutdown.
    void run();

    // Request the event loop to stop.
    void stop();

    // Returns true if the daemon is currently running.
    bool running() const;

private:
    // control loop and fd registration
    void setup_signals();
    void handle_signal();
    void setup_control_channel();
    void handle_control_commands();
    void setup_ipc_control_socket();
    void handle_ipc_control_socket();
    void remove_ipc_control_socket() noexcept;
    void wake_control_loop();
    bool is_event_loop_thread() const;

    // Signal handlers
    void handle_sigusr1();
    void schedule_sigusr1_runtime_refresh();
    void handle_sighup();
    void handle_interface_monitor_events(uint32_t events);
    void reconnect_interface_monitor();
    void register_interface_monitor_fd();
    void unregister_interface_monitor_fd();
    void schedule_interface_monitor_reconnect_retry();
    void handle_interface_event(const InterfaceMonitor::Event& event);
    bool is_interface_outbound_in_use(const std::string& interface_name) const;
    void refresh_iproute_and_firewall_runtime();
    void dispatch_event_fd(int fd, uint32_t events);
    void run_event_loop();
    void begin_startup_runtime();
    void continue_startup_after_lists(std::optional<RemoteListsRefreshResult> refresh_result,
                                      std::string error);
    void finish_startup_after_resolver_hook(bool hook_succeeded, std::string error);
    void fail_startup_runtime(std::string error);

    // lifecycle and runtime apply
    void setup_static_routing();
    void reconcile_static_routing();
    void apply_firewall(FirewallApplyMode mode = FirewallApplyMode::Destructive);
    void reconcile_lists_only(bool reload_resolver);
    void register_urltest_outbounds();
    void handle_urltest_selection_change(const std::string& urltest_tag,
                                         const std::string& new_child_tag);
    void commit_urltest_probe_results(const std::string& urltest_tag,
                                      std::uint64_t probe_generation,
                                      std::map<std::string, URLTestResult> results,
                                      TraceId trace_id);
    void apply_config(Config config, bool refresh_remote_lists = true);
    // Candidate application may mutate kernel/resolver state while keeping the
    // externally visible active snapshot unchanged until its transaction commits.
    void apply_prepared_runtime_inputs(PreparedRuntimeInputs prepared,
                                       bool publish_active_snapshot = true);
    PreparedRuntimeInputs prepare_runtime_inputs(const Config& config,
                                                bool refresh_remote_lists = true);
    void reload_from_disk();
    void teardown_routing_and_firewall(bool explicit_stop);
    void setup_routing_and_firewall();
    void reconcile_prepared_runtime(PreparedRuntimeInputs prepared);
    void complete_running_runtime(const char* reason);
    bool has_system_resolver(const Config& config) const;
    void start_routing_runtime();
    void stop_routing_runtime();
    void restart_routing_runtime();
    bool routing_runtime_active() const;
    void transition_runtime_or_throw(RuntimeState next, const char* reason);
    bool run_system_resolver_hook(std::string_view action);
    bool run_system_resolver_hook_reload();
    bool wait_for_resolver_stream_after(std::uint64_t baseline,
                                        std::chrono::milliseconds timeout);
    void drain_shutdown_resolver_callbacks(std::chrono::milliseconds duration);
    void schedule_lists_autoupdate();
    ListsRefreshExecutionResult execute_remote_list_refresh(
        const std::set<std::string>* target_lists = nullptr,
        std::string_view source = "service");
    void refresh_lists_and_maybe_reload();
    void refresh_lists_and_maybe_reload_async();
    void commit_lists_refresh_async_result(Config config_snapshot,
                                           bool runtime_active_snapshot,
                                           std::uint64_t generation,
                                           std::optional<RemoteListsRefreshResult> refresh_result,
                                           std::string error,
                                           TraceId trace_id);

    // PID file management
    void write_pid_file();
    void remove_pid_file();

    // state publication and resolver sync
    void refresh_resolver_config_hash_actual_async();
    void maybe_schedule_resolver_config_hash_actual_refresh();
    void schedule_resolver_config_hash_actual_retry();
    void schedule_keenetic_dns_refresh();
    bool refresh_keenetic_dns_cache(bool force_refresh);
    void reset_resolver_actual_state();
    void commit_resolver_hash_probe_result(const std::string& resolver_addr,
                                           std::uint64_t generation,
                                           std::optional<ResolverConfigHashProbeResult> probe_result,
                                           std::optional<std::int64_t> probe_completed_ts,
                                           TraceId trace_id);
    static bool wait_for_resolver_config_hash_confirmation(
        const Config& candidate,
        const std::string& expected_hash,
        std::int64_t apply_started_ts,
        std::string& error);

#ifdef WITH_API
    // API integration
    void setup_api();
    void finish_config_operation();
    void begin_config_operation_or_throw(ConfigOperationState state,
                                         const char* reason,
                                         bool require_runtime_running,
                                         bool require_runtime_stopped);
    ConfigApplyResult apply_validated_config_via_control_task(
        Config config,
        std::string saved_config_json,
        bool persist_config = true);
    std::string submit_lifecycle_operation(LifecycleRequest request);
    void execute_lifecycle_operation(std::string operation_id,
                                     LifecycleRequest request);
    void run_runtime_control_operation_or_throw(const std::string& label,
                                                const char* operation_name,
                                                std::function<void()> task);
    ListRefreshOperationResult refresh_lists_via_api(std::optional<std::string> requested_name);
#endif

    // DNS probe integration
    void setup_dns_probe();
    void teardown_dns_probe();
    void handle_dns_probe_query_event(const DnsProbeEvent& event);
    void handle_dns_probe_udp_events(uint32_t events);
    void handle_dns_probe_tcp_listener_events(uint32_t events);
    void handle_dns_probe_tcp_client_events(int client_fd, uint32_t events);
    void handle_dns_probe_tcp_timer_events(uint32_t events);

    ResolverSyncStateMachine resolver_sync_;
    // Timestamp captured when /api/config/save apply starts (server authoritative).
    std::atomic<std::int64_t> apply_started_ts_{0};

    // Recompute resolver_config_hash_ from current config/cache state
    void update_resolver_config_hash();
    ResolverGenerationSnapshot make_resolver_generation_snapshot();
    // Schedule (or reschedule) the periodic refresh of resolver_config_hash_actual_.
    void schedule_resolver_config_hash_actual_refresh();
    RuntimeStateSnapshot build_runtime_state_snapshot() const;
    void publish_runtime_state();

    // Lists autoupdate state
    int lists_autoupdate_task_id_{-1};
    // Periodic refresh task for cached Keenetic DNS server values.
    int keenetic_dns_refresh_task_id_{-1};
    // Periodic refresh task for the actual resolver config hash / live status.
    int resolver_config_hash_actual_task_id_{-1};
    // Short-interval retry while resolver hash is converging after apply.
    int resolver_config_hash_actual_retry_task_id_{-1};
    // Debounced runtime refresh triggered by SIGUSR1.
    int sigusr1_refresh_task_id_{-1};
    // Retry task for interface monitor netlink reconnect after failure.
    int interface_monitor_reconnect_task_id_{-1};

    // Epoll state
    int epoll_fd_{-1};
    int signal_fd_{-1};
    std::atomic<bool> running_{false};
    std::atomic<std::thread::id> event_loop_thread_id_{};
    std::atomic<bool> event_loop_active_{false};
    std::atomic<bool> accept_posted_control_tasks_{true};

    struct FdEntry {
        int fd;
        FdCallback callback;
    };
    mutable TracedMutex fd_entries_mutex_;
    std::vector<FdEntry> fd_entries_ GUARDED_BY(fd_entries_mutex_);

    PidFile pid_file_;
    int control_fd_{-1};
    int ipc_control_fd_{-1};
    std::string ipc_control_socket_path_;
    struct ControlTask {
        std::function<void()> callback;
        std::string label;
        TraceId trace_id{0};
    };
    TracedMutex control_tasks_mutex_;
    std::vector<ControlTask> control_tasks_ GUARDED_BY(control_tasks_mutex_);

#ifdef WITH_API
    TracedMutex config_op_mutex_;
    OperationCoordinator operation_coordinator_;
    std::condition_variable_any config_op_cv_;
    std::atomic<ConfigOperationState> config_op_state_{static_cast<ConfigOperationState>(0)};
#endif

    // Snapshot stores
    ConfigStore config_store_;
    ListService list_service_;
    RuntimeStateStore runtime_state_store_;
    LifecycleOperationStore lifecycle_operation_store_;
    LifecycleOperationCoordinator lifecycle_operations_{lifecycle_operation_store_};

    // Event-loop-owned controller state
    Config config_;
    std::string config_path_;
    DaemonOptions opts_;

    // Subsystems
    std::unique_ptr<Firewall> firewall_;
    std::unique_ptr<InterfaceMonitor> interface_monitor_;
    std::optional<int> interface_monitor_fd_;
    NetlinkManager netlink_;
    RouteTable route_table_;
    PolicyRuleManager policy_rules_;
    FirewallState firewall_state_;
    ConntrackManager conntrack_manager_;
    ResolverCoordinator resolver_coordinator_;
    std::optional<ResolverGenerationSnapshot> resolver_generation_snapshot_;
    RuntimeStateMachine runtime_state_machine_;
    URLTester url_tester_;
    OutboundMarkMap outbound_marks_;
    std::unique_ptr<Scheduler> scheduler_;
    std::unique_ptr<UrltestManager> urltest_manager_;
    BlockingExecutor blocking_executor_{2, 64};
    // Resolver hooks can synchronously call back into resolver config streaming,
    // so hook execution and resolver I/O must never share a worker.
    BlockingExecutor resolver_hook_executor_{1, 16};
    BlockingExecutor resolver_stream_executor_{1, 16};
    BlockingExecutor resolver_io_executor_{1, 32};
    BlockingExecutor lifecycle_executor_{1, 16};
    std::atomic<std::uint64_t> runtime_generation_{1};
    std::atomic<bool> remote_list_refresh_inflight_{false};
    std::atomic<bool> ipc_mutation_inflight_{false};
    std::atomic<bool> ipc_resolver_hook_inflight_{false};
    std::atomic<bool> resolver_hash_refresh_inflight_{false};
    std::atomic<std::uint64_t> resolver_stream_completed_{0};
    TracedMutex system_resolver_hook_mutex_;

#ifdef WITH_API
    std::unique_ptr<ApiServer> api_server_;
    std::unique_ptr<ApiContext> api_ctx_;
    std::unique_ptr<SseBroadcaster> dns_test_broadcaster_;
    std::unique_ptr<StatusStream> status_stream_;
#endif

    std::unique_ptr<DnsProbeServer> dns_probe_server_;
    HookCommandExecutor hook_command_executor_;
    bool routing_runtime_active_{true};
};

} // namespace keen_pbr3
