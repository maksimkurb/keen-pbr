#include "routing_reconciler.hpp"

#include <algorithm>
#include <netinet/in.h>
#include <set>

namespace keen_pbr3 {
namespace {

int route_family(const RouteSpec& route) {
    if (route.family != 0) return route.family;
    return route.destination == "default" ? AF_INET : 0;
}

bool same_route(const RouteSpec& expected, const DumpedRoute& actual,
                bool include_protocol) {
    return expected.destination == actual.destination &&
           expected.table == actual.table &&
           expected.interface == actual.interface &&
           expected.gateway == actual.gateway &&
           expected.blackhole == actual.blackhole &&
           expected.unreachable == actual.unreachable &&
           route_family(expected) == actual.family &&
           expected.metric == actual.metric &&
           (!include_protocol || expected.protocol == actual.protocol);
}

bool same_route_lookup_key(const RouteSpec& expected, const DumpedRoute& actual) {
    return expected.destination == actual.destination &&
           expected.table == actual.table &&
           route_family(expected) == actual.family;
}

RouteSpec route_spec_from_dump(const DumpedRoute& route) {
    return {route.destination, route.table, route.interface,
            route.gateway, route.blackhole, route.unreachable,
            route.family, route.metric, route.protocol};
}

DumpedRoute dumped_route_from_spec(const RouteSpec& route) {
    return {route.destination, route.table, route.interface,
            route.gateway, route.blackhole, route.unreachable,
            route.family, route.metric, route.protocol};
}

bool same_rule(const RuleSpec& expected, const DumpedRule& actual) {
    return expected.fwmark == actual.fwmark && expected.fwmask == actual.fwmask &&
           expected.table == actual.table && expected.priority == actual.priority &&
           expected.action == actual.action &&
           (expected.family == 0 || expected.family == actual.family);
}

bool rule_namespace_owned(const DumpedRule& actual,
                          const std::vector<RuleSpec>& desired) {
    return std::any_of(desired.begin(), desired.end(), [&](const RuleSpec& expected) {
        return expected.fwmark != 0 && expected.fwmask != 0 &&
               expected.fwmark == actual.fwmark && expected.fwmask == actual.fwmask;
    });
}

} // namespace

void RoutingReconciler::reconcile(const std::vector<RouteSpec>& desired_routes,
                                  const std::vector<RuleSpec>& desired_rules) {
    std::set<uint32_t> tables;
    for (const auto& route : desired_routes) tables.insert(route.table);

    std::vector<DumpedRoute> actual_routes;
    for (uint32_t table : tables) {
        auto routes = netlink_.dump_routes_in_table(table);
        actual_routes.insert(actual_routes.end(), routes.begin(), routes.end());
    }

    // A foreign route with the same lookup identity cannot be safely replaced:
    // it would make the configured path ambiguous.  Fail before mutation.
    for (const auto& desired : desired_routes) {
        for (const auto& actual : actual_routes) {
            if (same_route(desired, actual, true)) break;
            if (actual.protocol != KEEN_PBR_GENERATED_ROUTE_PROTOCOL &&
                same_route(desired, actual, false)) {
                throw NetlinkError("Foreign route conflicts with desired route in table " +
                                   std::to_string(desired.table) +
                                   "; choose a different table_start");
            }
        }
    }

    // Add all new paths before pruning unrelated old ones. A generated route
    // with the same table/family/destination is the exception: some kernels
    // report EEXIST when replacing an IPv6 unreachable fallback with unicast,
    // even when their metrics differ. The terminal RPDB guard remains active
    // while this owned fallback is replaced.
    for (const auto& desired : desired_routes) {
        auto present = std::any_of(actual_routes.begin(), actual_routes.end(),
                                   [&](const DumpedRoute& actual) {
                                       return same_route(desired, actual, true);
                                   });
        if (present) continue;

        for (auto it = actual_routes.begin(); it != actual_routes.end();) {
            const bool wanted = std::any_of(desired_routes.begin(), desired_routes.end(),
                                            [&](const RouteSpec& candidate) {
                                                return same_route(candidate, *it, true);
                                            });
            if (it->protocol == KEEN_PBR_GENERATED_ROUTE_PROTOCOL &&
                !wanted && same_route_lookup_key(desired, *it)) {
                netlink_.delete_route(route_spec_from_dump(*it));
                it = actual_routes.erase(it);
            } else {
                ++it;
            }
        }

        const auto result = netlink_.add_route(desired);
        if (result == RouteAddResult::Created) {
            actual_routes.push_back(dumped_route_from_spec(desired));
            continue;
        }

        auto refreshed = netlink_.dump_routes_in_table(desired.table);
        present = std::any_of(refreshed.begin(), refreshed.end(),
                              [&](const DumpedRoute& actual) {
                                  return same_route(desired, actual, true);
                              });
        if (!present) {
            throw NetlinkError(
                "kernel reported route already present but desired route is absent in table " +
                std::to_string(desired.table));
        }
    }
    for (const auto& actual : actual_routes) {
        if (actual.protocol != KEEN_PBR_GENERATED_ROUTE_PROTOCOL) continue;
        const bool wanted = std::any_of(desired_routes.begin(), desired_routes.end(),
                                        [&](const RouteSpec& desired) {
                                            return same_route(desired, actual, true);
                                        });
        if (!wanted) {
            netlink_.delete_route(route_spec_from_dump(actual));
        }
    }

    const auto actual_rules = netlink_.dump_policy_rules();
    for (const auto& desired : desired_rules) {
        const int families[] = {AF_INET, AF_INET6};
        for (int family : families) {
            if (desired.family != 0 && desired.family != family) continue;
            const bool present = std::any_of(actual_rules.begin(), actual_rules.end(),
                                             [&](const DumpedRule& actual) {
                                                 return same_rule(desired, actual) &&
                                                        actual.family == family;
                                             });
            if (!present) netlink_.add_rule_for_family(desired, family);
        }
    }
    for (const auto& actual : actual_rules) {
        if (!rule_namespace_owned(actual, desired_rules)) continue;
        const bool wanted = std::any_of(desired_rules.begin(), desired_rules.end(),
                                        [&](const RuleSpec& desired) {
                                            return same_rule(desired, actual);
                                        });
        if (!wanted) {
            RuleSpec stale{actual.fwmark, actual.fwmask, actual.table,
                           actual.priority, actual.family, actual.action};
            netlink_.delete_rule_for_family(stale, actual.family);
        }
    }
}

} // namespace keen_pbr3
