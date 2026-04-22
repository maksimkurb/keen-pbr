#include <doctest/doctest.h>

#include "../src/config/config.hpp"
#include "../src/config/routing_state.hpp"
#include "../src/routing/netlink.hpp"
#include "../src/routing/policy_rule.hpp"
#include "../src/routing/route_table.hpp"

using namespace keen_pbr3;

namespace {

Config parse_minimal_config(const std::string& json) {
    Config cfg = parse_config(json);
    if (!cfg.dns.has_value()) {
        cfg.dns = DnsConfig{};
    }
    if (!cfg.dns->servers.has_value()) {
        DnsServer fallback_server;
        fallback_server.tag = "default_dns";
        fallback_server.address = "127.0.0.1";
        cfg.dns->servers = std::vector<DnsServer>{fallback_server};
    }
    if (!cfg.dns->fallback.has_value()) {
        cfg.dns->fallback = std::vector<std::string>{"default_dns"};
    }
    if (!cfg.dns->system_resolver.has_value()) {
        api::SystemResolver resolver;
        resolver.address = "127.0.0.1";
        cfg.dns->system_resolver = resolver;
    }
    validate_config(cfg);
    return cfg;
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

size_t count_routes_in_table(const std::vector<RouteSpec>& routes, uint32_t table) {
    return static_cast<size_t>(std::count_if(routes.begin(),
                                             routes.end(),
                                             [table](const RouteSpec& route) {
                                                 return route.table == table;
                                             }));
}

} // namespace

TEST_CASE("build_fw_rule_states: ignore outbound becomes pass-through firewall rule") {
    auto cfg = parse_minimal_config(R"({
        "outbounds":[
            {"tag":"direct","type":"ignore"}
        ],
        "lists":{
            "local":{"ip_cidrs":["192.168.0.0/16"]}
        },
        "route":{
            "rules":[
                {"list":["local"],"outbound":"direct"}
            ]
        }
    })");

    auto marks = allocate_outbound_marks(cfg.fwmark.value_or(FwmarkConfig{}),
                                         cfg.outbounds.value_or(std::vector<Outbound>{}));
    auto states = build_fw_rule_states(cfg, marks);

    REQUIRE(states.size() == 1);
    CHECK(states[0].action_type == RuleActionType::Pass);
    CHECK(states[0].set_names == std::vector<std::string>({
        "kpbr4_local", "kpbr6_local", "kpbr4d_local", "kpbr6d_local"
    }));
}

TEST_CASE("build_fw_rule_states: disabled route rule is skipped while enabled rules stay active") {
    auto cfg = parse_minimal_config(R"({
        "outbounds":[
            {"tag":"direct","type":"ignore"}
        ],
        "lists":{
            "disabled_list":{"ip_cidrs":["192.168.10.0/24"]},
            "enabled_list":{"ip_cidrs":["192.168.20.0/24"]}
        },
        "route":{
            "rules":[
                {"enabled":false,"list":["disabled_list"],"outbound":"direct"},
                {"list":["enabled_list"],"outbound":"direct"}
            ]
        }
    })");

    auto marks = allocate_outbound_marks(cfg.fwmark.value_or(FwmarkConfig{}),
                                         cfg.outbounds.value_or(std::vector<Outbound>{}));
    auto states = build_fw_rule_states(cfg, marks);

    REQUIRE(states.size() == 2);
    CHECK(states[0].action_type == RuleActionType::Skip);
    CHECK(states[0].set_names.empty());
    CHECK(states[0].outbound_tag == "direct");
    CHECK(states[1].action_type == RuleActionType::Pass);
    CHECK(states[1].set_names == std::vector<std::string>({
        "kpbr4_enabled_list", "kpbr6_enabled_list", "kpbr4d_enabled_list", "kpbr6d_enabled_list"
    }));
}

TEST_CASE("build_firewall_global_prefilter: missing inbound_interfaces keeps interface restriction disabled") {
    auto cfg = parse_minimal_config(R"({
        "outbounds":[
            {"tag":"wan","type":"interface","interface":"eth0","gateway":"192.0.2.1"}
        ],
        "lists":{
            "local":{"ip_cidrs":["192.168.0.0/16"]}
        },
        "route":{
            "rules":[
                {"list":["local"],"outbound":"wan"}
            ]
        }
    })");

    const auto prefilter = build_firewall_global_prefilter(cfg);
    CHECK(prefilter.skip_established_or_dnat);
    CHECK(prefilter.skip_marked_packets);
    CHECK_FALSE(prefilter.has_inbound_interfaces());
    CHECK_FALSE(prefilter.inbound_interfaces.has_value());
}

