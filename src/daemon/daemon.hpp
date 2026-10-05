#pragma once

#include "../config/config.hpp"
#include "../firewall/firewall.hpp"
#include "../health/routing_health.hpp"
#include "../health/url_tester.hpp"
#include "../intercept/intercept_capabilities.hpp"
#include "../intercept/intercept_service.hpp"
#include "../intercept/intercept_settings.hpp"
#include "../health/icmp_tester.hpp"
#include "../routing/balance_classifier_state.hpp"
#include "../routing/firewall_state.hpp"
#include "../routing/interface_monitor.hpp"
#include "../routing/netlink.hpp"
#include "../routing/policy_rule.hpp"
#include "../routing/route_table.hpp"
#include "../runtime/conntrack_manager.hpp"
#include "../runtime/lifecycle_operation.hpp"
#include "../runtime/operation_coordinator.hpp"
#include "../intercept/rebind_backoff.hpp"
#include "../runtime/runtime_state_machine.hpp"
#include "../util/blocking_executor.hpp"
#include "../util/traced_mutex.hpp"
#include "config_store.hpp"
#include "dnsmasq_manager.hpp"
#include "list_service.hpp"
#include "pid_file.hpp"
#include "runtime_state_store.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
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

namespace keen_pbr3 {

class Firewall;
class Scheduler;
class UrltestManager;
class IcmpTester;

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
  // Opt-in Keenetic workaround: classify the selected family's forwarded
  // packets in raw PREROUTING. Local OUTPUT traffic remains in mangle.
  bool use_raw_prerouting{false};
  // Opt-in IPv6 equivalent; independent from use_raw_prerouting.
  bool use_raw6_prerouting{false};
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

// A URLTEST switch can commit its routing change while the targeted conntrack
// cleanup fails transiently.  Keep the cleanup tied to the selection that was
// actually applied so a later unchanged probe can retry it without repeating
// the routing reconcile.
struct PendingUrltestConntrackCleanup {
  std::string selected_child;
  uint32_t mark{0};
  uint32_t mark_mask{0};
};

// Per-runtime-generation decisions the control socket reports.
struct ControlGenerationSnapshot {
  bool ipv6_enabled{true};
  // The daemon fills the dynamic sets (DNS hold active).
  bool intercept_dns_hold{false};
  std::uint64_t generation{0};
};

enum class StatusPublishScope {
  ServiceAndOutbounds,
  Outbounds,
  OutboundsAndInterfaces,
};

// Helper to get tag from any outbound variant
std::string get_outbound_tag(const Outbound &ob);

// Find an outbound by tag, returning pointer or nullptr
const Outbound *find_outbound(const std::vector<Outbound> &outbounds,
                              const std::string &tag);

// Epoll-based daemon that owns all runtime subsystems.
// Handles signal dispatch, routing, firewall, urltest, and API lifecycle.
class Daemon {
public:
  Daemon(Config config, std::string config_path, DaemonOptions opts);
  ~Daemon();

  // Non-copyable, non-movable
  Daemon(const Daemon &) = delete;
  Daemon &operator=(const Daemon &) = delete;
  Daemon(Daemon &&) = delete;
  Daemon &operator=(Daemon &&) = delete;

  // Register an additional file descriptor for epoll monitoring.
  void add_fd(int fd, uint32_t events, FdCallback cb,
              bool wait_for_completion = true, const std::string &label = "");

  // Remove a previously registered file descriptor.
  void remove_fd(int fd, bool wait_for_completion = true,
                 const std::string &label = "");

  // Serialize execution of control operations in event loop.
  void enqueue_control_task(std::function<void()> task,
                            bool wait_for_completion = false,
                            const std::string &label = "");

  // Backward-compatible alias for enqueue_control_task.
  void enqueue_control_command(std::function<void()> command,
                               bool wait_for_completion = false,
                               const std::string &label = "");

