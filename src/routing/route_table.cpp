#include "route_table.hpp"

#include "../log/logger.hpp"

#include <algorithm>
#include <set>

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

RouteTable::RouteTable(NetlinkManager& netlink, bool dry_run)
    : netlink_(netlink),
      dry_run_(dry_run) {}

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
    if (!dry_run_) {
        try {
            netlink_.add_route(spec);
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
}

void RouteTable::remove(const RouteSpec& spec) {
    auto it = std::find_if(routes_.begin(), routes_.end(),
                           [&](const RouteSpec& r) { return routes_equal(r, spec); });
    if (it == routes_.end()) {
        return;
    }
    if (!dry_run_) {
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
    routes_.erase(it);
}

void RouteTable::clear() {
    if (!dry_run_) {
        std::set<uint32_t> managed_tables;
        for (const auto& route : routes_) {
            managed_tables.insert(route.table);
        }

        for (uint32_t table_id : managed_tables) {
            try {
                netlink_.flush_routes_in_table(table_id);
            } catch (const std::exception& e) {
                Logger::instance().error(
                    "Failed to flush routes in table {} during clear(): {}",
                    table_id,
                    e.what());
            } catch (...) {
                Logger::instance().error(
                    "Failed to flush routes in table {} during clear(): unknown error",
                    table_id);
            }
        }
    }
    routes_.clear();
}

} // namespace keen_pbr3
