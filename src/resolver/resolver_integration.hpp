#pragma once

#include "../api/generated/api_types.hpp"
#include "../config/config.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

class ListStreamer;

// Executes an external hook command (argv) and returns its exit code.  The
// daemon injects it so tests never spawn processes.
using HookCommandExecutor = std::function<int(const std::vector<std::string>& args)>;
int default_hook_command_executor(const std::vector<std::string>& args);

// Why a reload or verification is requested.  Only used for log context.
struct ResolverReloadRequest {
    std::string reason;
};

struct ResolverReloadResult {
    bool ok{true};
    // false when there was nothing to do (integration disabled, or no system
    // resolver configured).
    bool performed{false};
    std::string error;
};

using ResolverCompletion = std::function<void(ResolverReloadResult)>;

// Point-in-time resolver state exposed through the health API.  With the
// integration disabled the fields stay at their defaults and probe_status is
// `disabled`.
struct ResolverHealthReport {
    api::ResolverIntegration mode{api::ResolverIntegration::NONE};
    std::string expected_hash;
    std::string actual_hash;
    std::optional<std::int64_t> actual_ts;
    std::optional<api::ResolverConfigSyncState> sync_state;
    api::ResolverConfigProbeStatus probe_status{api::ResolverConfigProbeStatus::UNKNOWN};
    api::ResolverLiveStatus live_status{api::ResolverLiveStatus::UNKNOWN};
    std::optional<std::int64_t> last_probe_ts;
    std::optional<std::int64_t> apply_started_ts;
};

// Services the daemon offers to an integration.  Everything the integration
// needs from the daemon goes through here so it never reaches into daemon
// internals (and can be exercised with a fake host in tests).
struct ResolverIntegrationHost {
    // Defer a task to the control (event-loop) thread; never runs inline.
    std::function<bool(std::function<void()>, const std::string& label)> post_control_task;
    std::function<bool()> on_control_thread;
    // Serve pending control-socket requests while the control thread is
    // blocked (the resolver streams its configuration through that socket).
    std::function<void()> pump_control_socket;
    std::function<std::uint64_t()> runtime_generation;
    std::function<bool()> routing_runtime_active;
    std::function<int(std::chrono::milliseconds, std::function<void()>, std::string label)>
        schedule_repeating;
    std::function<int(std::chrono::milliseconds, std::function<void()>, std::string label)>
        schedule_oneshot;
    std::function<void(int task_id)> cancel_task;
    // The integration's health changed; the daemon republishes it.
    std::function<void()> publish_state;
    // Recompute the daemon-side generation snapshot (ipv6 / interception
    // decision) the resolver output is generated from.
    std::function<void()> refresh_generation_snapshot;
};

// Optional integration between the daemon and an external system resolver.
// The daemon core only talks to this interface; with mode `none` every
// operation is a no-op and no resolver code path is entered.
class ResolverIntegration {
public:
    virtual ~ResolverIntegration() = default;

    virtual api::ResolverIntegration mode() const = 0;
    // True when the integration does anything at all.  Callers use it to
    // decide whether resolver lifecycle stages exist.
    virtual bool enabled() const = 0;
    // True when enabled and a resolver endpoint is configured, i.e. reload()
    // actually runs the hook.
    virtual bool active() const = 0;
    // Pick up the settings of a new configuration (control thread).
    virtual void configure(const Config& config) = 0;

    // --- runtime lifecycle (control thread) ---
    // Refresh inputs the firewall plan depends on (Keenetic DNS addresses).
    virtual void prepare_runtime() = 0;
    // A new resolver generation is about to be reloaded.
    virtual void apply_started(std::int64_t ts) = 0;
    // The runtime reached the running state.
    virtual void runtime_running() = 0;
    // Routing/firewall teardown begins: stop periodic resolver work.
    virtual void runtime_stopping() = 0;
    // Re-evaluate and publish the resolver health asynchronously.
    virtual void refresh_health_async() = 0;