  // Post a task to the event loop, always deferred to the next iteration.
  // Unlike enqueue_control_task, never executes inline even when called from
  // the event loop thread. Safe to call while holding any lock — the posted
  // task only runs after the current event-loop iteration completes and all
  // caller locks have been released. Use this for callbacks that must not
  // run re-entrantly inside the current controller action.
  bool post_control_task(std::function<void()> task,
                         const std::string &label = "");

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
  void run_ipc_control_acceptor() noexcept;
  void handle_ipc_control_socket();
  bool try_begin_routing_test();
  void finish_routing_test();
  RoutingHealthReport cached_routing_health();
  void invalidate_routing_health_cache();
  bool is_routing_health_cache_valid(const RuntimeStateSnapshot& snapshot) const
      REQUIRES(routing_health_mutex_);
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
  void handle_interface_event(const InterfaceMonitor::Event &event);
  bool is_interface_outbound_in_use(const std::string &interface_name) const;
  bool is_auto_gateway_outbound_in_use(const std::string &interface_name = "") const;
  void refresh_iproute_and_firewall_runtime(
      StatusPublishScope scope = StatusPublishScope::ServiceAndOutbounds);
  void dispatch_event_fd(int fd, uint32_t events);
  void run_event_loop();
  void begin_startup_runtime();
  void continue_startup_after_lists(
      std::optional<RemoteListsRefreshResult> refresh_result,
      std::string error);
  void fail_startup_runtime(std::string error);
  // The one place kernel/system capabilities are measured (service start):
  // required tools, the shared KernelCapabilities snapshot, the interception
  // probe, conntrack filter and NFQUEUE GSO.  Later code only reads the results.
  // Throws DaemonError when a tool the selected backend needs is missing.
  void probe_capabilities_at_start();
  // Rejects a balance config the active firewall backend cannot realize
  // (iptables without xt_statistic) before anything is mutated.
  void require_balance_support(const Config &config) const;

  // lifecycle and runtime apply
  void setup_static_routing(const std::vector<DumpedRoute>* main_routes = nullptr);
  void reconcile_static_routing(
      const std::map<std::string, std::string> *urltest_selections = nullptr,
      const std::vector<DumpedRoute>* main_routes = nullptr);
  FirewallApplyMode runtime_refresh_firewall_mode() const;
  FirewallBalanceCandidates build_balance_candidates(
      const std::vector<DumpedRoute>& main_routes,
      const std::vector<DumpedInterface>& interfaces);
  void apply_firewall(FirewallApplyMode mode = FirewallApplyMode::Destructive,
                      bool force_clear_dynamic_sets = false,
                      const std::vector<DumpedRoute>* main_routes = nullptr,
                      const Config* quiesce_config = nullptr,
                      const OutboundMarkMap* quiesce_marks = nullptr);
  void reconcile_lists_only();

  // Traffic interception (DNS hold / L7 sniff), see daemon_intercept.cpp.
  InterceptEffective resolve_intercept_effective();
  void start_intercept_service(InterceptEffective &effective);
  // Folds what `service`'s listener binds revealed (nfqueue, fail-open,
  // payload replacement, nflog) into `effective` and the capability cache.
  void fold_intercept_listener_probe(InterceptEffective &effective,
                                     const InterceptService &service);
  // Applies the firewall without interception rules, then stops the service.
  void quiesce_intercept_service(
      const std::vector<DumpedRoute> &main_routes,
      const std::vector<DumpedInterface> &interfaces,
      const FirewallBalanceCandidates &balance_candidates,
      const Config &quiesce_config,
      const OutboundMarkMap &quiesce_marks);
  void stop_intercept_service();
  // Called inside the firewall-apply write pause: republishes the previous
  // domain index with the new set bindings, then builds the new index in the
  // background (retrying with backoff when that fails).
  void schedule_intercept_snapshot_update(
      std::vector<FirewallSetDeclaration> sets,
      const InterceptEffective &effective);
  struct InterceptSnapshotJob;
  void queue_intercept_snapshot_build(
      std::shared_ptr<const InterceptSnapshotJob> job, unsigned attempt);
  void retry_intercept_snapshot_build(
      std::shared_ptr<const InterceptSnapshotJob> job, unsigned attempt);
  InterceptEffective intercept_effective_snapshot() const;
  api::InterceptHealthClass build_intercept_health() const;
  void pump_intercept_events();
  // Control thread: notices a dead NFQUEUE/NFLOG listener and re-binds it
  // through apply_firewall() with backoff (see rebind_backoff.hpp).
  void tick_intercept_rebind();
  void publish_intercept_rebind_status();
  void register_urltest_outbounds();
  void handle_urltest_selection_change(const std::string &urltest_tag,
                                       const std::string &new_child_tag);
  bool commit_urltest_probe_results(
      const std::string &urltest_tag, std::uint64_t probe_generation,
      std::map<std::string, URLTestResult> results, TraceId trace_id);
  void apply_config(Config config, bool refresh_remote_lists = true);
  // Candidate application may mutate kernel state while keeping the
  // externally visible active snapshot unchanged until its transaction commits.
  void apply_prepared_runtime_inputs(PreparedRuntimeInputs prepared,
                                     bool publish_active_snapshot = true,
                                     bool defer_dnsmasq_sync = false);
  PreparedRuntimeInputs
  prepare_runtime_inputs(const Config &config,
                         bool refresh_remote_lists = true);
  void reload_from_disk();
  void teardown_routing_and_firewall(bool explicit_stop);
  void setup_routing_and_firewall();
  void reconcile_prepared_runtime(PreparedRuntimeInputs prepared);
  void complete_running_runtime(const char *reason, bool defer_dnsmasq_sync = false);
  void start_routing_runtime();
  void stop_routing_runtime();
  void restart_routing_runtime();
  bool routing_runtime_active() const;
  void transition_runtime_or_throw(RuntimeState next, const char *reason);
  // Marks the start of a runtime apply and refreshes the control snapshot.
  void begin_runtime_generation();
  void schedule_lists_autoupdate();
  // Queues a (coalesced) regeneration of the dnsmasq config for the current
  // configuration on a blocking worker.
  // `explicit_apply` (lifecycle operations) installs it even if dnsmasq looks
  // unchanged and refills the repair budget; `reason` is logged.
  void schedule_dnsmasq_sync(std::string reason = "lists updated",
                             bool explicit_apply = false);
  // Starts the repeating dnsmasq TXT check (replacing a previous one) /
  // cancels it.  Control-thread only.
  void start_dnsmasq_check();
  void stop_dnsmasq_check();
  ListsRefreshExecutionResult execute_remote_list_refresh(
      const std::set<std::string> *target_lists = nullptr,
      std::string_view source = "service");
  void refresh_lists_and_maybe_reload();
  void refresh_lists_and_maybe_reload_async();
  void commit_lists_refresh_async_result(
      Config config_snapshot, bool runtime_active_snapshot,
      std::uint64_t generation,
      std::optional<RemoteListsRefreshResult> refresh_result, std::string error,
      TraceId trace_id);

