#pragma once

#include "../health/routing_health.hpp"
#include "netlink.hpp"

#include <string>

namespace keen_pbr3 {

// Verifies live kernel routing state against expected configuration.
// Uses NetlinkManager (libnl3) — no CLI calls.
class RoutingVerifier {
public:
    explicit RoutingVerifier(NetlinkManager& netlink);

    // Verify that the expected route (from a RouteSpec) is present in the kernel.
    // outbound_tag is used to populate the RouteTableCheck for reporting.
    RouteTableCheck verify_route_table(const RouteSpec& expected,
                                       const std::string& outbound_tag);

    // Verify that the expected ip policy rule is present in the kernel for
    // both AF_INET and AF_INET6 (or the family specified in expected.family).
    // outbound_tag is used to populate the PolicyRuleCheck for reporting.
    PolicyRuleCheck verify_policy_rule(const RuleSpec& expected,
                                       const std::string& outbound_tag);

    // Non-copyable
    RoutingVerifier(const RoutingVerifier&) = delete;
    RoutingVerifier& operator=(const RoutingVerifier&) = delete;

private:
    NetlinkManager& netlink_;
};

} // namespace keen_pbr3
