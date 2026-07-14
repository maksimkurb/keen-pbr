#include "route_table.hpp"

#include "../log/logger.hpp"

#include <algorithm>

namespace keen_pbr3 {

namespace {

bool routes_equal(const RouteSpec& a, const RouteSpec& b) {
    return a.destination == b.destination &&
           a.table == b.table &&
           a.interface == b.interface &&
           a.gateway == b.gateway &&
           a.blackhole == b.blackhole &&
           a.unreachable == b.unreachable &&
           a.family == b.family &&
           a.metric == b.metric;
}

} // anonymous namespace

RouteTable::RouteTable(RouteNetlinkOperations& netlink, bool dry_run)
    : netlink_(netlink),
      dry_run_(dry_run) {}

RouteTable::~RouteTable() {
    // Best-effort cleanup on destruction
    try {
        clear();
    } catch (const std::exception& e) {
        Logger::instance().error("RouteTable cleanup failed during destruction: {}",
                                 e.what());
    } catch (...) {
        Logger::instance().error("RouteTable cleanup failed during destruction: unknown error");
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
    bool owned = dry_run_;
    if (!dry_run_) {
        try {
            owned = netlink_.add_route(spec) == RouteAddResult::Created;
        } catch (const std::exception& e) {
            Logger::instance().error(
                "Failed to add route (dst={}, table={}, iface={}, gw={}, metric={}, blackhole={}, unreachable={}): {}",
                spec.destination,
                spec.table,
                spec.interface.value_or("(none)"),
                spec.gateway.value_or("(none)"),
                spec.metric,
                spec.blackhole,
                spec.unreachable,
                e.what());
            return;
        }
    }
    routes_.push_back(spec);
    if (owned) owned_routes_.push_back(spec);
}

void RouteTable::remove(const RouteSpec& spec) {
    auto it = std::find_if(routes_.begin(), routes_.end(),
                           [&](const RouteSpec& r) { return routes_equal(r, spec); });
    if (it == routes_.end()) {
        return;
    }
    auto owned_it = std::find_if(owned_routes_.begin(), owned_routes_.end(),
                                 [&](const RouteSpec& route) {
                                     return routes_equal(route, spec);
                                 });
    if (!dry_run_ && owned_it != owned_routes_.end()) {
        try {
            netlink_.delete_route(spec);
        } catch (const std::exception& e) {
            Logger::instance().error(
                "Failed to delete route (dst={}, table={}, iface={}, gw={}, metric={}, blackhole={}, unreachable={}): {}",
                spec.destination,
                spec.table,
                spec.interface.value_or("(none)"),
                spec.gateway.value_or("(none)"),
                spec.metric,
                spec.blackhole,
                spec.unreachable,
                e.what());
        }
    }
    if (owned_it != owned_routes_.end()) owned_routes_.erase(owned_it);
    routes_.erase(it);
}

void RouteTable::clear() {
    if (!dry_run_) {
        for (auto it = owned_routes_.rbegin(); it != owned_routes_.rend(); ++it) {
            try {
                netlink_.delete_route(*it);
            } catch (const std::exception& e) {
                Logger::instance().error(
                    "Failed to delete owned route (dst={}, table={}) during clear(): {}",
                    it->destination,
                    it->table,
                    e.what());
            } catch (...) {
                Logger::instance().error(
                    "Failed to delete owned route (dst={}, table={}) during clear(): unknown error",
                    it->destination,
                    it->table);
            }
        }
    }
    owned_routes_.clear();
    routes_.clear();
}

} // namespace keen_pbr3
