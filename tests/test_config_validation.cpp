#include <doctest/doctest.h>

#include "../src/config/config.hpp"
#include "../src/config/routing_state.hpp"

#include <nlohmann/json.hpp>
#include <string>

using namespace keen_pbr3;

namespace {

Config parse_test_config(const std::string& json_str) {
    Config cfg = parse_config(json_str);
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

} // namespace

// Helper: build a minimal valid config JSON with a single list entry.
static std::string list_config_json(const std::string& list_name,
                                    const std::string& list_body = R"({"ip_cidrs":["10.0.0.1"]})") {
    nlohmann::json config;
    config["lists"] = nlohmann::json::object();
    config["lists"][list_name] = nlohmann::json::parse(list_body);
    return config.dump();
}

static std::vector<ConfigValidationIssue> parse_issues(const std::string& json) {
    try {
        (void)parse_config(json);
        return {};
    } catch (const ConfigValidationError& e) {
        return e.issues();
    }
}

// =============================================================================
// List name: length validation
// =============================================================================

TEST_CASE("list name: exactly 24 chars is valid") {
    const std::string name(24, 'a'); // "aaaaaaaaaaaaaaaaaaaaaaaa"
    CHECK_NOTHROW(parse_test_config(list_config_json(name)));
}

TEST_CASE("list name: 25 chars is rejected") {
    const std::string name(25, 'a');
    CHECK_THROWS_AS(parse_test_config(list_config_json(name)), ConfigError);
}

TEST_CASE("list name: 1 char is valid") {
    CHECK_NOTHROW(parse_test_config(list_config_json("a")));
}

TEST_CASE("list name: empty string is rejected") {
    // JSON object key "" is valid JSON but must be rejected by our validation.
    const std::string json = R"({"lists":{"":{"ip_cidrs":["10.0.0.1"]}}})";
    CHECK_THROWS_AS(parse_test_config(json), ConfigError);
}

// =============================================================================
// List name: character set validation
// =============================================================================

TEST_CASE("list name: lowercase letters only is valid") {
    CHECK_NOTHROW(parse_test_config(list_config_json("mylist")));
}

TEST_CASE("list name: uppercase letters are rejected") {
    CHECK_THROWS_AS(parse_test_config(list_config_json("MyList")), ConfigError);
}

TEST_CASE("list name: uppercase first char is rejected") {
    CHECK_THROWS_AS(parse_test_config(list_config_json("Mylist")), ConfigError);
}

TEST_CASE("list name: mixed case + digits + underscore is rejected") {
    CHECK_THROWS_AS(parse_test_config(list_config_json("My_List01")), ConfigError);
}

TEST_CASE("list name: lowercase + digits + underscore is valid") {
    CHECK_NOTHROW(parse_test_config(list_config_json("my_list01")));
}

TEST_CASE("list name: first char digit is rejected") {
    CHECK_THROWS_AS(parse_test_config(list_config_json("1list")), ConfigError);
}

TEST_CASE("list name: first char underscore is rejected") {
    CHECK_THROWS_AS(parse_test_config(list_config_json("_list")), ConfigError);
}

TEST_CASE("list name: hyphen in name is rejected") {
    CHECK_THROWS_AS(parse_test_config(list_config_json("my-list")), ConfigError);
}

TEST_CASE("list name: space in name is rejected") {
    CHECK_THROWS_AS(parse_test_config(list_config_json("my list")), ConfigError);
}

TEST_CASE("list name: dot in name is rejected") {
    CHECK_THROWS_AS(parse_test_config(list_config_json("my.list")), ConfigError);
}

// =============================================================================
// DNS server detour validation
// =============================================================================

static const std::string kDnsDetourBase = R"({
    "outbounds": [
        {"tag": "vpn", "type": "interface", "interface": "wg0"},
        {"tag": "vpn_table", "type": "table", "table": 100},
        {"tag": "urltest1", "type": "urltest", "url": "http://example.com",
         "outbound_groups": [{"outbounds": ["vpn"]}]},
        {"tag": "blackhole1", "type": "blackhole"},
        {"tag": "ignore1", "type": "ignore"}
    ]
})";

