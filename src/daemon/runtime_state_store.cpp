#include "runtime_state_store.hpp"

namespace keen_pbr3 {

RuntimeStateSnapshot RuntimeStateStore::snapshot() const {
    KPBR_SHARED_LOCK(lock, mutex_);
    return snapshot_;
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

} // namespace keen_pbr3
