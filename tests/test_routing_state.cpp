#include <doctest/doctest.h>

#include "../src/config/config.hpp"
#include "../src/config/routing_state.hpp"
#include "../src/routing/netlink.hpp"
#include "../src/routing/policy_rule.hpp"
#include "../src/routing/route_table.hpp"

using namespace keen_pbr3;

namespace {

Config parse_minimal_config(const std::string& json) {
    return parse_config(json);
}

const RouteSpec* find_route(const std::vector<RouteSpec>& routes,
                            uint32_t table,
                            bool blackhole,
                            bool unreachable,
                            uint32_t metric = 0,
                            std::optional<std::string> iface = std::nullopt) {
    for (const auto& route : routes) {
        if (route.table == table &&
            route.blackhole == blackhole &&
            route.unreachable == unreachable &&
            route.metric == metric &&
            route.interface == iface) {
            return &route;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("populate_routing_state: strict enforcement installs unreachable default when down") {
    auto cfg = parse_minimal_config(R"({
        "daemon":{"strict_enforcement":true},
        "outbounds":[
            {"tag":"vpn","type":"interface","interface":"wg0","gateway":"10.8.0.1"}
        ]
    })");
    auto marks = allocate_outbound_marks(cfg.fwmark.value_or(FwmarkConfig{}),
                                         cfg.outbounds.value_or(std::vector<Outbound>{}));

    NetlinkManager netlink;
    RouteTable routes(netlink, true);
    PolicyRuleManager rules(netlink, true);

    populate_routing_state(cfg, marks, routes, rules, [](const Outbound&) {
        return false;
    });

    REQUIRE(routes.get_routes().size() == 1);
    CHECK(find_route(routes.get_routes(), 100, false, true, 1000) != nullptr);
    CHECK(find_route(routes.get_routes(), 100, true, false) == nullptr);
}

TEST_CASE("populate_routing_state: strict enforcement installs real default when up") {
    auto cfg = parse_minimal_config(R"({
        "daemon":{"strict_enforcement":true},
        "outbounds":[
            {"tag":"vpn","type":"interface","interface":"wg0","gateway":"10.8.0.1"}
        ]
    })");
    auto marks = allocate_outbound_marks(cfg.fwmark.value_or(FwmarkConfig{}),
                                         cfg.outbounds.value_or(std::vector<Outbound>{}));

    NetlinkManager netlink;
    RouteTable routes(netlink, true);
    PolicyRuleManager rules(netlink, true);

    populate_routing_state(cfg, marks, routes, rules, [](const Outbound&) {
        return true;
    });

    REQUIRE(routes.get_routes().size() == 2);
    const RouteSpec* default_route = find_route(routes.get_routes(), 100, false, false, 0, std::optional<std::string>{"wg0"});
    REQUIRE(default_route != nullptr);
    CHECK(default_route->interface == std::optional<std::string>{"wg0"});
    CHECK(default_route->gateway == std::optional<std::string>{"10.8.0.1"});
    CHECK(find_route(routes.get_routes(), 100, false, true, 1000) != nullptr);
}

TEST_CASE("populate_routing_state: outbound false overrides daemon true") {
    auto cfg = parse_minimal_config(R"({
        "daemon":{"strict_enforcement":true},
        "outbounds":[
            {"tag":"vpn","type":"interface","interface":"wg0","gateway":"10.8.0.1","strict_enforcement":false}
        ]
    })");
    auto marks = allocate_outbound_marks(cfg.fwmark.value_or(FwmarkConfig{}),
                                         cfg.outbounds.value_or(std::vector<Outbound>{}));

    NetlinkManager netlink;
    RouteTable routes(netlink, true);
    PolicyRuleManager rules(netlink, true);

    populate_routing_state(cfg, marks, routes, rules, [](const Outbound&) {
        return false;
    });

    REQUIRE(routes.get_routes().size() == 1);
    CHECK(find_route(routes.get_routes(), 100, false, false, 0, std::optional<std::string>{"wg0"}) != nullptr);
    CHECK(find_route(routes.get_routes(), 100, false, true) == nullptr);
}

TEST_CASE("populate_routing_state: outbound true overrides daemon false") {
    auto cfg = parse_minimal_config(R"({
        "daemon":{"strict_enforcement":false},
        "outbounds":[
            {"tag":"vpn","type":"interface","interface":"wg0","gateway":"10.8.0.1","strict_enforcement":true}
        ]
    })");
    auto marks = allocate_outbound_marks(cfg.fwmark.value_or(FwmarkConfig{}),
                                         cfg.outbounds.value_or(std::vector<Outbound>{}));

    NetlinkManager netlink;
    RouteTable routes(netlink, true);
    PolicyRuleManager rules(netlink, true);

    populate_routing_state(cfg, marks, routes, rules, [](const Outbound&) {
        return false;
    });

    REQUIRE(routes.get_routes().size() == 1);
    CHECK(find_route(routes.get_routes(), 100, false, true, 1000) != nullptr);
}

