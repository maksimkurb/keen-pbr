#pragma once

#include "../config/config.hpp"
#include "../routing/firewall_state.hpp"
#include "../routing/policy_rule.hpp"
#include "../routing/route_table.hpp"

#include <map>
#include <vector>

namespace keen_pbr3 {

// Populate route tables and policy rules from config. Works for real or dry-run instances.
void populate_routing_state(const Config& cfg,
                            const OutboundMarkMap& marks,
                            RouteTable& routes,
                            PolicyRuleManager& rules);

// Build firewall rule state (set names + actions) from config without touching firewall.
// urltest_selections optionally overrides URLTEST outbounds to a selected child tag.
std::vector<RuleState> build_fw_rule_states(
    const Config& cfg,
    const OutboundMarkMap& marks,
    const std::map<std::string, std::string>* urltest_selections = nullptr);

} // namespace keen_pbr3