  // PID file management
  void write_pid_file();
  void remove_pid_file();

  // state publication

#ifdef WITH_API
  // API integration
  void setup_api();
  void finish_config_operation();
  void begin_config_operation_or_throw(ConfigOperationState state,
                                       const char *reason,
                                       bool require_runtime_running,
                                       bool require_runtime_stopped);
  ConfigApplyResult apply_validated_config_via_control_task(
      Config config, std::string saved_config_json, bool persist_config = true);
  std::string submit_lifecycle_operation(LifecycleRequest request);
  void execute_lifecycle_operation(std::string operation_id,
                                   LifecycleRequest request);
  void run_runtime_control_operation_or_throw(const std::string &label,
                                              const char *operation_name,
                                              std::function<void()> task);
  ListRefreshOperationResult
  refresh_lists_via_api(std::optional<std::string> requested_name);
#endif

  // Recompute the per-generation control snapshot from the current config.
  void refresh_generation_snapshot();
  ControlGenerationSnapshot make_generation_snapshot();
  RuntimeStateSnapshot build_runtime_state_snapshot() const;
  void publish_runtime_state(
      StatusPublishScope scope = StatusPublishScope::ServiceAndOutbounds);
  void publish_urltest_runtime_state(const std::string &tag);

  // Lists autoupdate state
  int lists_autoupdate_task_id_{-1};
  // Debounced runtime refresh triggered by SIGUSR1.
  int sigusr1_refresh_task_id_{-1};
  // Retry task for interface monitor netlink reconnect after failure.
  int interface_monitor_reconnect_task_id_{-1};
  // Debounced runtime refresh triggered by interface events.
  int interface_refresh_task_id_{-1};
  bool interface_refresh_pending_{false};
  std::chrono::steady_clock::time_point interface_refresh_quiet_until_{};

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
  std::atomic<bool> ipc_accept_running_{false};
  std::thread ipc_accept_thread_;
  struct IpcControlRequest {
    int fd{-1};
    std::uint32_t peer_uid{0};
    nlohmann::json request;
  };
  TracedMutex ipc_accepted_clients_mutex_;
  std::deque<IpcControlRequest> ipc_accepted_clients_
      GUARDED_BY(ipc_accepted_clients_mutex_);
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
  std::atomic<ConfigOperationState> config_op_state_{
      static_cast<ConfigOperationState>(0)};
#endif

