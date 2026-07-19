#pragma once

#include <vector>

#include "netlink.hpp"

namespace keen_pbr3 {

// Reconciles kernel state without relying on an in-memory manifest.  Routes
// are owned by the generated rtm_protocol; rules are owned by an exact,
// non-zero mark/mask pair reserved by the active configuration.
class RoutingReconciler {
public:
    explicit RoutingReconciler(RoutingNetlinkOperations& netlink)
        : netlink_(netlink) {}

    void reconcile(const std::vector<RouteSpec>& desired_routes,
                   const std::vector<RuleSpec>& desired_rules);

private:
    RoutingNetlinkOperations& netlink_;
};

} // namespace keen_pbr3