TEST_CASE("dns detour: valid interface outbound") {
    std::string json = R"({"outbounds":[{"tag":"vpn","type":"interface","interface":"wg0"}],
        "dns":{"servers":[{"tag":"vpn_dns","address":"10.8.0.1","detour":"vpn"}],"fallback":["vpn_dns"]}})";
    CHECK_NOTHROW(parse_test_config(json));
}

TEST_CASE("dns detour: valid table outbound") {
    std::string json = R"({"outbounds":[{"tag":"tbl","type":"table","table":100}],
        "dns":{"servers":[{"tag":"tbl_dns","address":"10.8.0.2","detour":"tbl"}],"fallback":["tbl_dns"]}})";
    CHECK_NOTHROW(parse_test_config(json));
}

TEST_CASE("dns detour: valid urltest outbound") {
    std::string json = R"({"outbounds":[
        {"tag":"vpn","type":"interface","interface":"wg0"},
        {"tag":"ut","type":"urltest","url":"http://example.com","outbound_groups":[{"outbounds":["vpn"]}]}
    ],"dns":{"servers":[{"tag":"ut_dns","address":"10.8.0.3","detour":"ut"}],"fallback":["ut_dns"]}})";
    CHECK_NOTHROW(parse_test_config(json));
}

TEST_CASE("dns detour: unknown outbound tag is rejected") {
    std::string json = R"({"outbounds":[{"tag":"vpn","type":"interface","interface":"wg0"}],
        "dns":{"servers":[{"tag":"vpn_dns","address":"10.8.0.1","detour":"nonexistent"}],"fallback":["vpn_dns"]}})";
    CHECK_THROWS_AS(parse_test_config(json), ConfigError);
}

TEST_CASE("dns detour: blackhole outbound is rejected") {
    std::string json = R"({"outbounds":[{"tag":"bh","type":"blackhole"}],
        "dns":{"servers":[{"tag":"bh_dns","address":"10.8.0.1","detour":"bh"}],"fallback":["bh_dns"]}})";
    CHECK_THROWS_AS(parse_test_config(json), ConfigError);
}

TEST_CASE("dns detour: ignore outbound is rejected") {
    std::string json = R"({"outbounds":[{"tag":"ig","type":"ignore"}],
        "dns":{"servers":[{"tag":"ig_dns","address":"10.8.0.1","detour":"ig"}],"fallback":["ig_dns"]}})";
    CHECK_THROWS_AS(parse_test_config(json), ConfigError);
}

TEST_CASE("dns detour: no detour field is accepted") {
    std::string json = R"({"dns":{"servers":[{"tag":"plain_dns","address":"8.8.8.8"}],"fallback":["plain_dns"]}})";
    CHECK_NOTHROW(parse_test_config(json));
}

TEST_CASE("dns fallback: parser diagnostics include precise path for type error") {
    const auto issues = parse_issues(R"({"dns":{"fallback":"quad9"}})");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "$");
    CHECK(issues[0].message.find("/dns/fallback") != std::string::npos);
    CHECK(issues[0].message.find("type must be array") != std::string::npos);
}

TEST_CASE("parse_config accepts JSON comments") {
    const std::string json = R"({
        // daemon settings
        "daemon": {
            "strict_enforcement": false
        },
        /* dns settings */
        "dns": {
            "servers": [
                {"tag":"quad9","address":"9.9.9.9"}
            ],
            "fallback": ["quad9"]
        }
    })";

    CHECK_NOTHROW(parse_test_config(json));
}

TEST_CASE("dns servers: duplicate tag is rejected") {
    std::string json = R"({
        "dns":{
            "servers":[
                {"tag":"dup_dns","address":"8.8.8.8"},
                {"tag":"dup_dns","address":"1.1.1.1"}
            ],
            "fallback":["dup_dns"]
        }
    })";
    CHECK_THROWS_AS(parse_test_config(json), ConfigError);
}

