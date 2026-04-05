#pragma once

#include "../firewall/firewall.hpp"
#include "../routing/firewall_state.hpp"
#include "../routing/netlink.hpp"
#include "../routing/policy_rule.hpp"
#include "../routing/route_table.hpp"
#include "routing_health.hpp"

#include <nlohmann/json.hpp>

namespace keen_pbr3 {

// Build a routing health report from caller-provided snapshots of the tracked
// route/policy state. This lets API readers copy daemon state quickly under
// lock and perform the expensive live verification work after releasing it.
RoutingHealthReport build_routing_health_report(
    FirewallBackend firewall_backend,
    const FirewallState& firewall_state,
    const std::vector<RouteSpec>& tracked_routes,
    const std::vector<RuleSpec>& tracked_policy_rules,
    NetlinkManager& netlink);

// Orchestrates firewall and routing verification to produce a RoutingHealthReport.
// Combines results from FirewallVerifier and RoutingVerifier.
class RoutingHealthChecker {
public:
    RoutingHealthChecker(const Firewall& firewall,
                         const FirewallState& firewall_state,
                         const RouteTable& route_table,
                         const PolicyRuleManager& policy_rules,
                         NetlinkManager& netlink);

    // Run all health checks and return a combined report.
    // Never throws: exceptions are caught and stored in report.error.
    RoutingHealthReport check() const;

    // Non-copyable
    RoutingHealthChecker(const RoutingHealthChecker&) = delete;
    RoutingHealthChecker& operator=(const RoutingHealthChecker&) = delete;

private:
    const Firewall& firewall_;
    const FirewallState& firewall_state_;
    const RouteTable& route_table_;
    const PolicyRuleManager& policy_rules_;
    NetlinkManager& netlink_;
};

// Serialize a RoutingHealthReport to JSON.
// - "overall": "ok" / "degraded" / "error"
// - fwmark/fwmask values formatted as hex strings (e.g. "0x00010000")
nlohmann::json routing_health_report_to_json(const RoutingHealthReport& r);

} // namespace keen_pbr3
