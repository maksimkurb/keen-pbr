#include <doctest/doctest.h>

#include "../src/routing/routing_reconciler.hpp"

#include <netinet/in.h>
#include <stdexcept>
#include <vector>

namespace keen_pbr3 {
namespace {

class FakeRoutingNetlink final : public RoutingNetlinkOperations {
public:
    RouteAddResult add_route(const RouteSpec& route) override {
        added_routes.push_back(route);
        return RouteAddResult::Created;
    }
    void delete_route(const RouteSpec& route) override {
        if (fail_route_delete) throw std::runtime_error("route delete failed");
        deleted_routes.push_back(route);
    }
    RuleAddResult add_rule_for_family(const RuleSpec& rule, int family) override {
        added_rules.push_back({rule, family});
        return RuleAddResult::Created;
    }
    void delete_rule_for_family(const RuleSpec& rule, int family) override {
        deleted_rules.push_back({rule, family});
    }
    std::vector<DumpedRoute> dump_routes_in_table(uint32_t table, int = 0) override {
        std::vector<DumpedRoute> result;
        for (const auto& route : routes) if (route.table == table) result.push_back(route);
        return result;
    }
    std::vector<DumpedRule> dump_policy_rules(int = 0) override { return rules; }

    std::vector<DumpedRoute> routes;
    std::vector<DumpedRule> rules;
    std::vector<RouteSpec> added_routes;
    std::vector<RouteSpec> deleted_routes;
    std::vector<std::pair<RuleSpec, int>> added_rules;
    std::vector<std::pair<RuleSpec, int>> deleted_rules;
    bool fail_route_delete{false};
};

RouteSpec desired_route() {
    RouteSpec route;
    route.destination = "default";
    route.table = 100;
    route.blackhole = true;
    route.family = AF_INET;
    return route;
}

DumpedRoute dumped(const RouteSpec& route) {
    return {route.destination, route.table, route.interface, route.gateway,
            route.blackhole, route.unreachable, route.family, route.metric, route.protocol};
}

} // namespace

TEST_CASE("RoutingReconciler adopts an intact generated route after restart") {
    FakeRoutingNetlink netlink;
    const auto route = desired_route();
    netlink.routes.push_back(dumped(route));
    RoutingReconciler reconciler(netlink);

    reconciler.reconcile({route}, {});

    CHECK(netlink.added_routes.empty());
    CHECK(netlink.deleted_routes.empty());
}

TEST_CASE("RoutingReconciler adds missing route and deletes only owned extras") {
    FakeRoutingNetlink netlink;
    const auto route = desired_route();
    auto owned_extra = route;
    owned_extra.destination = "192.0.2.0/24";
    auto foreign_extra = owned_extra;
    foreign_extra.protocol = 4;
    netlink.routes = {dumped(owned_extra), dumped(foreign_extra)};
    RoutingReconciler reconciler(netlink);

    reconciler.reconcile({route}, {});

    REQUIRE(netlink.added_routes.size() == 1);
    CHECK(netlink.added_routes.front().destination == "default");
    REQUIRE(netlink.deleted_routes.size() == 1);
    CHECK(netlink.deleted_routes.front().destination == "192.0.2.0/24");
}

TEST_CASE("RoutingReconciler refuses conflicting foreign route before mutation") {
    FakeRoutingNetlink netlink;
    const auto route = desired_route();
    auto foreign = dumped(route);
    foreign.protocol = 4;
    netlink.routes.push_back(foreign);
    RoutingReconciler reconciler(netlink);

    CHECK_THROWS_WITH_AS(reconciler.reconcile({route}, {}),
                         doctest::Contains("Foreign route conflicts"), NetlinkError);
    CHECK(netlink.added_routes.empty());
    CHECK(netlink.deleted_routes.empty());
}

TEST_CASE("RoutingReconciler propagates owned route deletion failures") {
    FakeRoutingNetlink netlink;
    auto extra = desired_route();
    extra.destination = "192.0.2.0/24";
    netlink.routes.push_back(dumped(extra));
    netlink.fail_route_delete = true;
    RoutingReconciler reconciler(netlink);

    CHECK_THROWS_WITH(reconciler.reconcile({desired_route()}, {}), "route delete failed");
}

TEST_CASE("RoutingReconciler removes stale rules only from active mark namespace") {
    FakeRoutingNetlink netlink;
    RuleSpec desired{10, 0xff, 100, 1000, AF_INET};
    netlink.rules = {{1001, 10, 0xff, 101, AF_INET}, {1002, 11, 0xff, 102, AF_INET}};
    RoutingReconciler reconciler(netlink);

    reconciler.reconcile({}, {desired});

    REQUIRE(netlink.added_rules.size() == 1);
    CHECK(netlink.added_rules.front().second == AF_INET);
    REQUIRE(netlink.deleted_rules.size() == 1);
    CHECK(netlink.deleted_rules.front().first.priority == 1001);
}

} // namespace keen_pbr3
