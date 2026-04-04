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
        fallback_server.tag = "default-dns";
        fallback_server.address = "127.0.0.1";
        cfg.dns->servers = std::vector<DnsServer>{fallback_server};
    }
    if (!cfg.dns->fallback.has_value()) {
        cfg.dns->fallback = std::vector<std::string>{"default-dns"};
    }
    if (!cfg.dns->system_resolver.has_value()) {
        api::SystemResolver resolver;
        resolver.type = DnsSystemResolverType::DNSMASQ_NFTSET;
        resolver.hook = "/usr/lib/keen-pbr/dnsmasq.sh";
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

TEST_CASE("list name: uppercase letters are valid") {
    CHECK_NOTHROW(parse_test_config(list_config_json("MyList")));
}

TEST_CASE("list name: uppercase first char is valid") {
    CHECK_NOTHROW(parse_test_config(list_config_json("Mylist")));
}

TEST_CASE("list name: mixed case + digits + underscore is valid") {
    CHECK_NOTHROW(parse_test_config(list_config_json("My_List01")));
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
        "dns":{"servers":[{"tag":"vpn-dns","address":"10.8.0.1","detour":"vpn"}],"fallback":["vpn-dns"]}})";
    CHECK_NOTHROW(parse_test_config(json));
}

TEST_CASE("dns detour: valid table outbound") {
    std::string json = R"({"outbounds":[{"tag":"tbl","type":"table","table":100}],
        "dns":{"servers":[{"tag":"tbl-dns","address":"10.8.0.2","detour":"tbl"}],"fallback":["tbl-dns"]}})";
    CHECK_NOTHROW(parse_test_config(json));
}

TEST_CASE("dns detour: valid urltest outbound") {
    std::string json = R"({"outbounds":[
        {"tag":"vpn","type":"interface","interface":"wg0"},
        {"tag":"ut","type":"urltest","url":"http://example.com","outbound_groups":[{"outbounds":["vpn"]}]}
    ],"dns":{"servers":[{"tag":"ut-dns","address":"10.8.0.3","detour":"ut"}],"fallback":["ut-dns"]}})";
    CHECK_NOTHROW(parse_test_config(json));
}

TEST_CASE("dns detour: unknown outbound tag is rejected") {
    std::string json = R"({"outbounds":[{"tag":"vpn","type":"interface","interface":"wg0"}],
        "dns":{"servers":[{"tag":"vpn-dns","address":"10.8.0.1","detour":"nonexistent"}],"fallback":["vpn-dns"]}})";
    CHECK_THROWS_AS(parse_test_config(json), ConfigError);
}

TEST_CASE("dns detour: blackhole outbound is rejected") {
    std::string json = R"({"outbounds":[{"tag":"bh","type":"blackhole"}],
        "dns":{"servers":[{"tag":"bh-dns","address":"10.8.0.1","detour":"bh"}],"fallback":["bh-dns"]}})";
    CHECK_THROWS_AS(parse_test_config(json), ConfigError);
}

TEST_CASE("dns detour: ignore outbound is rejected") {
    std::string json = R"({"outbounds":[{"tag":"ig","type":"ignore"}],
        "dns":{"servers":[{"tag":"ig-dns","address":"10.8.0.1","detour":"ig"}],"fallback":["ig-dns"]}})";
    CHECK_THROWS_AS(parse_test_config(json), ConfigError);
}

TEST_CASE("dns detour: no detour field is accepted") {
    std::string json = R"({"dns":{"servers":[{"tag":"plain-dns","address":"8.8.8.8"}],"fallback":["plain-dns"]}})";
    CHECK_NOTHROW(parse_test_config(json));
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
            "servers": [{"tag":"plain-dns","address":"8.8.8.8"}],
            "fallback": ["plain-dns"],
            "system_resolver": {
                "type": "dnsmasq-nftset",
                "hook": "/usr/lib/keen-pbr/dnsmasq.sh",
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
                {"tag":"plain-dns","address":"8.8.8.8"}
            ],
            "fallback": ["plain-dns"]
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
            "servers": [{"tag":"plain-dns","address":"8.8.8.8"}],
            "system_resolver": {
                "type": "dnsmasq-nftset",
                "hook": "/usr/lib/keen-pbr/dnsmasq.sh",
                "address": "127.0.0.1"
            }
        }
    })");

    CHECK_NOTHROW(validate_config(cfg));
}

TEST_CASE("config validation: allows empty fallback array") {
    auto cfg = parse_config(R"({
        "dns": {
            "servers": [{"tag":"plain-dns","address":"8.8.8.8"}],
            "fallback": [],
            "system_resolver": {
                "type": "dnsmasq-nftset",
                "hook": "/usr/lib/keen-pbr/dnsmasq.sh",
                "address": "127.0.0.1"
            }
        }
    })");

    CHECK_NOTHROW(validate_config(cfg));
}

TEST_CASE("config validation: rejects unknown fallback tag") {
    auto cfg = parse_config(R"({
        "dns": {
            "servers": [{"tag":"plain-dns","address":"8.8.8.8"}],
            "fallback": ["missing-dns"],
            "system_resolver": {
                "type": "dnsmasq-nftset",
                "hook": "/usr/lib/keen-pbr/dnsmasq.sh",
                "address": "127.0.0.1"
            }
        }
    })");

    CHECK_THROWS_AS(validate_config(cfg), ConfigValidationError);
}

TEST_CASE("config validation: rejects duplicate fallback tag") {
    auto cfg = parse_config(R"({
        "dns": {
            "servers": [{"tag":"plain-dns","address":"8.8.8.8"}],
            "fallback": ["plain-dns", "plain-dns"],
            "system_resolver": {
                "type": "dnsmasq-nftset",
                "hook": "/usr/lib/keen-pbr/dnsmasq.sh",
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
    fallback_server.tag = "default-dns";
    fallback_server.address = "127.0.0.1";
    cfg.dns->servers = std::vector<DnsServer>{fallback_server};
    cfg.dns->fallback = std::vector<std::string>{"default-dns"};
    api::SystemResolver resolver{};
    resolver.type = DnsSystemResolverType::DNSMASQ_NFTSET;
    cfg.dns->system_resolver = resolver;

    try {
        validate_config(cfg);
        FAIL("Expected ConfigValidationError");
    } catch (const ConfigValidationError& e) {
        REQUIRE(e.issues().size() == 2);
        CHECK(e.issues()[0].path == "dns.system_resolver.hook");
        CHECK(e.issues()[0].message == "dns.system_resolver.hook must not be empty");
        CHECK(e.issues()[1].path == "dns.system_resolver.address");
        CHECK(e.issues()[1].message == "dns.system_resolver.address must not be empty");
    }
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