TEST_CASE("route rule enabled: parse and serialize cover true false omitted and null") {
    const auto cfg_true = parse_test_config(R"({
        "route":{"rules":[{"enabled":true,"list":["ads"],"outbound":"vpn"}]}
    })");
    REQUIRE(cfg_true.route.has_value());
    REQUIRE(cfg_true.route->rules.has_value());
    REQUIRE(cfg_true.route->rules->size() == 1);
    CHECK(cfg_true.route->rules->at(0).enabled == std::optional<bool>(true));
    const nlohmann::json json_true = cfg_true;
    CHECK(json_true["route"]["rules"][0]["enabled"] == true);

    const auto cfg_false = parse_test_config(R"({
        "route":{"rules":[{"enabled":false,"list":["ads"],"outbound":"vpn"}]}
    })");
    REQUIRE(cfg_false.route.has_value());
    REQUIRE(cfg_false.route->rules.has_value());
    REQUIRE(cfg_false.route->rules->size() == 1);
    CHECK(cfg_false.route->rules->at(0).enabled == std::optional<bool>(false));
    const nlohmann::json json_false = cfg_false;
    CHECK(json_false["route"]["rules"][0]["enabled"] == false);

    const auto cfg_omitted = parse_test_config(R"({
        "route":{"rules":[{"list":["ads"],"outbound":"vpn"}]}
    })");
    REQUIRE(cfg_omitted.route.has_value());
    REQUIRE(cfg_omitted.route->rules.has_value());
    REQUIRE(cfg_omitted.route->rules->size() == 1);
    CHECK_FALSE(cfg_omitted.route->rules->at(0).enabled.has_value());
    const nlohmann::json json_omitted = cfg_omitted;
    CHECK(json_omitted["route"]["rules"][0]["enabled"].is_null());

    const auto cfg_null = parse_test_config(R"({
        "route":{"rules":[{"enabled":null,"list":["ads"],"outbound":"vpn"}]}
    })");
    REQUIRE(cfg_null.route.has_value());
    REQUIRE(cfg_null.route->rules.has_value());
    REQUIRE(cfg_null.route->rules->size() == 1);
    CHECK_FALSE(cfg_null.route->rules->at(0).enabled.has_value());
    const nlohmann::json json_null = cfg_null;
    CHECK(json_null["route"]["rules"][0]["enabled"].is_null());
}

TEST_CASE("dns rule enabled: parse and serialize cover true false omitted and null") {
    const auto cfg_true = parse_test_config(R"({
        "dns":{
            "servers":[{"tag":"vpn_dns","address":"10.8.0.1"}],
            "fallback":["vpn_dns"],
            "rules":[{"enabled":true,"list":["ads"],"server":"vpn_dns"}]
        }
    })");
    REQUIRE(cfg_true.dns.has_value());
    REQUIRE(cfg_true.dns->rules.has_value());
    REQUIRE(cfg_true.dns->rules->size() == 1);
    CHECK(cfg_true.dns->rules->at(0).enabled == std::optional<bool>(true));
    const nlohmann::json json_true = cfg_true;
    CHECK(json_true["dns"]["rules"][0]["enabled"] == true);

    const auto cfg_false = parse_test_config(R"({
        "dns":{
            "servers":[{"tag":"vpn_dns","address":"10.8.0.1"}],
            "fallback":["vpn_dns"],
            "rules":[{"enabled":false,"list":["ads"],"server":"vpn_dns"}]
        }
    })");
    REQUIRE(cfg_false.dns.has_value());
    REQUIRE(cfg_false.dns->rules.has_value());
    REQUIRE(cfg_false.dns->rules->size() == 1);
    CHECK(cfg_false.dns->rules->at(0).enabled == std::optional<bool>(false));
    const nlohmann::json json_false = cfg_false;
    CHECK(json_false["dns"]["rules"][0]["enabled"] == false);

    const auto cfg_omitted = parse_test_config(R"({
        "dns":{
            "servers":[{"tag":"vpn_dns","address":"10.8.0.1"}],
            "fallback":["vpn_dns"],
            "rules":[{"list":["ads"],"server":"vpn_dns"}]
        }
    })");
    REQUIRE(cfg_omitted.dns.has_value());
    REQUIRE(cfg_omitted.dns->rules.has_value());
    REQUIRE(cfg_omitted.dns->rules->size() == 1);
    CHECK_FALSE(cfg_omitted.dns->rules->at(0).enabled.has_value());
    const nlohmann::json json_omitted = cfg_omitted;
    CHECK(json_omitted["dns"]["rules"][0]["enabled"].is_null());

    const auto cfg_null = parse_test_config(R"({
        "dns":{
            "servers":[{"tag":"vpn_dns","address":"10.8.0.1"}],
            "fallback":["vpn_dns"],
            "rules":[{"enabled":null,"list":["ads"],"server":"vpn_dns"}]
        }
    })");
    REQUIRE(cfg_null.dns.has_value());
    REQUIRE(cfg_null.dns->rules.has_value());
    REQUIRE(cfg_null.dns->rules->size() == 1);
    CHECK_FALSE(cfg_null.dns->rules->at(0).enabled.has_value());
    const nlohmann::json json_null = cfg_null;
    CHECK(json_null["dns"]["rules"][0]["enabled"].is_null());
}

