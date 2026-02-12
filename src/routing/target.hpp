#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../config/config.hpp"

namespace keen_pbr3 {

// Result of resolving a route rule to a concrete routing action
struct RoutingDecision {
    // The resolved outbound, or std::nullopt if no match
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

// Resolve a route rule's outbound tag to a RoutingDecision.
// Looks up the tag in the outbounds list.
// If tag refers to an IgnoreOutbound, returns RoutingDecision::skip().
// If tag refers to a UrltestOutbound, returns RoutingDecision::route_to() with the UrltestOutbound.
//
// Parameters:
//   outbound_tag - the single outbound tag from the route rule
//   outbounds    - the configured outbounds to look up by tag
RoutingDecision resolve_route_action(
    const std::string& outbound_tag,
    const std::vector<Outbound>& outbounds);

} // namespace keen_pbr3
