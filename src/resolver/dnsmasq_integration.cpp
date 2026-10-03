#include "dnsmasq_integration.hpp"

#include "../daemon/resolver_apply_confirmation.hpp"
#include "../daemon/resolver_stream_wait.hpp"
#include "../daemon/system_resolver_hook.hpp"
#include "../dns/keenetic_dns.hpp"
#include "../log/logger.hpp"
#include "../util/time_utils.hpp"

#include <fmt/ranges.h>

#include <exception>
#include <thread>

namespace keen_pbr3 {

namespace {

constexpr auto kHashActualRefreshInterval = std::chrono::seconds{5};
constexpr auto kApplyProbeTimeout = std::chrono::seconds{30};
constexpr auto kApplyProbeInterval = std::chrono::seconds{1};
constexpr auto kKeeneticDnsRefreshInterval = std::chrono::minutes{5};

bool dns_config_uses_keenetic_server(const std::optional<DnsConfig>& dns_cfg) {
    if (!dns_cfg.has_value()) return false;
    for (const auto& server : dns_cfg->servers.value_or(std::vector<DnsServer>{})) {
        if (server.type.value_or(api::DnsServerType::STATIC) == api::DnsServerType::KEENETIC) {
            return true;
        }
    }
    return false;
}

} // namespace

DnsmasqIntegration::DnsmasqIntegration(ResolverIntegrationHost host,
                                       HookCommandExecutor hook_executor)
    : host_(std::move(host)), hook_executor_(std::move(hook_executor)) {}

DnsmasqIntegration::~DnsmasqIntegration() {
    shutdown();
}

void DnsmasqIntegration::shutdown() {
    if (shut_down_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    lifetime_->store(false, std::memory_order_release);
    cancel_keenetic_dns_refresh();
    cancel_hash_actual_tasks();
    hook_worker_.shutdown();
    io_worker_.shutdown();
}

DnsmasqIntegration::Settings DnsmasqIntegration::settings() const {
    KPBR_LOCK_GUARD(settings_mutex_);
    return settings_;
}

bool DnsmasqIntegration::active() const {
    return settings().resolver_configured;
}

void DnsmasqIntegration::configure(const Config& config) {
    Settings next;
    next.resolver_configured = config.dns.has_value() &&
                               config.dns->system_resolver.has_value() &&
                               !config.dns->system_resolver->address.empty();
    if (next.resolver_configured) {
        next.address = config.dns->system_resolver->address;
    }
    next.ready_timeout = resolver_ready_timeout(config);
    next.uses_keenetic_server = dns_config_uses_keenetic_server(config.dns);
    KPBR_LOCK_GUARD(settings_mutex_);
    settings_ = std::move(next);
}

// ---------------------------------------------------------------------------
// hook execution
// ---------------------------------------------------------------------------

bool DnsmasqIntegration::run_hook(std::string_view action) {
    auto& log = Logger::instance();

    const bool resolver_configured =
        settings().resolver_configured || hook_activated_.load(std::memory_order_acquire);
    const auto args = build_system_resolver_hook_args(resolver_configured, action);
    if (args.empty()) {
        return true;
    }

    std::string command;
    for (std::size_t index = 0; index < args.size(); ++index) {
        if (index != 0) command += ' ';
        command += args[index];
    }

    auto execute_hook = [this, args] {
        KPBR_LOCK_GUARD(hook_mutex_);
        hook_in_flight_.store(true, std::memory_order_release);
        try {
            const int result = hook_executor_(args);
            hook_in_flight_.store(false, std::memory_order_release);
            return result;
        } catch (...) {
            hook_in_flight_.store(false, std::memory_order_release);
            throw;
        }
    };

    int exit_code = 0;
    if (host_.on_control_thread && host_.on_control_thread()) {
        // Some runtime reconfiguration paths originate on the control
        // thread.  Run the external hook on the bounded worker and service
        // only the resolver stream while waiting.  This avoids both deadlock
        // and an extra thread/stack.
        auto hook_result = hook_worker_.submit("system-resolver-hook-command",
                                               std::move(execute_hook));
        while (hook_result.wait_for(std::chrono::milliseconds{10}) !=
               std::future_status::ready) {
            if (host_.pump_control_socket) host_.pump_control_socket();
        }
        exit_code = hook_result.get();
    } else {
        exit_code = execute_hook();
    }

    if (exit_code != 0) {
        log.warn("System resolver {} hook failed (exit code: {}): {}",
                 action, exit_code, command);
        return false;
    }

    log.info("System resolver hook complete: {}", command);
    return true;
}

bool DnsmasqIntegration::wait_for_stream_after(std::uint64_t baseline,
                                               std::chrono::seconds timeout) {
    return keen_pbr3::wait_for_resolver_stream_after(
        baseline, timeout,
        [this] { return stream_completed_.load(std::memory_order_acquire); },
        [this] {
            if (host_.on_control_thread && host_.on_control_thread() &&
                host_.pump_control_socket) {
                host_.pump_control_socket();
            }
        },
        wait_for_resolver_stream_poll,
        resolver_stream_now);
}

ResolverReloadResult DnsmasqIntegration::reload(const ResolverReloadRequest& request) {
    ResolverReloadResult result;
    const Settings current = settings();
    if (!current.resolver_configured) {
        return result;
    }
    result.performed = true;
    Logger::instance().verbose("Reloading system resolver ({})", request.reason);

    const auto baseline = stream_completed_.load(std::memory_order_acquire);
    // The first reload of a daemon run (or after a deactivation) installs the
    // conf-script hook; `activate` also restarts dnsmasq so it runs.
    const bool activating = !hook_activated_.load(std::memory_order_acquire);
    if (!run_hook(activating ? "activate" : "reload")) {
        result.ok = false;
        result.error = "system resolver reload hook failed";
        return result;
    }
    if (activating) hook_activated_.store(true, std::memory_order_release);
    if (!wait_for_stream_after(baseline, current.ready_timeout)) {
        const auto stream_current = stream_completed_.load(std::memory_order_acquire);
        Logger::instance().warn(
            "Timed out after {} seconds waiting for dnsmasq resolver "
            "configuration generation to complete after reload (resolver stream "
            "completions baseline={}, current={})",
            current.ready_timeout.count(), baseline, stream_current);
        result.ok = false;
        result.error = "system resolver reload hook failed";
        return result;
    }
    // The init script may return before dnsmasq invokes its conf-script. The
    // completed-stream wait above makes this the true reload boundary.
    if (host_.refresh_generation_snapshot) host_.refresh_generation_snapshot();
    return result;
}

ResolverReloadResult DnsmasqIntegration::verify(const ResolverReloadRequest& request) {
    ResolverReloadResult result;
    const Settings current = settings();
    if (!current.resolver_configured) {
        return result;
    }
    result.performed = true;
    Logger::instance().verbose("Verifying system resolver configuration ({})", request.reason);

    std::string expected_hash;
    std::int64_t started = 0;
    {
        KPBR_LOCK_GUARD(state_mutex_);
        expected_hash = sync_.snapshot(unix_timestamp_now_seconds()).expected_hash;
        started = apply_started_ts_.value_or(0);
    }
    const std::string address = current.address;
    std::string error;
    result.ok = wait_for_resolver_hash_confirmation(
        expected_hash, started,
        std::chrono::duration_cast<std::chrono::milliseconds>(kApplyProbeTimeout),
        kApplyProbeInterval,
        [&address] {
            return query_resolver_config_hash_txt(
                address, "config-hash.keen.pbr", std::chrono::milliseconds{2000});
        },
        error);
    if (!result.ok) result.error = std::move(error);
    return result;
}

void DnsmasqIntegration::reload_async(const ResolverReloadRequest& request,
                                      ResolverCompletion done) {
    const auto lifetime = lifetime_;
    const bool queued = hook_worker_.try_post(
        "resolver-reload", [this, lifetime, request, done]() mutable {
            if (!lifetime->load(std::memory_order_acquire)) return;
            ResolverReloadResult result;
            try {
                result = reload(request);
            } catch (const std::exception& error) {
                result.ok = false;
                result.error = error.what();
            } catch (...) {
                result.ok = false;
                result.error = "unknown system resolver hook error";
            }
            if (done && lifetime->load(std::memory_order_acquire)) {
                done(std::move(result));
            }
        });
    if (!queued && done) {
        ResolverReloadResult failed;
        failed.ok = false;
        failed.error = "startup resolver hook executor is unavailable";
        done(std::move(failed));
    }
}

void DnsmasqIntegration::verify_async(const ResolverReloadRequest& request,
                                      ResolverCompletion done) {
    const auto lifetime = lifetime_;
    const bool queued = io_worker_.try_post(
        "resolver-verification", [this, lifetime, request, done]() mutable {
            if (!lifetime->load(std::memory_order_acquire)) return;
            ResolverReloadResult result;
            try {
                result = verify(request);
            } catch (const std::exception& error) {
                result.ok = false;
                result.error = error.what();
            } catch (...) {
                result.ok = false;
                result.error = "resolver confirmation failed with an unknown error";
            }
            if (done && lifetime->load(std::memory_order_acquire)) {
                done(std::move(result));
            }
        });
    if (!queued && done) {
        ResolverReloadResult failed;
        failed.ok = false;
        failed.error = "resolver verification executor is unavailable";
        done(std::move(failed));
    }
}

bool DnsmasqIntegration::fallback() {
    if (!active() && !hook_activated_.load(std::memory_order_acquire)) {
        return true;
    }
    // dnsmasq restarts and its conf-script emits the static fallback while the
    // runtime is stopped or shutting down.
    return run_hook("reload");
}

bool DnsmasqIntegration::deactivate() {
    if (!active() && !hook_activated_.load(std::memory_order_acquire)) {
        return true;
    }
    const bool removed = run_hook("deactivate");
    if (removed) hook_activated_.store(false, std::memory_order_release);
    return removed;
}

void DnsmasqIntegration::drain_callbacks(std::chrono::milliseconds duration) {
    if (!active()) return;
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
        if (host_.pump_control_socket) host_.pump_control_socket();
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
}

bool DnsmasqIntegration::hook_in_flight() const {
    return hook_in_flight_.load(std::memory_order_acquire);
}

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------

void DnsmasqIntegration::apply_started(std::int64_t ts) {
    KPBR_LOCK_GUARD(state_mutex_);
    apply_started_ts_ = ts;
}

bool DnsmasqIntegration::accept_generated_config(const std::string& hash) {
    if (hash.empty()) {
        return false;
    }
    {
        KPBR_LOCK_GUARD(state_mutex_);
        sync_.expected_hash_updated(hash);
        (void)coordinator_.reconcile(hash);
        if (apply_started_ts_.value_or(0) > 0) {
            sync_.apply_started(*apply_started_ts_, hash);
        }
    }
    stream_completed_.fetch_add(1, std::memory_order_release);
    Logger::instance().info("Resolver config hash: {}", hash);
    publish();
    return true;
}

ResolverHealthReport DnsmasqIntegration::health() const {
    ResolverSyncSnapshot snapshot;
    {
        KPBR_LOCK_GUARD(state_mutex_);
        snapshot = sync_.snapshot(unix_timestamp_now_seconds());
    }
    ResolverHealthReport report;
    report.mode = api::ResolverIntegration::DNSMASQ;
    report.expected_hash = std::move(snapshot.expected_hash);
    report.actual_hash = std::move(snapshot.actual_hash);
    report.actual_ts = snapshot.actual_ts;
    report.sync_state = snapshot.sync_state;
    report.probe_status = snapshot.probe_status;
    report.live_status = snapshot.live_status;
    report.last_probe_ts = snapshot.last_probe_ts;
    report.apply_started_ts = snapshot.apply_started_ts;
    return report;
}

void DnsmasqIntegration::publish() {
    if (host_.publish_state) host_.publish_state();
}

// ---------------------------------------------------------------------------
// runtime lifecycle
// ---------------------------------------------------------------------------

void DnsmasqIntegration::prepare_runtime() {
    (void)refresh_keenetic_dns_cache(true);
}

void DnsmasqIntegration::runtime_running() {
    schedule_keenetic_dns_refresh();
    schedule_hash_actual_refresh();
    refresh_hash_actual();
}

void DnsmasqIntegration::runtime_stopping() {
    cancel_keenetic_dns_refresh();
    cancel_hash_actual_tasks();
}

void DnsmasqIntegration::refresh_health_async() {
    refresh_hash_actual();
}

void DnsmasqIntegration::cancel_keenetic_dns_refresh() {
    if (keenetic_refresh_task_id_ >= 0) {
        if (host_.cancel_task) host_.cancel_task(keenetic_refresh_task_id_);
        keenetic_refresh_task_id_ = -1;
    }
}

void DnsmasqIntegration::cancel_hash_actual_tasks() {
    if (hash_actual_task_id_ >= 0) {
        if (host_.cancel_task) host_.cancel_task(hash_actual_task_id_);
        hash_actual_task_id_ = -1;
    }
    if (hash_actual_retry_task_id_ >= 0) {
        if (host_.cancel_task) host_.cancel_task(hash_actual_retry_task_id_);
        hash_actual_retry_task_id_ = -1;
    }
}

void DnsmasqIntegration::schedule_hash_actual_refresh() {
    if (hash_actual_task_id_ >= 0 && host_.cancel_task) {
        host_.cancel_task(hash_actual_task_id_);
    }
    if (!host_.schedule_repeating) return;
    const auto lifetime = lifetime_;
    hash_actual_task_id_ = host_.schedule_repeating(
        kHashActualRefreshInterval,
        [this, lifetime] {
            if (lifetime->load(std::memory_order_acquire)) maybe_refresh_hash_actual();
        },
        "resolver-config-hash-actual");
}

void DnsmasqIntegration::schedule_hash_actual_retry() {
    if (hash_actual_retry_task_id_ >= 0 && host_.cancel_task) {
        host_.cancel_task(hash_actual_retry_task_id_);
    }
    if (!host_.schedule_oneshot) return;
    const auto lifetime = lifetime_;
    hash_actual_retry_task_id_ = host_.schedule_oneshot(
        std::chrono::seconds{1},
        [this, lifetime] {
            if (!lifetime->load(std::memory_order_acquire)) return;
            hash_actual_retry_task_id_ = -1;
            maybe_refresh_hash_actual();
        },
        "resolver-config-hash-actual-retry");
}

void DnsmasqIntegration::schedule_keenetic_dns_refresh() {
    cancel_keenetic_dns_refresh();
    if (!settings().uses_keenetic_server || !host_.schedule_repeating) {
        return;
    }
    const auto lifetime = lifetime_;
    keenetic_refresh_task_id_ = host_.schedule_repeating(
        kKeeneticDnsRefreshInterval,
        [this, lifetime] {
            if (!lifetime->load(std::memory_order_acquire)) return;
            host_.post_control_task([this, lifetime] {
                if (!lifetime->load(std::memory_order_acquire)) return;
                if (shut_down_.load(std::memory_order_acquire) ||
                    !host_.routing_runtime_active()) {
                    return;
                }
                if (refresh_keenetic_dns_cache(true)) {
                    apply_started(unix_timestamp_now_seconds());
                    if (host_.refresh_generation_snapshot) host_.refresh_generation_snapshot();
                    (void)reload(ResolverReloadRequest{"keenetic dns refresh"});
                    refresh_hash_actual();
                    publish();
                }
            }, "keenetic-dns-refresh");
        },
        "keenetic-dns-refresh");
}

bool DnsmasqIntegration::refresh_keenetic_dns_cache(bool force_refresh) {
    if (!settings().uses_keenetic_server) {
        return false;
    }

    const KeeneticDnsRefreshResult result = refresh_keenetic_dns_address_cache(force_refresh);
    auto& log = Logger::instance();

    switch (result.status) {
    case KeeneticDnsRefreshStatus::UPDATED:
        if (!result.addresses.empty()) {
            log.info("Keenetic DNS refreshed: {}", fmt::join(result.addresses, ", "));
        }
        return true;
    case KeeneticDnsRefreshStatus::UNCHANGED:
        return false;
    case KeeneticDnsRefreshStatus::FETCH_FAILED_USED_CACHE: {
        const std::string value_suffix =
            result.addresses.size() > 1 ? "s: "
            : (result.addresses.empty() ? "" : ": ");
        log.warn("Keenetic DNS refresh failed; reusing cached value{}{}",
                 value_suffix,
                 fmt::join(result.addresses, ", "));
        if (!result.error.empty()) {
            log.warn("Keenetic DNS refresh error: {}", result.error);
        }
        return false;
    }
    case KeeneticDnsRefreshStatus::FETCH_FAILED_NO_CACHE:
        if (!result.error.empty()) {
            log.warn("Keenetic DNS refresh failed with no cached value: {}", result.error);
        }
        return false;
    }

    return false;
}

// ---------------------------------------------------------------------------
// actual-hash probing
// ---------------------------------------------------------------------------

void DnsmasqIntegration::maybe_refresh_hash_actual() {
    if (hash_refresh_inflight_.load(std::memory_order_acquire)) {
        Logger::instance().trace("resolver_hash_refresh_skip", "reason=inflight");
        return;
    }
    refresh_hash_actual();
}

void DnsmasqIntegration::refresh_hash_actual() {
    if (shut_down_.load(std::memory_order_acquire)) {
        return;
    }
    const Settings current = settings();
    if (!host_.routing_runtime_active() || !current.resolver_configured) {
        {
            KPBR_LOCK_GUARD(state_mutex_);
            if (!host_.routing_runtime_active()) {
                sync_.runtime_stopped();
            } else {
                sync_.resolver_not_configured();
                coordinator_.clear_actual();
            }
        }
        publish();
        return;
    }

    const std::string resolver_addr = current.address;
    bool expected = false;
    if (!hash_refresh_inflight_.compare_exchange_strong(expected, true,
                                                        std::memory_order_acq_rel)) {
        Logger::instance().trace("resolver_hash_refresh_skip", "reason=inflight");
        return;
    }

    const auto generation = host_.runtime_generation();
    const TraceId trace_id = ensure_trace_id();
    const auto lifetime = lifetime_;
    const bool enqueued = io_worker_.try_post(
        "resolver-config-hash-actual",
        [this, lifetime, resolver_addr, generation, trace_id]() mutable {
            if (!lifetime->load(std::memory_order_acquire)) return;
            ScopedTraceContext trace_scope(trace_id);
            std::optional<ResolverConfigHashProbeResult> probe_result;
            std::optional<std::int64_t> probe_completed_ts;

            Logger::instance().trace("resolver_hash_refresh_start",
                                     "resolver={} generation={}",
                                     resolver_addr, generation);
            try {
                probe_result = query_resolver_config_hash_txt(
                    resolver_addr, "config-hash.keen.pbr", std::chrono::milliseconds(2000));
                probe_completed_ts = unix_timestamp_now_seconds();
            } catch (const std::exception& e) {
                ResolverConfigHashProbeResult failed_result;
                failed_result.status = ResolverConfigHashProbeStatus::QUERY_FAILED;
                failed_result.error = e.what();
                probe_result = std::move(failed_result);
                probe_completed_ts = unix_timestamp_now_seconds();
            }

            commit_probe_result(resolver_addr, generation, std::move(probe_result),
                                probe_completed_ts, trace_id);
        },
        trace_id);

    if (!enqueued) {
        hash_refresh_inflight_.store(false, std::memory_order_release);
        Logger::instance().trace("resolver_hash_refresh_skip", "reason=executor_unavailable");
    }
}

void DnsmasqIntegration::commit_probe_result(
    const std::string& resolver_addr,
    std::uint64_t generation,
    std::optional<ResolverConfigHashProbeResult> probe_result,
    std::optional<std::int64_t> probe_completed_ts,
    TraceId trace_id) {
    const auto lifetime = lifetime_;
    const bool posted = host_.post_control_task(
        [this,
         lifetime,
         resolver_addr,
         generation,
         probe_result = std::move(probe_result),
         probe_completed_ts,
         trace_id]() mutable {
            if (!lifetime->load(std::memory_order_acquire)) return;
            ScopedTraceContext trace_scope_inner(trace_id);
            hash_refresh_inflight_.store(false, std::memory_order_release);

            if (generation != host_.runtime_generation()) {
                Logger::instance().trace("resolver_hash_refresh_skip",
                                         "resolver={} generation={} reason=stale_runtime",
                                         resolver_addr, generation);
                return;
            }

            ResolverSyncSnapshot resolver_snapshot;
            {
                KPBR_LOCK_GUARD(state_mutex_);
                const std::int64_t apply_started_ts = apply_started_ts_.value_or(0);
                if (probe_result.has_value() &&
                    probe_result->status == ResolverConfigHashProbeStatus::SUCCESS) {
                    sync_.probe_succeeded(probe_result->parsed_value.hash,
                                          probe_result->parsed_value.ts,
                                          probe_completed_ts);
                    Logger::instance().verbose("Resolver config hash (actual): {}",
                                               probe_result->parsed_value.hash);
                    if (probe_result->parsed_value.ts.has_value() &&
                        apply_started_ts > 0 &&
                        *probe_result->parsed_value.ts < apply_started_ts) {
                        Logger::instance().verbose(
                            "Resolver config hash TXT is older than current apply; using live actual value "
                            "(resolver={}, txt_ts={}, apply_started_ts={})",
                            resolver_addr, *probe_result->parsed_value.ts, apply_started_ts);
                    }
                } else if (probe_result.has_value()) {
                    sync_.probe_failed(probe_result->status, probe_completed_ts);
                    switch (probe_result->status) {
                    case ResolverConfigHashProbeStatus::QUERY_FAILED:
                        Logger::instance().warn(
                            "Resolver config hash TXT query failed via {}: {}; clearing actual value",
                            resolver_addr, probe_result->error);
                        break;
                    case ResolverConfigHashProbeStatus::NO_USABLE_TXT:
                        Logger::instance().warn(
                            "Resolver config hash TXT is missing via {}; clearing actual value",
                            resolver_addr);
                        break;
                    case ResolverConfigHashProbeStatus::INVALID_TXT:
                        Logger::instance().warn(
                            "Resolver config hash TXT is invalid via {}: {}; clearing actual value",
                            resolver_addr, probe_result->raw_txt.value_or("<empty>"));
                        break;
                    case ResolverConfigHashProbeStatus::SUCCESS:
                        break;
                    }
                }
                resolver_snapshot = sync_.snapshot(unix_timestamp_now_seconds());
                coordinator_.observe_actual(resolver_snapshot.actual_hash);
            }
            if (resolver_snapshot.sync_state == api::ResolverConfigSyncState::CONVERGING) {
                schedule_hash_actual_retry();
            }
            publish();
        },
        "resolver-hash-refresh-commit");
    if (!posted) {
        hash_refresh_inflight_.store(false, std::memory_order_release);
    }
}

} // namespace keen_pbr3