TEST_CASE("build_firewall_global_prefilter: empty inbound_interfaces keeps interface restriction disabled") {
    auto cfg = parse_minimal_config(R"({
        "outbounds":[
            {"tag":"wan","type":"interface","interface":"eth0","gateway":"192.0.2.1"}
        ],
        "lists":{
            "local":{"ip_cidrs":["192.168.0.0/16"]}
        },
        "route":{
            "inbound_interfaces":[],
            "rules":[
                {"list":["local"],"outbound":"wan"}
            ]
        }
    })");

    const auto prefilter = build_firewall_global_prefilter(cfg);
    CHECK(prefilter.skip_established_or_dnat);
    CHECK(prefilter.skip_marked_packets);
    CHECK_FALSE(prefilter.has_inbound_interfaces());
    CHECK_FALSE(prefilter.inbound_interfaces.has_value());
}

TEST_CASE("build_firewall_global_prefilter: inbound_interfaces enables interface restriction") {
    auto cfg = parse_minimal_config(R"({
        "outbounds":[
            {"tag":"wan","type":"interface","interface":"eth0","gateway":"192.0.2.1"}
        ],
        "lists":{
            "local":{"ip_cidrs":["192.168.0.0/16"]}
        },
        "route":{
            "inbound_interfaces":["br0","wg0"],
            "rules":[
                {"list":["local"],"outbound":"wan"}
            ]
        }
    })");

    const auto prefilter = build_firewall_global_prefilter(cfg);
    CHECK(prefilter.skip_established_or_dnat);
    CHECK(prefilter.skip_marked_packets);
    REQUIRE(prefilter.inbound_interfaces.has_value());
    CHECK(prefilter.has_inbound_interfaces());
    CHECK(*prefilter.inbound_interfaces == std::vector<std::string>({"br0", "wg0"}));
}

TEST_CASE("build_firewall_global_prefilter: daemon.skip_marked_packets false disables marked-packet bypass") {
    auto cfg = parse_minimal_config(R"({
        "daemon":{"skip_marked_packets":false},
        "outbounds":[
            {"tag":"wan","type":"interface","interface":"eth0","gateway":"192.0.2.1"}
        ],
        "lists":{
            "local":{"ip_cidrs":["192.168.0.0/16"]}
        },
        "route":{
            "rules":[
                {"list":["local"],"outbound":"wan"}
            ]
        }
    })");

    const auto prefilter = build_firewall_global_prefilter(cfg);
    CHECK(prefilter.skip_established_or_dnat);
    CHECK_FALSE(prefilter.skip_marked_packets);
}

TEST_CASE("build_firewall_global_prefilter: daemon.skip_marked_packets null keeps marked-packet bypass enabled") {
    auto cfg = parse_minimal_config(R"({
        "daemon":{"skip_marked_packets":null},
        "outbounds":[
            {"tag":"wan","type":"interface","interface":"eth0","gateway":"192.0.2.1"}
        ],
        "lists":{
            "local":{"ip_cidrs":["192.168.0.0/16"]}
        },
        "route":{
            "rules":[
                {"list":["local"],"outbound":"wan"}
            ]
        }
    })");

    const auto prefilter = build_firewall_global_prefilter(cfg);
    CHECK(prefilter.skip_established_or_dnat);
    CHECK(prefilter.skip_marked_packets);
}

TEST_CASE("build_fw_rule_states: urltest selection to blackhole becomes drop rule") {
    auto cfg = parse_minimal_config(R"({
        "outbounds":[
            {"tag":"bh","type":"blackhole"},
            {"tag":"wan","type":"interface","interface":"eth0","gateway":"192.0.2.1"},
            {"tag":"auto","type":"urltest","url":"http://example.com",
             "outbound_groups":[{"outbounds":["wan","bh"]}]}
        ],
        "lists":{
            "local":{"ip_cidrs":["192.168.0.0/16"]}
        },
        "route":{
            "rules":[
                {"list":["local"],"outbound":"auto"}
            ]
        }
    })");

    auto marks = allocate_outbound_marks(cfg.fwmark.value_or(FwmarkConfig{}),
                                         cfg.outbounds.value_or(std::vector<Outbound>{}));
    std::map<std::string, std::string> selections{{"auto", "bh"}};
    auto states = build_fw_rule_states(cfg, marks, &selections);

    REQUIRE(states.size() == 1);
    CHECK(states[0].action_type == RuleActionType::Drop);
}

