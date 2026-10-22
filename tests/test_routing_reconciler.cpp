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
        return route_add_result;
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
    std::vector<DumpedRoute> dump_routes(int = 0) override { return routes; }
    std::vector<DumpedRule> dump_policy_rules(int = 0) override { return rules; }

    std::vector<DumpedRoute> routes;
    std::vector<DumpedRule> rules;
    std::vector<RouteSpec> added_routes;
    std::vector<RouteSpec> deleted_routes;
    std::vector<std::pair<RuleSpec, int>> added_rules;
    std::vector<std::pair<RuleSpec, int>> deleted_rules;
    bool fail_route_delete{false};
    RouteAddResult route_add_result{RouteAddResult::Created};
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

TEST_CASE("RoutingReconciler replaces stale unreachable route before adding unicast") {
    FakeRoutingNetlink netlink;
    RouteSpec desired;
    desired.destination = "default";
    desired.table = 402;
    desired.interface = "vpn_servitro";
    desired.family = AF_INET6;

    auto stale = desired;
    stale.interface.reset();
    stale.unreachable = true;
    stale.metric = 65535;
    netlink.routes.push_back(dumped(stale));
    RoutingReconciler reconciler(netlink);

    reconciler.reconcile({desired}, {});

    REQUIRE(netlink.deleted_routes.size() == 1);
    CHECK(netlink.deleted_routes.front().unreachable);
    CHECK(netlink.deleted_routes.front().family == AF_INET6);
    REQUIRE(netlink.added_routes.size() == 1);
    CHECK(netlink.added_routes.front().interface ==
          std::optional<std::string>{"vpn_servitro"});
    CHECK_FALSE(netlink.added_routes.front().unreachable);
}

TEST_CASE("RoutingReconciler rejects false already-present result") {
    FakeRoutingNetlink netlink;
    const auto route = desired_route();
    netlink.route_add_result = RouteAddResult::AlreadyPresent;
    RoutingReconciler reconciler(netlink);

    CHECK_THROWS_WITH_AS(
        reconciler.reconcile({route}, {}),
        doctest::Contains("desired route is absent in table 100"),
        NetlinkError);
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

TEST_CASE("RoutingReconciler removes leftovers of the previous generation") {
    FakeRoutingNetlink netlink;
    RuleSpec desired{0x140000, 0xff0000, 407, 419, AF_INET};
    RuleSpec previous{0x120000, 0xff0000, 407, 419, AF_INET};
    netlink.rules = {{419, 0x120000, 0xff0000, 407, AF_INET},
                     {419, 0x140000, 0xff0000, 407, AF_INET}};
    RoutingReconciler reconciler(netlink);

    reconciler.reconcile({}, {desired}, {previous});

    CHECK(netlink.added_rules.empty());
    REQUIRE(netlink.deleted_rules.size() == 1);
    CHECK(netlink.deleted_rules.front().first.fwmark == 0x120000);
}

TEST_CASE("RoutingReconciler removes unclaimed rules only inside the namespace") {
    FakeRoutingNetlink netlink;
    RuleNamespace ns;
    ns.priority_first = 401;
    ns.priority_last = 910;
    ns.fwmask = 0xff0000;
    ns.mark_first = 0x10000;
    ns.table_first = 401;
    ns.table_last = 670;
    ns.tables = {200};
    RuleSpec desired{0x140000, 0xff0000, 407, 419, AF_INET};
    netlink.rules = {
        {419, 0x140000, 0xff0000, 407, AF_INET},
        {420, 0x120000, 0xff0000, 0, AF_INET6, RuleAction::unreachable},  // stale guard
        {421, 0x120000, 0xff0000, 200, AF_INET},       // stale table outbound lookup
        {5270, 0x120000, 0xff0000, 407, AF_INET},      // priority outside the block
        {422, 0x120000, 0xffffff, 407, AF_INET},       // different mask
        {423, 0x120000, 0xff0000, 52, AF_INET},        // foreign table
        {424, 0x120001, 0xff0000, 407, AF_INET},       // mark outside fwmark.mask
    };
    RoutingReconciler reconciler(netlink);

    reconciler.reconcile({}, {desired}, {}, ns);

    REQUIRE(netlink.deleted_rules.size() == 2);
    CHECK(netlink.deleted_rules[0].first.priority == 420);
    CHECK(netlink.deleted_rules[0].second == AF_INET6);
    CHECK(netlink.deleted_rules[1].first.priority == 421);
}

namespace {

DumpedRoute stale_default(uint32_t table, uint8_t protocol = KEEN_PBR_GENERATED_ROUTE_PROTOCOL) {
    RouteSpec route;
    route.destination = "default";
    route.table = table;
    route.interface = "phy1-sta0";
    route.gateway = "192.168.8.1";
    route.family = AF_INET;
    route.protocol = protocol;
    return dumped(route);
}

RuleNamespace table_range_namespace() {
    RuleNamespace ns;
    ns.fwmask = 0xff0000;
    ns.mark_first = 0x10000;
    ns.table_first = 151;
    ns.table_last = 160;
    ns.tables = {200};
    return ns;
}

} // namespace

TEST_CASE("RoutingReconciler prunes a previously applied table that has no desired routes") {
    FakeRoutingNetlink netlink;
    netlink.routes = {stale_default(151), stale_default(152)};
    RoutingReconciler reconciler(netlink);

    reconciler.reconcile({desired_route()}, {}, {}, {}, {151});

    REQUIRE(netlink.deleted_routes.size() == 1);
    CHECK(netlink.deleted_routes.front().table == 151);
}

TEST_CASE("RoutingReconciler ignores tables it neither applied nor owns by range") {
    FakeRoutingNetlink netlink;
    netlink.routes = {stale_default(151)};
    RoutingReconciler reconciler(netlink);

    reconciler.reconcile({desired_route()}, {});

    CHECK(netlink.deleted_routes.empty());
}

TEST_CASE("RoutingReconciler first apply prunes generated routes in the table range") {
    FakeRoutingNetlink netlink;
    netlink.routes = {stale_default(151), stale_default(200), stale_default(152, 4),
                      stale_default(300), stale_default(161)};
    RoutingReconciler reconciler(netlink);

    reconciler.reconcile({desired_route()}, {}, {}, table_range_namespace());

    REQUIRE(netlink.deleted_routes.size() == 2);
    CHECK(netlink.deleted_routes[0].table == 151);
    CHECK(netlink.deleted_routes[1].table == 200);
}

TEST_CASE("RoutingReconciler never deletes a foreign-protocol route in an inspected table") {
    FakeRoutingNetlink netlink;
    netlink.routes = {stale_default(151, 4), stale_default(152, 3)};
    RoutingReconciler reconciler(netlink);

    reconciler.reconcile({}, {}, {}, table_range_namespace(), {151, 152});

    CHECK(netlink.deleted_routes.empty());
}

TEST_CASE("RoutingReconciler with nothing desired clears every tracked table") {
    FakeRoutingNetlink netlink;
    netlink.routes = {stale_default(151), stale_default(153)};
    RoutingReconciler reconciler(netlink);

    reconciler.reconcile({}, {}, {}, {}, {151, 153});

    REQUIRE(netlink.deleted_routes.size() == 2);
    CHECK(netlink.rules.empty());
    CHECK(netlink.deleted_rules.empty());
}

} // namespace keen_pbr3