TEST_CASE("dns servers: duplicate server definition is rejected") {
    std::string json = R"({
        "dns":{
            "servers":[
                {"tag":"dns_a","address":"8.8.8.8"},
                {"tag":"dns_b","address":"8.8.8.8"}
            ],
            "fallback":["dns_a"]
        }
    })";
    CHECK_THROWS_AS(parse_test_config(json), ConfigError);
}

TEST_CASE("outbound tag: uppercase is rejected") {
    std::string json = R"({"outbounds":[{"tag":"Vpn","type":"interface","interface":"wg0"}]})";
    CHECK_THROWS_AS(parse_test_config(json), ConfigError);
}

TEST_CASE("dns tag: uppercase is rejected") {
    std::string json = R"({"dns":{"servers":[{"tag":"Dns_1","address":"8.8.8.8"}],"fallback":["Dns_1"]}})";
    CHECK_THROWS_AS(parse_test_config(json), ConfigError);
}

TEST_CASE("dns test server: valid listen parses") {
    std::string json = R"({"dns":{"dns_test_server":{"listen":"127.0.0.88:53"}}})";
    auto cfg = parse_test_config(json);
    REQUIRE(cfg.dns.has_value());
    REQUIRE(cfg.dns->dns_test_server.has_value());
    CHECK(cfg.dns->dns_test_server->listen == "127.0.0.88:53");
    CHECK(!cfg.dns->dns_test_server->answer_ipv4.has_value());
}

TEST_CASE("dns test server: explicit answer IPv4 parses") {
    std::string json = R"({"dns":{"dns_test_server":{"listen":"127.0.0.88:53","answer_ipv4":"127.0.0.99"}}})";
    auto cfg = parse_test_config(json);
    REQUIRE(cfg.dns.has_value());
    REQUIRE(cfg.dns->dns_test_server.has_value());
    CHECK(cfg.dns->dns_test_server->answer_ipv4.value_or("") == "127.0.0.99");
}

TEST_CASE("dns test server: invalid listen is rejected") {
    std::string json = R"({"dns":{"dns_test_server":{"listen":"not-an-ip:53"}}})";
    CHECK_THROWS_AS(parse_test_config(json), ConfigError);
}

TEST_CASE("dns test server: ipv6 listen is rejected") {
    std::string json = R"({"dns":{"dns_test_server":{"listen":"[::1]:53"}}})";
    CHECK_THROWS_AS(parse_test_config(json), ConfigError);
}

TEST_CASE("dns test server: invalid answer IPv4 is rejected") {
    std::string json = R"({"dns":{"dns_test_server":{"listen":"127.0.0.88:53","answer_ipv4":"example.com"}}})";
    CHECK_THROWS_AS(parse_test_config(json), ConfigError);
}

TEST_CASE("config validation: accepts system_resolver") {
    auto cfg = parse_test_config(R"({
        "dns": {
            "servers": [{"tag":"plain_dns","address":"8.8.8.8"}],
            "fallback": ["plain_dns"],
            "system_resolver": {
                "address": "127.0.0.1"
            }
        }
    })");

    CHECK_NOTHROW(validate_config(cfg));
}

TEST_CASE("config validation: rejects missing system_resolver") {
    auto cfg = parse_config(R"({
        "dns": {
            "servers": [
                {"tag":"plain_dns","address":"8.8.8.8"}
            ],
            "fallback": ["plain_dns"]
        }
    })");

    try {
        validate_config(cfg);
        FAIL("Expected ConfigValidationError");
    } catch (const ConfigValidationError& e) {
        REQUIRE(e.issues().size() == 1);
        CHECK(e.issues().front().path == "dns.system_resolver");
        CHECK(e.issues().front().message == "dns.system_resolver must be present");
    }
}