TEST_CASE("populate_routing_state: strict urltest installs selected primary, weighted fallbacks, and unreachable terminal") {
    auto cfg = parse_minimal_config(R"({
        "daemon":{"strict_enforcement":false},
        "outbounds":[
            {"tag":"vpn1","type":"interface","interface":"wg1","gateway":"10.0.1.1"},
            {"tag":"vpn2","type":"interface","interface":"wg2","gateway":"10.0.2.1"},
            {"tag":"wan1","type":"interface","interface":"eth0","gateway":"192.168.1.1"},
            {"tag":"wan2","type":"interface","interface":"eth1","gateway":"192.168.2.1"},
            {"tag":"auto","type":"urltest","url":"http://example.com",
             "strict_enforcement":true,
             "outbound_groups":[
                 {"weight":1,"outbounds":["vpn1","vpn2"]},
                 {"weight":2,"outbounds":["wan1","wan2"]}
             ]}
        ]
    })");
    auto marks = allocate_outbound_marks(cfg.fwmark.value_or(FwmarkConfig{}),
                                         cfg.outbounds.value_or(std::vector<Outbound>{}));

    std::map<std::string, std::string> selections{{"auto", "vpn2"}};

    NetlinkManager netlink;
    RouteTable routes(netlink, true);
    PolicyRuleManager rules(netlink, true);

    populate_routing_state(
        cfg,
        marks,
        routes,
        rules,
        [](const Outbound&) { return true; },
        &selections);

    REQUIRE(routes.get_routes().size() == 10);
    CHECK(find_route(routes.get_routes(), 104, false, false, 0, std::optional<std::string>{"wg2"}) != nullptr);
    CHECK(find_route(routes.get_routes(), 104, false, false, 1, std::optional<std::string>{"wg1"}) != nullptr);
    CHECK(find_route(routes.get_routes(), 104, false, false, 2, std::optional<std::string>{"wg2"}) != nullptr);
    CHECK(find_route(routes.get_routes(), 104, false, false, 3, std::optional<std::string>{"eth0"}) != nullptr);
    CHECK(find_route(routes.get_routes(), 104, false, false, 4, std::optional<std::string>{"eth1"}) != nullptr);
    CHECK(find_route(routes.get_routes(), 104, false, true, 1000) != nullptr);
}

TEST_CASE("populate_routing_state: strict urltest skips unreachable children") {
    auto cfg = parse_minimal_config(R"({
        "daemon":{"strict_enforcement":false},
        "outbounds":[
            {"tag":"vpn1","type":"interface","interface":"wg1","gateway":"10.0.1.1"},
            {"tag":"vpn2","type":"interface","interface":"wg2","gateway":"10.0.2.1"},
            {"tag":"auto","type":"urltest","url":"http://example.com",
             "strict_enforcement":true,
             "outbound_groups":[{"weight":1,"outbounds":["vpn1","vpn2"]}]}
        ]
    })");
    auto marks = allocate_outbound_marks(cfg.fwmark.value_or(FwmarkConfig{}),
                                         cfg.outbounds.value_or(std::vector<Outbound>{}));

    std::map<std::string, std::string> selections{{"auto", "vpn2"}};

    NetlinkManager netlink;
    RouteTable routes(netlink, true);
    PolicyRuleManager rules(netlink, true);

    populate_routing_state(
        cfg,
        marks,
        routes,
        rules,
        [](const Outbound& ob) { return ob.tag != "vpn1"; },
        &selections);

    REQUIRE(routes.get_routes().size() == 5);
    CHECK(find_route(routes.get_routes(), 102, false, false, 0, std::optional<std::string>{"wg2"}) != nullptr);
    CHECK(find_route(routes.get_routes(), 102, false, false, 1, std::optional<std::string>{"wg2"}) != nullptr);
    CHECK(find_route(routes.get_routes(), 102, false, false, 1, std::optional<std::string>{"wg1"}) == nullptr);
    CHECK(find_route(routes.get_routes(), 102, false, true, 1000) != nullptr);
}

TEST_CASE("populate_routing_state: non-strict urltest has no terminal fallback route") {
    auto cfg = parse_minimal_config(R"({
        "daemon":{"strict_enforcement":false},
        "outbounds":[
            {"tag":"vpn1","type":"interface","interface":"wg1","gateway":"10.0.1.1"},
            {"tag":"vpn2","type":"interface","interface":"wg2","gateway":"10.0.2.1"},
            {"tag":"auto","type":"urltest","url":"http://example.com",
             "outbound_groups":[{"weight":1,"outbounds":["vpn1","vpn2"]}]}
        ]
    })");
    auto marks = allocate_outbound_marks(cfg.fwmark.value_or(FwmarkConfig{}),
                                         cfg.outbounds.value_or(std::vector<Outbound>{}));

    std::map<std::string, std::string> selections{{"auto", "vpn2"}};

    NetlinkManager netlink;
    RouteTable routes(netlink, true);
    PolicyRuleManager rules(netlink, true);

    populate_routing_state(
        cfg,
        marks,
        routes,
        rules,
        [](const Outbound&) { return true; },
        &selections);

    CHECK(find_route(routes.get_routes(), 102, false, true, 1000) == nullptr);
}
