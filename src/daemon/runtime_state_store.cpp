#include "runtime_state_store.hpp"

namespace keen_pbr3 {

RuntimeStateSnapshot RuntimeStateStore::snapshot() const {
    KPBR_SHARED_LOCK(lock, mutex_);
    return snapshot_;
}

ServiceRuntimeSnapshot RuntimeStateStore::service_snapshot() const {
    KPBR_SHARED_LOCK(lock, mutex_);
    return ServiceRuntimeSnapshot{
        snapshot_.resolver_config_hash,
        snapshot_.resolver_config_hash_actual,
        snapshot_.resolver_config_hash_actual_ts,
        snapshot_.resolver_config_sync_state,
        snapshot_.resolver_config_probe_status,
        snapshot_.resolver_live_status,
        snapshot_.resolver_last_probe_ts,
        snapshot_.apply_started_ts,
        snapshot_.routing_runtime_active,
        snapshot_.runtime_state,
        snapshot_.runtime_state_reason,
    };
}

OutboundRuntimeSnapshot RuntimeStateStore::outbound_snapshot() const {
    KPBR_SHARED_LOCK(lock, mutex_);
    return OutboundRuntimeSnapshot{
        snapshot_.firewall_state.get_outbound_marks(),
        snapshot_.policy_rule_specs,
        snapshot_.firewall_state.get_urltest_selections(),
        snapshot_.urltest_states,
        snapshot_.runtime_state,
    };
}

ControlRuntimeSnapshot RuntimeStateStore::control_snapshot(bool include_realized_rules) const {
    KPBR_SHARED_LOCK(lock, mutex_);
    std::vector<ControlRuntimeSnapshot::Rule> rules;
    if (include_realized_rules) {
        rules.reserve(snapshot_.firewall_state.get_rules().size());
        for (const auto& rule : snapshot_.firewall_state.get_rules()) {
            rules.push_back({rule.rule_index, rule.set_names, rule.outbound_tag,
                             rule.action_type, rule.fwmark});
        }
    }
    return ControlRuntimeSnapshot{
        std::move(rules),
        snapshot_.routing_runtime_active,
        snapshot_.runtime_state,
        snapshot_.runtime_state_reason,
    };
}

void RuntimeStateStore::publish(RuntimeStateSnapshot snapshot) {
    KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
    snapshot_ = std::move(snapshot);
}

void RuntimeStateStore::update_resolver(ResolverRuntimeStateUpdate update) {
    KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
    snapshot_.resolver_config_hash = std::move(update.resolver_config_hash);
    snapshot_.resolver_config_hash_actual =
        std::move(update.resolver_config_hash_actual);
    snapshot_.resolver_config_hash_actual_ts = update.resolver_config_hash_actual_ts;
    snapshot_.resolver_config_sync_state = update.resolver_config_sync_state;
    snapshot_.resolver_config_probe_status = update.resolver_config_probe_status;
    snapshot_.resolver_live_status = update.resolver_live_status;
    snapshot_.resolver_last_probe_ts = update.resolver_last_probe_ts;
    snapshot_.apply_started_ts = update.apply_started_ts;
}

void RuntimeStateStore::update_urltest(std::string tag,
                                       std::optional<UrltestState> state) {
    KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
    if (state.has_value()) {
        snapshot_.urltest_states.insert_or_assign(std::move(tag), std::move(*state));
    } else {
        snapshot_.urltest_states.erase(tag);
    }
}

} // namespace keen_pbr3