TEST_CASE("config validation: allows missing fallback") {
    auto cfg = parse_config(R"({
        "dns": {
            "servers": [{"tag":"plain_dns","address":"8.8.8.8"}],
            "system_resolver": {
                "address": "127.0.0.1"
            }
        }
    })");

    CHECK_NOTHROW(validate_config(cfg));
}

TEST_CASE("config validation: allows empty fallback array") {
    auto cfg = parse_config(R"({
        "dns": {
            "servers": [{"tag":"plain_dns","address":"8.8.8.8"}],
            "fallback": [],
            "system_resolver": {
                "address": "127.0.0.1"
            }
        }
    })");

    CHECK_NOTHROW(validate_config(cfg));
}

TEST_CASE("config validation: rejects unknown fallback tag") {
    auto cfg = parse_config(R"({
        "dns": {
            "servers": [{"tag":"plain_dns","address":"8.8.8.8"}],
            "fallback": ["missing_dns"],
            "system_resolver": {
                "address": "127.0.0.1"
            }
        }
    })");

    CHECK_THROWS_AS(validate_config(cfg), ConfigValidationError);
}

TEST_CASE("config validation: rejects duplicate fallback tag") {
    auto cfg = parse_config(R"({
        "dns": {
            "servers": [{"tag":"plain_dns","address":"8.8.8.8"}],
            "fallback": ["plain_dns", "plain_dns"],
            "system_resolver": {
                "address": "127.0.0.1"
            }
        }
    })");

    CHECK_THROWS_AS(validate_config(cfg), ConfigValidationError);
}

TEST_CASE("config validation: collects empty system_resolver fields") {
    Config cfg;
    cfg.dns = DnsConfig{};
    DnsServer fallback_server;
    fallback_server.tag = "default_dns";
    fallback_server.address = "127.0.0.1";
    cfg.dns->servers = std::vector<DnsServer>{fallback_server};
    cfg.dns->fallback = std::vector<std::string>{"default_dns"};
    api::SystemResolver resolver{};
    cfg.dns->system_resolver = resolver;

    try {
        validate_config(cfg);
        FAIL("Expected ConfigValidationError");
    } catch (const ConfigValidationError& e) {
        REQUIRE(e.issues().size() == 1);
        CHECK(e.issues()[0].path == "dns.system_resolver.address");
        CHECK(e.issues()[0].message == "dns.system_resolver.address must not be empty");
    }
}

TEST_CASE("config validation: accepts legacy system_resolver.type and ignores it") {
    auto cfg = parse_config(R"({
        "dns": {
            "servers": [{"tag":"plain_dns","address":"8.8.8.8"}],
            "fallback": ["plain_dns"],
            "system_resolver": {
                "type": "dnsmasq-ipset",
                "address": "127.0.0.1"
            }
        }
    })");

    CHECK_NOTHROW(validate_config(cfg));
    REQUIRE(cfg.dns.has_value());
    REQUIRE(cfg.dns->system_resolver.has_value());
    CHECK(cfg.dns->system_resolver->address == "127.0.0.1");
}

TEST_CASE("strict enforcement: daemon default parses") {
    std::string json = R"({"daemon":{"strict_enforcement":true}})";
    auto cfg = parse_test_config(json);
    REQUIRE(cfg.daemon.has_value());
    CHECK(cfg.daemon->strict_enforcement.value_or(false));
}

TEST_CASE("daemon max_file_size_bytes: parses and is returned") {
    std::string json = R"({"daemon":{"max_file_size_bytes":123456}})";
    auto cfg = parse_test_config(json);
    REQUIRE(cfg.daemon.has_value());
    CHECK(cfg.daemon->max_file_size_bytes.value_or(0) == 123456);
    CHECK(max_file_size_bytes(cfg) == 123456);
}

TEST_CASE("daemon max_file_size_bytes: default is 8 MiB") {
    auto cfg = parse_test_config(R"({})");
    CHECK(max_file_size_bytes(cfg) == 8 * 1024 * 1024);
}

TEST_CASE("daemon max_file_size_bytes: zero is rejected") {
    CHECK_THROWS_AS(parse_test_config(R"({"daemon":{"max_file_size_bytes":0}})"),
                    ConfigValidationError);
}

