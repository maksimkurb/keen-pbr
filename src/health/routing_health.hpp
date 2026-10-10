#pragma once

#include "../firewall/firewall.hpp"

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

enum class VerificationState {
    verified,    // firewall chain is properly configured
    unavailable, // verification not possible (runtime initializing/applying, snapshot unavailable)
    failed       // firewall chain is missing or misconfigured
};

struct FirewallChainCheck {
    bool chain_present{false};
    bool prerouting_hook_present{false};
    VerificationState verification_state{VerificationState::failed};
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
    std::string expected_action{"lookup"};
    bool rule_present_v4{false};
    bool rule_present_v6{false};
    CheckStatus status{CheckStatus::missing};
    std::string detail;
};

// Extend this enum (and the API schema) to add new warning kinds.
enum class HealthWarningCode {
    nat_missing,
    nat_partial,
    rp_filter_strict,
    fwmark_mask_conflict
};

inline const char* health_warning_code_name(HealthWarningCode code) {
    switch (code) {
        case HealthWarningCode::nat_missing: return "nat_missing";
        case HealthWarningCode::nat_partial: return "nat_partial";
        case HealthWarningCode::rp_filter_strict: return "rp_filter_strict";
        case HealthWarningCode::fwmark_mask_conflict: return "fwmark_mask_conflict";
    }
    return "unknown";
}

// Non-blocking host configuration warning; never affects overall_ok.
struct HealthWarning {
    HealthWarningCode code{HealthWarningCode::nat_missing};
    std::optional<std::string> interface;
    std::optional<std::string> outbound;
    std::string message;
};

struct RoutingHealthReport {
    bool overall_ok{false};
    std::optional<FirewallBackend> firewall_backend;
    FirewallChainCheck firewall_chain;
    std::vector<FirewallRuleCheck> firewall_rules;
    std::vector<RouteTableCheck> route_tables;
    std::vector<PolicyRuleCheck> policy_rules;
    std::vector<HealthWarning> warnings;
    std::string error;
};

} // namespace keen_pbr3