TEST_CASE("build_fw_rule_states: selector-only route rule without list still becomes mark rule") {
    auto cfg = parse_minimal_config(R"({
        "outbounds":[
            {"tag":"cloudflare","type":"interface","interface":"wg0","gateway":"10.0.0.1"}
        ],
        "route":{
            "rules":[
                {"dest_port":"443,80","outbound":"cloudflare"}
            ]
        }
    })");

    auto marks = allocate_outbound_marks(cfg.fwmark.value_or(FwmarkConfig{}),
                                         cfg.outbounds.value_or(std::vector<Outbound>{}));
    auto states = build_fw_rule_states(cfg, marks);

    REQUIRE(states.size() == 1);
    CHECK(states[0].action_type == RuleActionType::Mark);
    CHECK(states[0].set_names.empty());
    CHECK(states[0].outbound_tag == "cloudflare");
    CHECK(states[0].fwmark != 0);
}

TEST_CASE("prune_fw_rule_states_to_realized_sets: removes nonexistent pass-through set variants") {
    auto cfg = parse_minimal_config(R"({
        "outbounds":[
            {"tag":"direct","type":"ignore"}
        ],
        "lists":{
            "local":{"ip_cidrs":["192.168.0.0/16"]}
        },
        "route":{
            "rules":[
                {"list":["local"],"outbound":"direct"}
            ]
        }
    })");

    auto marks = allocate_outbound_marks(cfg.fwmark.value_or(FwmarkConfig{}),
                                         cfg.outbounds.value_or(std::vector<Outbound>{}));
    auto states = build_fw_rule_states(cfg, marks);

    prune_fw_rule_states_to_realized_sets(
        cfg,
        states,
        [](const std::string&, const ListConfig&) {
            ListSetUsage usage;
            usage.has_static_entries = true;
            usage.has_domain_entries = false;
            return usage;
        });

    REQUIRE(states.size() == 1);
    CHECK(states[0].action_type == RuleActionType::Pass);
    CHECK(states[0].set_names == std::vector<std::string>({
        "kpbr4_local", "kpbr6_local"
    }));
}