TEST_CASE("strict enforcement: outbound override parses") {
    std::string json = R"({
        "outbounds":[
            {"tag":"vpn","type":"interface","interface":"wg0","strict_enforcement":true}
        ]
    })";
    auto cfg = parse_test_config(json);
    REQUIRE(cfg.outbounds.has_value());
    REQUIRE(cfg.outbounds->size() == 1);
    CHECK(cfg.outbounds->front().strict_enforcement.value_or(false));
}

// =============================================================================
// Route rule port/address validation
// =============================================================================

TEST_CASE("route rule: valid port and address filters are accepted") {
    std::string json = R"({
        "route":{"rules":[
            {"list":["ads"],"outbound":"vpn","src_port":"80,443","dest_port":"!10000-20000","src_addr":"10.0.0.1,2001:db8::1","dest_addr":"!192.168.0.0/16"}
        ]}
    })";
    CHECK_NOTHROW(parse_config(json));
}

TEST_CASE("route rule: at least one condition is required") {
    std::string json = R"({
        "route":{"rules":[
            {"list":[],"outbound":"vpn"}
        ]}
    })";
    const auto issues = parse_issues(json);
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().path == "route.rules[0]");
}

TEST_CASE("route rule: list is optional when another condition is present") {
    std::string json = R"({
        "route":{"rules":[
            {"outbound":"vpn","src_addr":"10.0.0.1"}
        ]}
    })";
    CHECK_NOTHROW(parse_config(json));
}

TEST_CASE("route rule: invalid src_port reports route.rules[0].src_port") {
    std::string json = R"({"route":{"rules":[{"list":["ads"],"outbound":"vpn","src_port":"1,,2"}]}})";
    const auto issues = parse_issues(json);
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().path == "route.rules[0].src_port");
}

TEST_CASE("route rule: invalid dest_port range reports route.rules[0].dest_port") {
    std::string json = R"({"route":{"rules":[{"list":["ads"],"outbound":"vpn","dest_port":"9000-8000"}]}})";
    const auto issues = parse_issues(json);
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().path == "route.rules[0].dest_port");
}

TEST_CASE("route rule: invalid src_addr reports route.rules[0].src_addr") {
    std::string json = R"({"route":{"rules":[{"list":["ads"],"outbound":"vpn","src_addr":"not-an-ip"}]}})";
    const auto issues = parse_issues(json);
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().path == "route.rules[0].src_addr");
}

TEST_CASE("route rule: invalid dest_addr reports route.rules[0].dest_addr") {
    std::string json = R"({"route":{"rules":[{"list":["ads"],"outbound":"vpn","dest_addr":",10.0.0.0/8"}]}})";
    const auto issues = parse_issues(json);
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().path == "route.rules[0].dest_addr");
}

TEST_CASE("route inbound_interfaces: omitted is accepted") {
    CHECK_NOTHROW(parse_test_config(R"({"route":{"rules":[{"list":["ads"],"outbound":"vpn"}]}})"));
}

TEST_CASE("route inbound_interfaces: empty array is accepted") {
    CHECK_NOTHROW(parse_test_config(R"({"route":{"inbound_interfaces":[],"rules":[{"list":["ads"],"outbound":"vpn"}]}})"));
}

TEST_CASE("route inbound_interfaces: valid entries are parsed") {
    auto cfg = parse_test_config(
        R"({"route":{"inbound_interfaces":["br0","wg0"],"rules":[{"list":["ads"],"outbound":"vpn"}]}})");
    REQUIRE(cfg.route.has_value());
    REQUIRE(cfg.route->inbound_interfaces.has_value());
    CHECK(cfg.route->inbound_interfaces->size() == 2);
    CHECK(cfg.route->inbound_interfaces->at(0) == "br0");
    CHECK(cfg.route->inbound_interfaces->at(1) == "wg0");
}

TEST_CASE("route inbound_interfaces: non-array is rejected") {
    const auto issues = parse_issues(
        R"({"route":{"inbound_interfaces":"br0","rules":[{"list":["ads"],"outbound":"vpn"}]}})");
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().path == "route.inbound_interfaces");
}

