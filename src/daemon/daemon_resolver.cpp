#include "daemon.hpp"

#include "../log/logger.hpp"
#ifdef WITH_API
#include "../api/status_stream.hpp"
#endif
#include "../util/ipv6_support.hpp"
#include "../util/time_utils.hpp"
#include "scheduler.hpp"

// Daemon glue around the optional resolver integration (src/resolver/).  The
// daemon core only talks to the ResolverIntegration interface; everything
// dnsmasq specific lives behind it.

namespace keen_pbr3 {

ResolverIntegrationHost Daemon::make_resolver_host() {
    ResolverIntegrationHost host;
    host.post_control_task = [this](std::function<void()> task, const std::string& label) {
        return post_control_task(std::move(task), label);
    };
    host.on_control_thread = [this] { return is_event_loop_thread(); };
    host.pump_control_socket = [this] { handle_ipc_control_socket(); };
    host.runtime_generation = [this] {
        return runtime_generation_.load(std::memory_order_acquire);
    };
    host.routing_runtime_active = [this] { return routing_runtime_active_; };
    host.schedule_repeating = [this](std::chrono::milliseconds interval,
                                     std::function<void()> task, std::string label) {
        return scheduler_->schedule_repeating(interval, std::move(task), std::move(label));
    };
    host.schedule_oneshot = [this](std::chrono::milliseconds delay,
                                   std::function<void()> task, std::string label) {
        return scheduler_->schedule_oneshot(delay, std::move(task), std::move(label));
    };
    host.cancel_task = [this](int task_id) { scheduler_->cancel(task_id); };
    host.publish_state = [this] { publish_resolver_runtime_state(); };
    host.refresh_generation_snapshot = [this] {
        if (is_event_loop_thread()) {
            refresh_generation_snapshot();
        } else {
            (void)post_control_task([this] { refresh_generation_snapshot(); },
                                    "resolver-generation-snapshot");
        }
    };
    return host;
}

void Daemon::sync_resolver_integration(const Config& config) {
    reconfigure_resolver_integration(resolver_integration_, config, resolver_deps_);
}

void Daemon::reload_resolver_or_throw(const char* reason) {
    const auto reload = resolver_integration_->reload(ResolverReloadRequest{reason});
    if (!reload.ok) {
        throw DaemonError(reload.error.empty() ? "system resolver reload hook failed"
                                               : reload.error);
    }
}

void Daemon::reload_and_verify_resolver_or_throw(const char* reason) {
    reload_resolver_or_throw(reason);
    const auto verified = resolver_integration_->verify(ResolverReloadRequest{reason});
    if (!verified.ok) {
        throw DaemonError(verified.error);
    }
}

void Daemon::begin_resolver_generation() {
    resolver_integration_->apply_started(unix_timestamp_now_seconds());
    refresh_generation_snapshot();
}

ControlGenerationSnapshot Daemon::make_generation_snapshot() {
    ControlGenerationSnapshot snapshot;
    const Ipv6SupportDecision ipv6_decision = resolve_ipv6_support(config_);
    log_ipv6_support_decision_once(ipv6_decision);
    snapshot.ipv6_enabled = ipv6_decision.enabled;
    snapshot.intercept_dns_hold = intercept_effective_snapshot().dns_hold;
    snapshot.generation = runtime_generation_.load(std::memory_order_acquire);
    return snapshot;
}

void Daemon::refresh_generation_snapshot() {
    generation_snapshot_ = make_generation_snapshot();
}

bool Daemon::accept_resolver_generated_hash(std::uint64_t generation,
                                            const std::string& hash) {
    if (!generation_snapshot_.has_value() || hash.empty() ||
        generation_snapshot_->generation != generation ||
        runtime_generation_.load(std::memory_order_acquire) != generation) {
        return false;
    }
    return resolver_integration_->accept_generated_config(hash);
}

RuntimeStateSnapshot Daemon::build_runtime_state_snapshot() const {
    RuntimeStateSnapshot snapshot;
    snapshot.firewall_state = firewall_state_;
    snapshot.route_specs = route_table_.get_routes();
    snapshot.policy_rule_specs = policy_rules_.get_rules();
    const auto resolver = resolver_integration_->health();
    snapshot.resolver_integration = resolver.mode;
    snapshot.resolver_config_hash = resolver.expected_hash;
    snapshot.resolver_config_hash_actual = resolver.actual_hash;
    snapshot.resolver_config_hash_actual_ts = resolver.actual_ts;
    snapshot.resolver_config_sync_state = resolver.sync_state;
    snapshot.resolver_config_probe_status = resolver.probe_status;
    snapshot.resolver_live_status = resolver.live_status;
    snapshot.resolver_last_probe_ts = resolver.last_probe_ts;
    snapshot.apply_started_ts = resolver.apply_started_ts;
    snapshot.routing_runtime_active = routing_runtime_active_;
    snapshot.runtime_state = runtime_state_machine_.state();
    snapshot.runtime_state_reason = runtime_state_machine_.reason();

    if (urltest_manager_) {
        for (const auto& outbound : config_.outbounds.value_or(std::vector<Outbound>{})) {
            if (outbound.type != OutboundType::URLTEST && outbound.type != OutboundType::ICMPTEST) {
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

void Daemon::publish_runtime_state(StatusPublishScope scope) {
    Logger::instance().trace("runtime_state_publish", "routing_runtime_active={}",
                             routing_runtime_active_ ? "true" : "false");
    invalidate_routing_health_cache();
    const auto snapshot = build_runtime_state_snapshot();
    runtime_state_store_.publish(snapshot);
    if (snapshot.runtime_state == RuntimeState::running &&
        snapshot.routing_runtime_active) {
        // Warm the canonical report off-loop so the first API/status client
        // still gets an immediate response without waiting for inspection.
        (void)cached_routing_health();
    }
#ifdef WITH_API
    if (status_stream_) {
        StatusUpdate updates = StatusUpdate::Outbounds;
        if (scope == StatusPublishScope::ServiceAndOutbounds) {
            updates = updates | StatusUpdate::Service;
        } else if (scope == StatusPublishScope::OutboundsAndInterfaces) {
            updates = updates | StatusUpdate::Interfaces;
        }
        status_stream_->reconcile(updates);
    }
#endif
}

void Daemon::publish_resolver_runtime_state() {
    const auto resolver = resolver_integration_->health();
    runtime_state_store_.update_resolver(ResolverRuntimeStateUpdate{
        resolver.expected_hash,
        resolver.actual_hash,
        resolver.actual_ts,
        resolver.sync_state,
        resolver.probe_status,
        resolver.live_status,
        resolver.last_probe_ts,
        resolver.apply_started_ts,
        resolver.mode,
    });
#ifdef WITH_API
    if (status_stream_) {
        status_stream_->reconcile(StatusUpdate::Service);
    }
#endif
}

void Daemon::publish_urltest_runtime_state(const std::string& tag) {
    runtime_state_store_.update_urltest(
        tag, urltest_manager_ ? urltest_manager_->get_state(tag) : std::nullopt);
#ifdef WITH_API
    if (status_stream_) {
        status_stream_->reconcile(StatusUpdate::Outbounds);
    }
#endif
}

} // namespace keen_pbr3
