#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

enum class CheckStatus {
    ok,
    missing,
    mismatch
};

struct FirewallChainCheck {
    bool chain_present{false};
    bool prerouting_hook_present{false};
    std::string detail;
};

struct FirewallRuleCheck {
    std::string set_name;
    std::string action;
    std::optional<uint32_t> expected_fwmark;
    std::optional<uint32_t> actual_fwmark;
    CheckStatus status{CheckStatus::missing};
    std::string detail;
};

struct RouteTableCheck {
    uint32_t table_id{0};
    std::string outbound_tag;
    std::optional<std::string> expected_destination;
    std::optional<std::string> expected_interface;
    std::optional<std::string> expected_gateway;
    std::optional<uint32_t> expected_metric;
    std::optional<std::string> expected_route_type;
    bool table_exists{false};
    bool default_route_present{false};
    bool interface_matches{false};
    bool gateway_matches{false};
    CheckStatus status{CheckStatus::missing};
    std::string detail;
};

struct PolicyRuleCheck {
    uint32_t fwmark{0};
    uint32_t fwmask{0};
    uint32_t expected_table{0};
    uint32_t priority{0};
    bool rule_present_v4{false};
    bool rule_present_v6{false};
    CheckStatus status{CheckStatus::missing};
    std::string detail;
};

struct RoutingHealthReport {
    bool overall_ok{false};
    std::string firewall_backend;
    FirewallChainCheck firewall_chain;
    std::vector<FirewallRuleCheck> firewall_rules;
    std::vector<RouteTableCheck> route_tables;
    std::vector<PolicyRuleCheck> policy_rules;
    std::string error;
};

} // namespace keen_pbr3