TEST_CASE("populate_routing_state: strict enforcement installs unreachable default when down") {
    auto cfg = parse_minimal_config(R"({
        "iproute":{"table_start":100},
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
        "iproute":{"table_start":100},
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

TEST_CASE("populate_routing_state: unreachable interface outbound remains unavailable when strict is disabled") {
    auto cfg = parse_minimal_config(R"({
        "iproute":{"table_start":100},
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

    CHECK(routes.get_routes().empty());
    CHECK(find_route(routes.get_routes(), 100, false, true) == nullptr);
    REQUIRE(rules.get_rules().size() == 1);
    CHECK(rules.get_rules()[0].table == 100);
}

TEST_CASE("populate_routing_state: outbound true overrides daemon false") {
    auto cfg = parse_minimal_config(R"({
        "iproute":{"table_start":100},
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
        "iproute":{"table_start":100},
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
        "iproute":{"table_start":100},
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

    REQUIRE(routes.get_routes().size() == 4);
    CHECK(find_route(routes.get_routes(), 102, false, false, 0, std::optional<std::string>{"wg2"}) != nullptr);
    CHECK(find_route(routes.get_routes(), 102, false, false, 1, std::optional<std::string>{"wg2"}) != nullptr);
    CHECK(find_route(routes.get_routes(), 102, false, false, 1, std::optional<std::string>{"wg1"}) == nullptr);
    CHECK(find_route(routes.get_routes(), 102, false, true, 1000) != nullptr);
}

TEST_CASE("populate_routing_state: urltest without completed probe does not install child routes") {
    auto cfg = parse_minimal_config(R"({
        "iproute":{"table_start":100},
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

    NetlinkManager netlink;
    RouteTable routes(netlink, true);
    PolicyRuleManager rules(netlink, true);

    populate_routing_state(
        cfg,
        marks,
        routes,
        rules,
        [](const Outbound&) { return true; },
        nullptr);

    CHECK(count_routes_in_table(routes.get_routes(), 102) == 0);
    REQUIRE(rules.get_rules().size() == 3);
    CHECK(rules.get_rules()[2].table == 102);
}

TEST_CASE("populate_routing_state: strict urltest without completed probe keeps only terminal unreachable route") {
    auto cfg = parse_minimal_config(R"({
        "iproute":{"table_start":100},
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

    NetlinkManager netlink;
    RouteTable routes(netlink, true);
    PolicyRuleManager rules(netlink, true);

    populate_routing_state(
        cfg,
        marks,
        routes,
        rules,
        [](const Outbound&) { return true; },
        nullptr);

    REQUIRE(count_routes_in_table(routes.get_routes(), 102) == 1);
    CHECK(find_route(routes.get_routes(), 102, false, true, 1000) != nullptr);
}

// =============================================================================
// Reserved-table skipping
// =============================================================================

TEST_CASE("populate_routing_state: allocation skips reserved block 250-260") {
    // table_start=249: offset 0 → 249, offset 1 → 261 (250-260 skipped)
    auto cfg = parse_minimal_config(R"({
        "iproute":{"table_start":249},
        "outbounds":[
            {"tag":"vpn1","type":"interface","interface":"wg1"},
            {"tag":"vpn2","type":"interface","interface":"wg2"}
        ]
    })");
    auto marks = allocate_outbound_marks(cfg.fwmark.value_or(FwmarkConfig{}),
                                         cfg.outbounds.value_or(std::vector<Outbound>{}));

    NetlinkManager netlink;
    RouteTable routes(netlink, true);
    PolicyRuleManager rules(netlink, true);
    populate_routing_state(cfg, marks, routes, rules);

    const auto& rule_list = rules.get_rules();
    REQUIRE(rule_list.size() == 2);
    CHECK(rule_list[0].table == 249);
    CHECK(rule_list[1].table == 261); // jumped over 250-260

    for (const auto& rule : rule_list) {
        CHECK_FALSE(is_reserved_table(rule.table));
    }
}

TEST_CASE("populate_routing_state: allocation skips prelocal table 128") {
    // table_start=127: offset 0 → 127, offset 1 → 129 (128 skipped)
    auto cfg = parse_minimal_config(R"({
        "iproute":{"table_start":127},
        "outbounds":[
            {"tag":"vpn1","type":"interface","interface":"wg1"},
            {"tag":"vpn2","type":"interface","interface":"wg2"}
        ]
    })");
    auto marks = allocate_outbound_marks(cfg.fwmark.value_or(FwmarkConfig{}),
                                         cfg.outbounds.value_or(std::vector<Outbound>{}));

    NetlinkManager netlink;
    RouteTable routes(netlink, true);
    PolicyRuleManager rules(netlink, true);
    populate_routing_state(cfg, marks, routes, rules);

    const auto& rule_list = rules.get_rules();
    REQUIRE(rule_list.size() == 2);
    CHECK(rule_list[0].table == 127);
    CHECK(rule_list[1].table == 129); // jumped over 128

    for (const auto& rule : rule_list) {
        CHECK_FALSE(is_reserved_table(rule.table));
    }
}

TEST_CASE("populate_routing_state: no allocated table falls in reserved range") {
    // table_start=248 with 4 outbounds crosses both 250-260 and prelocal-adjacent area
    auto cfg = parse_minimal_config(R"({
        "iproute":{"table_start":248},
        "outbounds":[
            {"tag":"a","type":"interface","interface":"wg1"},
            {"tag":"b","type":"interface","interface":"wg2"},
            {"tag":"c","type":"interface","interface":"wg3"},
            {"tag":"d","type":"interface","interface":"wg4"}
        ]
    })");
    auto marks = allocate_outbound_marks(cfg.fwmark.value_or(FwmarkConfig{}),
                                         cfg.outbounds.value_or(std::vector<Outbound>{}));

    NetlinkManager netlink;
    RouteTable routes(netlink, true);
    PolicyRuleManager rules(netlink, true);
    populate_routing_state(cfg, marks, routes, rules);

    for (const auto& rule : rules.get_rules()) {
        CHECK_FALSE(is_reserved_table(rule.table));
    }
    for (const auto& route : routes.get_routes()) {
        CHECK_FALSE(is_reserved_table(route.table));
    }
}

TEST_CASE("populate_routing_state: non-strict urltest has no terminal fallback route") {
    auto cfg = parse_minimal_config(R"({
        "iproute":{"table_start":100},
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