TEST_CASE("route inbound_interfaces: non-string entry is rejected") {
    const auto issues = parse_issues(
        R"({"route":{"inbound_interfaces":["br0",42],"rules":[{"list":["ads"],"outbound":"vpn"}]}})");
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().path == "route.inbound_interfaces[1]");
}

TEST_CASE("route inbound_interfaces: blank entry is rejected") {
    const auto issues = parse_issues(
        R"({"route":{"inbound_interfaces":["br0","   "],"rules":[{"list":["ads"],"outbound":"vpn"}]}})");
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().path == "route.inbound_interfaces[1]");
}

TEST_CASE("route inbound_interfaces: duplicate entry is rejected") {
    const auto issues = parse_issues(
        R"({"route":{"inbound_interfaces":["br0","br0"],"rules":[{"list":["ads"],"outbound":"vpn"}]}})");
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().path == "route.inbound_interfaces[1]");
}

// =============================================================================
// is_reserved_table
// =============================================================================

TEST_CASE("is_reserved_table: table 0 (unspec) is reserved") {
    CHECK(is_reserved_table(0));
}

TEST_CASE("is_reserved_table: table 128 (prelocal) is reserved") {
    CHECK(is_reserved_table(128));
}

TEST_CASE("is_reserved_table: tables 250-260 are reserved") {
    for (uint32_t id = 250; id <= 260; ++id) {
        CHECK(is_reserved_table(id));
    }
}

TEST_CASE("is_reserved_table: tables 32000+ are reserved") {
    CHECK(is_reserved_table(32000));
    CHECK(is_reserved_table(32767));
    CHECK(is_reserved_table(65535));
}

TEST_CASE("is_reserved_table: safe values are not reserved") {
    CHECK_FALSE(is_reserved_table(1));
    CHECK_FALSE(is_reserved_table(100));
    CHECK_FALSE(is_reserved_table(127));
    CHECK_FALSE(is_reserved_table(129));
    CHECK_FALSE(is_reserved_table(249));
    CHECK_FALSE(is_reserved_table(261));
    CHECK_FALSE(is_reserved_table(31999));
}

// =============================================================================
// iproute.table_start validation
// =============================================================================

TEST_CASE("iproute.table_start: default (no iproute section) is accepted") {
    CHECK_NOTHROW(parse_test_config(R"({})"));
}

TEST_CASE("iproute.table_start: value 100 is accepted") {
    CHECK_NOTHROW(parse_test_config(R"({"iproute":{"table_start":100}})"));
}

TEST_CASE("iproute.table_start: value 249 is accepted") {
    CHECK_NOTHROW(parse_test_config(R"({"iproute":{"table_start":249}})"));
}

TEST_CASE("iproute.table_start: value 261 is accepted") {
    CHECK_NOTHROW(parse_test_config(R"({"iproute":{"table_start":261}})"));
}

TEST_CASE("iproute.table_start: value 31999 is accepted") {
    CHECK_NOTHROW(parse_test_config(R"({"iproute":{"table_start":31999}})"));
}

TEST_CASE("iproute.table_start: value 0 is rejected") {
    CHECK_THROWS_AS(parse_test_config(R"({"iproute":{"table_start":0}})"), ConfigError);
}

TEST_CASE("iproute.table_start: value 128 (prelocal) is rejected") {
    CHECK_THROWS_AS(parse_test_config(R"({"iproute":{"table_start":128}})"), ConfigError);
}

TEST_CASE("iproute.table_start: value 250 is rejected") {
    CHECK_THROWS_AS(parse_test_config(R"({"iproute":{"table_start":250}})"), ConfigError);
}

TEST_CASE("iproute.table_start: value 255 (local) is rejected") {
    CHECK_THROWS_AS(parse_test_config(R"({"iproute":{"table_start":255}})"), ConfigError);
}

TEST_CASE("iproute.table_start: value 260 is rejected") {
    CHECK_THROWS_AS(parse_test_config(R"({"iproute":{"table_start":260}})"), ConfigError);
}

TEST_CASE("iproute.table_start: value 32000 is rejected") {
    CHECK_THROWS_AS(parse_test_config(R"({"iproute":{"table_start":32000}})"), ConfigError);
}

