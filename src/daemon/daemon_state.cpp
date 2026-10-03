#include "daemon.hpp"

#include "../log/logger.hpp"
#ifdef WITH_API
#include "../api/status_stream.hpp"
#endif
#include "../util/ipv6_support.hpp"
#include "../util/time_utils.hpp"

// Runtime state snapshots published by the daemon core to the control socket,
// the REST API and the status stream.

namespace keen_pbr3 {

void Daemon::begin_runtime_generation() {
    apply_started_ts_.store(unix_timestamp_now_seconds(), std::memory_order_release);
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

RuntimeStateSnapshot Daemon::build_runtime_state_snapshot() const {
    RuntimeStateSnapshot snapshot;
    snapshot.firewall_state = firewall_state_;
    snapshot.route_specs = route_table_.get_routes();
    snapshot.policy_rule_specs = policy_rules_.get_rules();
    const auto apply_started = apply_started_ts_.load(std::memory_order_acquire);
    if (apply_started > 0) snapshot.apply_started_ts = apply_started;
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
