#pragma once

#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "../config/config.hpp"

namespace keen_pbr3 {

// Re-export config outbound types for routing use
// Outbound = std::variant<InterfaceOutbound, TableOutbound, BlackholeOutbound>
// SkipAction is already defined in config.hpp

// Health status callback: given an outbound tag, return true if healthy
using HealthCheckFn = std::function<bool(const std::string& tag)>;

// Result of resolving a route rule to a concrete routing action
struct RoutingDecision {
    // The resolved outbound, or std::nullopt if the rule says to skip
    std::optional<const Outbound*> outbound;
    bool is_skip{false};

    static RoutingDecision skip() {
        RoutingDecision d;
        d.is_skip = true;
        return d;
    }

    static RoutingDecision route_to(const Outbound* ob) {
        RoutingDecision d;
        d.outbound = ob;
        return d;
    }

    static RoutingDecision none() {
        return RoutingDecision{};
    }
};

// Resolve a route rule's action to a RoutingDecision.
// For single outbound: looks up the tag in the outbounds list.
// For failover chain: selects the first healthy outbound using the health check function.
// For skip action: returns RoutingDecision::skip().
//
// Parameters:
//   action     - the route rule action variant (single tag, failover tags, or SkipAction)
//   outbounds  - the configured outbounds to look up by tag
//   health_fn  - callback that returns true if an outbound tag is healthy
//                (if null, all outbounds are considered healthy)
RoutingDecision resolve_route_action(
    const std::variant<std::string, std::vector<std::string>, SkipAction>& action,
    const std::vector<Outbound>& outbounds,
    const HealthCheckFn& health_fn = nullptr);

} // namespace keen_pbr3