TEST_CASE("iproute.table_start: non-integer value is rejected") {
    CHECK_THROWS_AS(
        parse_test_config(R"({"iproute":{"table_start":"400abc"}})"),
        ConfigValidationError
    );
    CHECK_THROWS_AS(
        parse_test_config(R"({"iproute":{"table_start":400.5}})"),
        ConfigValidationError
    );
}

// =============================================================================

TEST_CASE("fwmark mask: invalid value is rejected during config parsing") {
    const std::string json = R"({
        "fwmark": {
            "mask": "0xFFFF0001"
        }
    })";

    CHECK_THROWS_AS(parse_test_config(json), ConfigValidationError);
}

TEST_CASE("fwmark start and mask: non-string values are rejected during config parsing") {
    CHECK_THROWS_AS(parse_test_config(R"({"fwmark":{"start":65536}})"), ConfigValidationError);
    CHECK_THROWS_AS(parse_test_config(R"({"fwmark":{"mask":16711680}})"), ConfigValidationError);
}

TEST_CASE("config parsing returns all collected validation errors") {
    const std::string json = R"({
        "lists_autoupdate": {
            "enabled": true
        },
        "fwmark": {
            "mask": "0xFFFF0001"
        },
        "lists": {
            "bad-list": {}
        }
    })";

    try {
        (void)parse_test_config(json);
        FAIL("Expected ConfigValidationError");
    } catch (const ConfigValidationError& e) {
        CHECK(e.issues().size() >= 3);

        bool saw_cron_error = false;
        bool saw_fwmark_error = false;
        bool saw_list_error = false;

        for (const auto& issue : e.issues()) {
            if (issue.path == "lists_autoupdate.cron") {
                saw_cron_error = true;
            }
            if (issue.path == "fwmark.mask") {
                saw_fwmark_error = true;
            }
            if (issue.path == "lists.bad-list") {
                saw_list_error = true;
            }
        }

        CHECK(saw_cron_error);
        CHECK(saw_fwmark_error);
        CHECK(saw_list_error);
    }
}

TEST_CASE("daemon.firewall_verify_max_bytes: accepts positive value") {
    auto cfg = parse_test_config(R"({"daemon":{"firewall_verify_max_bytes":131072}})");
    REQUIRE(cfg.daemon.has_value());
    REQUIRE(cfg.daemon->firewall_verify_max_bytes.has_value());
    CHECK(*cfg.daemon->firewall_verify_max_bytes == 131072);
}

TEST_CASE("daemon.firewall_verify_max_bytes: rejects non-integer value") {
    const auto issues = parse_issues(R"({"daemon":{"firewall_verify_max_bytes":"131072"}})");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "daemon.firewall_verify_max_bytes");
}

TEST_CASE("daemon.firewall_verify_max_bytes: rejects negative value") {
    CHECK_THROWS_AS(parse_test_config(R"({"daemon":{"firewall_verify_max_bytes":-1}})"), ConfigError);
}

TEST_CASE("daemon.firewall_backend: defaults to auto when absent") {
    auto cfg = parse_test_config(R"({"daemon":{}})");
    CHECK(firewall_backend_preference(cfg) == FirewallBackendPreference::auto_detect);
}

TEST_CASE("daemon.firewall_backend: accepts auto") {
    auto cfg = parse_test_config(R"({"daemon":{"firewall_backend":"auto"}})");
    CHECK(firewall_backend_preference(cfg) == FirewallBackendPreference::auto_detect);
}

TEST_CASE("daemon.firewall_backend: accepts iptables") {
    auto cfg = parse_test_config(R"({"daemon":{"firewall_backend":"iptables"}})");
    CHECK(firewall_backend_preference(cfg) == FirewallBackendPreference::iptables);
}

TEST_CASE("daemon.firewall_backend: accepts nftables") {
    auto cfg = parse_test_config(R"({"daemon":{"firewall_backend":"nftables"}})");
    CHECK(firewall_backend_preference(cfg) == FirewallBackendPreference::nftables);
}

TEST_CASE("daemon.firewall_backend: rejects non-string value") {
    const auto issues = parse_issues(R"({"daemon":{"firewall_backend":true}})");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "daemon.firewall_backend");
}

TEST_CASE("daemon.firewall_backend: rejects unsupported value") {
    CHECK_THROWS_AS(parse_test_config(R"({"daemon":{"firewall_backend":"pf"}})"), ConfigError);
}
