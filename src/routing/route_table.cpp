#include "route_table.hpp"

#include <algorithm>

namespace keen_pbr3 {

namespace {

bool routes_equal(const RouteSpec& a, const RouteSpec& b) {
    return a.destination == b.destination &&
           a.table == b.table &&
           a.interface == b.interface &&
           a.gateway == b.gateway &&
           a.blackhole == b.blackhole &&
           a.family == b.family;
}

} // anonymous namespace

RouteTable::RouteTable(NetlinkManager& netlink)
    : netlink_(netlink) {}

RouteTable::~RouteTable() {
    // Best-effort cleanup on destruction
    try {
        clear();
    } catch (...) {
        // Suppress exceptions in destructor
    }
}

bool RouteTable::is_tracked(const RouteSpec& spec) const {
    return std::any_of(routes_.begin(), routes_.end(),
                       [&](const RouteSpec& r) { return routes_equal(r, spec); });
}

void RouteTable::add(const RouteSpec& spec) {
    if (is_tracked(spec)) {
        return;
    }
    netlink_.add_route(spec);
    routes_.push_back(spec);
}

void RouteTable::remove(const RouteSpec& spec) {
    auto it = std::find_if(routes_.begin(), routes_.end(),
                           [&](const RouteSpec& r) { return routes_equal(r, spec); });
    if (it == routes_.end()) {
        return;
    }
    netlink_.delete_route(spec);
    routes_.erase(it);
}

void RouteTable::clear() {
    // Remove in reverse order (last added first)
    for (auto it = routes_.rbegin(); it != routes_.rend(); ++it) {
        try {
            netlink_.delete_route(*it);
        } catch (...) {
            // Best effort: continue removing remaining routes
        }
    }
    routes_.clear();
}

} // namespace keen_pbr3
