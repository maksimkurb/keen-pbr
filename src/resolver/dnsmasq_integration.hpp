#pragma once

#include "resolver_integration.hpp"

#include "../daemon/resolver_sync_state_machine.hpp"
#include "../dns/dns_txt_client.hpp"
#include "../runtime/resolver_coordinator.hpp"
#include "../util/blocking_executor.hpp"
#include "../util/traced_mutex.hpp"

#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace keen_pbr3 {

// dnsmasq as system resolver.  dnsmasq asks the daemon for its configuration
// through a conf-script hook (`keen-pbr generate-resolver-config`); this class
// drives the hook script (activate/reload/deactivate), waits for the resulting
// configuration stream, verifies the loaded configuration through its TXT hash
// and keeps the Keenetic DNS addresses and the resolver sync state fresh.
class DnsmasqIntegration final : public ResolverIntegration {
public:
    DnsmasqIntegration(ResolverIntegrationHost host, HookCommandExecutor hook_executor);
    ~DnsmasqIntegration() override;

    api::ResolverIntegration mode() const override { return api::ResolverIntegration::DNSMASQ; }
    bool enabled() const override { return true; }
    bool active() const override;
    void configure(const Config& config) override;

    void prepare_runtime() override;
    void apply_started(std::int64_t ts) override;
    void runtime_running() override;
    void runtime_stopping() override;
    void refresh_health_async() override;

    ResolverReloadResult reload(const ResolverReloadRequest& request) override;
    ResolverReloadResult verify(const ResolverReloadRequest& request) override;
    void reload_async(const ResolverReloadRequest& request, ResolverCompletion done) override;
    void verify_async(const ResolverReloadRequest& request, ResolverCompletion done) override;
    bool fallback() override;
    bool deactivate() override;
    void drain_callbacks(std::chrono::milliseconds duration) override;

    bool hook_in_flight() const override;
    bool accept_generated_config(const std::string& hash) override;

    ResolverHealthReport health() const override;
    void shutdown() override;

private:
    struct Settings {
        bool resolver_configured{false};
        std::string address;
        std::chrono::seconds ready_timeout{kDefaultResolverReadyTimeoutSeconds};
        bool uses_keenetic_server{false};
    };

    Settings settings() const;
    bool run_hook(std::string_view action);
    bool wait_for_stream_after(std::uint64_t baseline, std::chrono::seconds timeout);

    bool refresh_keenetic_dns_cache(bool force_refresh);
    void schedule_keenetic_dns_refresh();
    void cancel_keenetic_dns_refresh();

    void schedule_hash_actual_refresh();
    void schedule_hash_actual_retry();
    void cancel_hash_actual_tasks();
    void maybe_refresh_hash_actual();
    void refresh_hash_actual();
    void commit_probe_result(const std::string& resolver_addr,
                             std::uint64_t generation,
                             std::optional<ResolverConfigHashProbeResult> probe_result,
                             std::optional<std::int64_t> probe_completed_ts,
                             TraceId trace_id);
    void publish();

    ResolverIntegrationHost host_;
    HookCommandExecutor hook_executor_;

    mutable TracedMutex settings_mutex_;
    Settings settings_ GUARDED_BY(settings_mutex_);

    mutable TracedMutex state_mutex_;
    ResolverSyncStateMachine sync_ GUARDED_BY(state_mutex_);
    ResolverCoordinator coordinator_ GUARDED_BY(state_mutex_);
    std::optional<std::int64_t> apply_started_ts_ GUARDED_BY(state_mutex_);

    // Tasks are created and cancelled on the control thread only.
    int keenetic_refresh_task_id_{-1};
    int hash_actual_task_id_{-1};
    int hash_actual_retry_task_id_{-1};

    std::atomic<bool> hook_in_flight_{false};
    std::atomic<bool> hash_refresh_inflight_{false};
    std::atomic<bool> shut_down_{false};
    // Posted scheduler/control callbacks can outlive the integration object.
    // Keep their cancellation guard independent of `this` so a late callback
    // returns before dereferencing a destroyed integration.
    std::shared_ptr<std::atomic<bool>> lifetime_ =
        std::make_shared<std::atomic<bool>>(true);
    // The hook script installs the conf-script on `activate`; later reloads
    // only restart dnsmasq.
    std::atomic<bool> hook_activated_{false};
    std::atomic<std::uint64_t> stream_completed_{0};
    TracedMutex hook_mutex_;
    // Resolver hooks can synchronously call back into resolver config
    // streaming, so hook execution and resolver I/O must never share a worker.
    BlockingExecutor hook_worker_{1, 16};
    BlockingExecutor io_worker_{1, 32};
};

} // namespace keen_pbr3
