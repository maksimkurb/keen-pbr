#pragma once

#include "../api/generated/api_types.hpp"
#include "../routing/firewall_state.hpp"
#include "../routing/policy_rule.hpp"
#include "../routing/route_table.hpp"
#include "../routing/urltest_manager.hpp"
#include "../util/traced_mutex.hpp"

#include <map>
#include <string>
#include <cstdint>
#include <optional>
#include <vector>

namespace keen_pbr3 {

struct RuntimeStateSnapshot {
    FirewallState firewall_state;
    std::vector<RouteSpec> route_specs;
    std::vector<RuleSpec> policy_rule_specs;
    std::map<std::string, UrltestState> urltest_states;
    std::string resolver_config_hash;
    std::string resolver_config_hash_actual;
    std::optional<std::int64_t> resolver_config_hash_actual_ts;
    api::ResolverLiveStatus resolver_live_status{api::ResolverLiveStatus::UNKNOWN};
    std::optional<std::int64_t> resolver_last_probe_ts;
    bool routing_runtime_active{true};
};

class RuntimeStateStore {
public:
    RuntimeStateSnapshot snapshot() const;
    void publish(RuntimeStateSnapshot snapshot);

private:
    mutable TracedSharedMutex mutex_;
    RuntimeStateSnapshot snapshot_ GUARDED_BY(mutex_);
};

} // namespace keen_pbr3