  // Snapshot stores
  ConfigStore config_store_;
  ListService list_service_;
  RuntimeStateStore runtime_state_store_;
  LifecycleOperationStore lifecycle_operation_store_;
  LifecycleOperationCoordinator lifecycle_operations_{
      lifecycle_operation_store_};

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
  std::optional<ControlGenerationSnapshot> generation_snapshot_;
  RuntimeStateMachine runtime_state_machine_;
URLTester url_tester_;
IcmpTester icmp_tester_;
  OutboundMarkMap outbound_marks_;
  std::unique_ptr<Scheduler> scheduler_;
  std::unique_ptr<UrltestManager> urltest_manager_;
  // Event-loop-owned state. Entries are valid only for the current runtime
  // generation and are cleared whenever the configured groups are rebuilt.
  std::map<std::string, PendingUrltestConntrackCleanup>
      pending_urltest_conntrack_cleanup_;
  // Last successfully applied balance classifier inputs per test group, so
  // probe cycles that change nothing skip the firewall rebuild. Cleared
  // whenever the configured groups are rebuilt.
  std::map<std::string, BalanceClassifierState> balance_classifier_cache_;
  // Declared before the executor: queued sync jobs reference it, so the
  // executor must be torn down first.
  DnsmasqManager dnsmasq_manager_;
  BlockingExecutor blocking_executor_{2, 64};
  // Interception service and its resolved settings.  The service pointer and
  // the effective settings are read by API threads; everything else is owned
  // by the control/event-loop thread.
  mutable TracedMutex intercept_mutex_;
  std::shared_ptr<InterceptService> intercept_service_
      GUARDED_BY(intercept_mutex_);
  InterceptEffective intercept_effective_ GUARDED_BY(intercept_mutex_);
  // What /health reports about an automatic listener re-bind in progress.
  struct InterceptRebindStatus {
    bool active{false};
    unsigned attempts{0};
    std::chrono::steady_clock::time_point next_due{};
  };
  InterceptRebindStatus intercept_rebind_status_ GUARDED_BY(intercept_mutex_);
  // Control thread only.  The listeners that were wanted when they failed:
  // a failed re-bind disables them in the effective settings, but they stay
  // wanted until an explicit apply takes over.
  RebindBackoff intercept_rebind_;
  bool intercept_rebind_want_dns_{false};
  bool intercept_rebind_want_l7_{false};
  bool intercept_rebind_in_progress_{false};
  int intercept_rebind_task_id_{-1};
  InterceptServiceOptions intercept_service_options_;
  // Interception capabilities measured once at service start (immutable
  // afterwards); resolve_intercept_effective only reads them.
  std::optional<InterceptStartupProbe> intercept_startup_probe_;
  // What the listener binds revealed (queue/group bind, fail-open, payload
  // replacement).  Not a kernel capability: a blocking result is forgotten on a
  // re-bind or config apply so the bind is retried, without any probing.
  InterceptRuntimeProbe intercept_listener_results_;
  // Shared with the nft set writers: true while the nft_timeout_update probe
  // proved the kernel extends an existing element's timeout in place.
  std::shared_ptr<std::atomic<bool>> nft_timeout_update_ =
      std::make_shared<std::atomic<bool>>(false);
  std::atomic<std::uint64_t> intercept_snapshot_seq_{0};
  std::uint64_t intercept_forwarded_seq_{0};
  int intercept_event_task_id_{-1};
  int dnsmasq_check_task_id_{-1};
  BlockingExecutor lifecycle_executor_{1, 16};
  // An open descriptor pins the pre-apply inode without retaining another
  // parsed or serialized configuration in RAM.
  int rollback_config_fd_{-1};
  std::atomic<bool> rollback_available_{false};
  // Routing diagnostics are CPU/process-heavy. Two workers allow API and CLI
  // tests to overlap while the small queue keeps resource use bounded.
  BlockingExecutor routing_test_executor_{2, 2};
  // Control status must not inspect the firewall on the event-loop thread.
  // A single coalesced job refreshes this short-lived, generation-tagged
  // report; callers never treat a report from an older runtime as current.
  mutable TracedMutex routing_health_mutex_;
  std::optional<RoutingHealthReport> routing_health_cache_
      GUARDED_BY(routing_health_mutex_);
  std::uint64_t routing_health_cache_revision_
      GUARDED_BY(routing_health_mutex_){0};
  std::uint64_t routing_health_cache_generation_
      GUARDED_BY(routing_health_mutex_){0};
  RuntimeState routing_health_cache_state_
      GUARDED_BY(routing_health_mutex_){RuntimeState::starting};
  std::chrono::steady_clock::time_point routing_health_cache_time_
      GUARDED_BY(routing_health_mutex_){};
  bool routing_health_check_inflight_ GUARDED_BY(routing_health_mutex_){false};
  std::atomic<std::uint64_t> routing_health_revision_{1};
  std::atomic<std::uint64_t> runtime_generation_{1};
  std::atomic<bool> remote_list_refresh_inflight_{false};
  std::atomic<bool> ipc_mutation_inflight_{false};
  std::atomic<std::size_t> routing_tests_inflight_{0};

#ifdef WITH_API
  std::unique_ptr<ApiServer> api_server_;
  std::unique_ptr<ApiContext> api_ctx_;
  std::unique_ptr<SseBroadcaster> dns_test_broadcaster_;
  std::unique_ptr<StatusStream> status_stream_;
#endif

  bool routing_runtime_active_{true};
  // Unix seconds of the last runtime apply start; 0 when none happened yet.
  std::atomic<std::int64_t> apply_started_ts_{0};
};

// Maps a runtime lifecycle reason to the wording used when dnsmasq is
// restarted because of it.
std::string dnsmasq_apply_reason(std::string_view lifecycle_reason);

} // namespace keen_pbr3
