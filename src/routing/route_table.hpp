#pragma once

#include <vector>

#include "netlink.hpp"

namespace keen_pbr3 {

// Manages installed kernel routes, tracking them for duplicate avoidance and cleanup.
// Uses NetlinkManager for actual kernel operations.
class RouteTable {
public:
    explicit RouteTable(NetlinkManager& netlink);
    ~RouteTable();

    // Non-copyable
    RouteTable(const RouteTable&) = delete;
    RouteTable& operator=(const RouteTable&) = delete;

    // Add a route. If an identical route is already tracked, this is a no-op.
    void add(const RouteSpec& spec);

    // Remove a specific route. If not tracked, this is a no-op.
    void remove(const RouteSpec& spec);

    // Remove all installed routes (shutdown cleanup).
    void clear();

    // Number of currently tracked routes.
    size_t size() const { return routes_.size(); }

private:
    NetlinkManager& netlink_;
    std::vector<RouteSpec> routes_;

    // Check if an identical route is already tracked.
    bool is_tracked(const RouteSpec& spec) const;
};

} // namespace keen_pbr3