    // --- reload / verification ---
    // Make the resolver load the current configuration and wait until it did.
    // Blocking; callable from any thread (on the control thread the control
    // socket keeps being served).
    virtual ResolverReloadResult reload(const ResolverReloadRequest& request) = 0;
    // Confirm that the resolver serves the expected configuration (blocking).
    virtual ResolverReloadResult verify(const ResolverReloadRequest& request) = 0;
    // Same as above on the integration's own worker; `done` runs on that
    // worker (or inline when nothing is queued).
    virtual void reload_async(const ResolverReloadRequest& request,
                              ResolverCompletion done) = 0;
    virtual void verify_async(const ResolverReloadRequest& request,
                              ResolverCompletion done) = 0;
    // The daemon stops (stop lifecycle, shutdown): put the resolver on its
    // static fallback configuration but keep the integration installed so the
    // resolver picks the managed configuration up again on the next start.
    virtual bool fallback() = 0;
    // The integration is switched off (mode change to `none`): remove the
    // daemon's hooks from the resolver so it no longer talks to the daemon.
    virtual bool deactivate() = 0;
    // Control thread only: keep serving the control socket for a short while
    // after deactivate() so a late resolver callback is answered.
    virtual void drain_callbacks(std::chrono::milliseconds duration) = 0;

    // --- control socket ---
    // A reload/deactivate hook is currently running.
    virtual bool hook_in_flight() const = 0;
    // The resolver finished streaming the configuration with this hash.
    virtual bool accept_generated_config(const std::string& hash) = 0;

    virtual ResolverHealthReport health() const = 0;
    // Stop workers; no further callbacks are delivered.
    virtual void shutdown() = 0;
};

// Integration used when dns.resolver_integration is `none`.
class NoResolverIntegration final : public ResolverIntegration {
public:
    api::ResolverIntegration mode() const override { return api::ResolverIntegration::NONE; }
    bool enabled() const override { return false; }
    bool active() const override { return false; }
    void configure(const Config&) override {}
    void prepare_runtime() override {}
    void apply_started(std::int64_t) override {}
    void runtime_running() override {}
    void runtime_stopping() override {}
    void refresh_health_async() override {}
    ResolverReloadResult reload(const ResolverReloadRequest&) override { return {}; }
    ResolverReloadResult verify(const ResolverReloadRequest&) override { return {}; }
    void reload_async(const ResolverReloadRequest&, ResolverCompletion done) override {
        if (done) done({});
    }
    void verify_async(const ResolverReloadRequest&, ResolverCompletion done) override {
        if (done) done({});
    }
    bool fallback() override { return true; }
    bool deactivate() override { return true; }
    void drain_callbacks(std::chrono::milliseconds) override {}
    bool hook_in_flight() const override { return false; }
    bool accept_generated_config(const std::string&) override { return false; }
    ResolverHealthReport health() const override;
    void shutdown() override {}
};

struct ResolverIntegrationDeps {
    ResolverIntegrationHost host;
    HookCommandExecutor hook_executor{default_hook_command_executor};
};

using ResolverIntegrationFactory = std::function<std::unique_ptr<ResolverIntegration>(
    api::ResolverIntegration mode, const ResolverIntegrationDeps& deps)>;

// Creates the integration for `mode`: NoResolverIntegration for `none`,
// DnsmasqIntegration for `dnsmasq`.
std::unique_ptr<ResolverIntegration> make_resolver_integration(
    api::ResolverIntegration mode, const ResolverIntegrationDeps& deps);

// Brings `current` in line with `config` (control thread).  A mode change
// deactivates and shuts down the old integration before the new one takes
// over; an unchanged mode only hands the new settings to the existing
// instance.  Returns true when the instance was replaced.
bool reconfigure_resolver_integration(std::unique_ptr<ResolverIntegration>& current,
                                      const Config& config,
                                      const ResolverIntegrationDeps& deps,
                                      const ResolverIntegrationFactory& factory =
                                          make_resolver_integration);

} // namespace keen_pbr3

namespace keen_pbr3 {

// Dry-run of the resolver output for `config`: throws when the configuration
// cannot be rendered.  A no-op unless the effective mode needs a resolver
// configuration (dnsmasq).
void dry_run_resolver_generation(const Config& config,
                                 ListStreamer& streamer,
                                 bool ipv6_enabled);

} // namespace keen_pbr3
