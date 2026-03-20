#pragma once

#include "../config/config.hpp"
#include "../routing/firewall_state.hpp"
#include "../routing/policy_rule.hpp"
#include "../routing/route_table.hpp"

#include <functional>
#include <map>
#include <vector>

namespace keen_pbr3 {

// Routing table IDs that must never be used for keen-pbr outbound tables.
static constexpr uint32_t RESERVED_TABLE_UNSPEC         = 0;
static constexpr uint32_t RESERVED_TABLE_PRELOCAL       = 128;
static constexpr uint32_t RESERVED_TABLE_BLOCK_LOW      = 250;
static constexpr uint32_t RESERVED_TABLE_BLOCK_HIGH     = 260; // covers 253=default, 254=main, 255=local
static constexpr uint32_t RESERVED_TABLE_SYSTEM_START   = 32000;

inline bool is_reserved_table(uint32_t id) {
    return id == RESERVED_TABLE_UNSPEC ||
           id == RESERVED_TABLE_PRELOCAL ||
           (id >= RESERVED_TABLE_BLOCK_LOW && id <= RESERVED_TABLE_BLOCK_HIGH) ||
           id >= RESERVED_TABLE_SYSTEM_START;
}

using OutboundReachabilityFn = std::function<bool(const Outbound&)>;

// Populate route tables and policy rules from config. Works for real or dry-run instances.
void populate_routing_state(const Config& cfg,
                            const OutboundMarkMap& marks,
                            RouteTable& routes,
                            PolicyRuleManager& rules,
                            OutboundReachabilityFn reachability_check = {},
                            const std::map<std::string, std::string>* urltest_selections = nullptr);

bool is_interface_outbound_reachable(const Outbound& outbound, NetlinkManager& netlink);

// Build firewall rule state (set names + actions) from config without touching firewall.
// urltest_selections optionally overrides URLTEST outbounds to a selected child tag.
std::vector<RuleState> build_fw_rule_states(
    const Config& cfg,
    const OutboundMarkMap& marks,
    const std::map<std::string, std::string>* urltest_selections = nullptr);

} // namespace keen_pbr3
