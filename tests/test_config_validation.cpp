#include <doctest/doctest.h>

#include "../src/config/config.hpp"

#include <nlohmann/json.hpp>
#include <string>

using namespace keen_pbr3;

// Helper: build a minimal valid config JSON with a single list entry.
static std::string list_config_json(const std::string& list_name,
                                    const std::string& list_body = R"({"ip_cidrs":["10.0.0.1"]})") {
    nlohmann::json config;
    config["lists"] = nlohmann::json::object();
    config["lists"][list_name] = nlohmann::json::parse(list_body);
    return config.dump();
}

// =============================================================================
// List name: length validation
// =============================================================================

TEST_CASE("list name: exactly 24 chars is valid") {
    const std::string name(24, 'a'); // "aaaaaaaaaaaaaaaaaaaaaaaa"
    CHECK_NOTHROW(parse_config(list_config_json(name)));
}

TEST_CASE("list name: 25 chars is rejected") {
    const std::string name(25, 'a');
    CHECK_THROWS_AS(parse_config(list_config_json(name)), ConfigError);
}

TEST_CASE("list name: 1 char is valid") {
    CHECK_NOTHROW(parse_config(list_config_json("a")));
}

TEST_CASE("list name: empty string is rejected") {
    // JSON object key "" is valid JSON but must be rejected by our validation.
    const std::string json = R"({"lists":{"":{"ip_cidrs":["10.0.0.1"]}}})";
    CHECK_THROWS_AS(parse_config(json), ConfigError);
}

// =============================================================================
// List name: character set validation
// =============================================================================

TEST_CASE("list name: lowercase letters only is valid") {
    CHECK_NOTHROW(parse_config(list_config_json("mylist")));
}

TEST_CASE("list name: uppercase letters are valid") {
    CHECK_NOTHROW(parse_config(list_config_json("MyList")));
}

TEST_CASE("list name: uppercase first char is valid") {
    CHECK_NOTHROW(parse_config(list_config_json("Mylist")));
}

TEST_CASE("list name: mixed case + digits + underscore is valid") {
    CHECK_NOTHROW(parse_config(list_config_json("My_List01")));
}

TEST_CASE("list name: lowercase + digits + underscore is valid") {
    CHECK_NOTHROW(parse_config(list_config_json("my_list01")));
}

TEST_CASE("list name: first char digit is rejected") {
    CHECK_THROWS_AS(parse_config(list_config_json("1list")), ConfigError);
}

TEST_CASE("list name: first char underscore is rejected") {
    CHECK_THROWS_AS(parse_config(list_config_json("_list")), ConfigError);
}

TEST_CASE("list name: hyphen in name is rejected") {
    CHECK_THROWS_AS(parse_config(list_config_json("my-list")), ConfigError);
}

TEST_CASE("list name: space in name is rejected") {
    CHECK_THROWS_AS(parse_config(list_config_json("my list")), ConfigError);
}

TEST_CASE("list name: dot in name is rejected") {
    CHECK_THROWS_AS(parse_config(list_config_json("my.list")), ConfigError);
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
        "dns":{"servers":[{"tag":"vpn-dns","address":"10.8.0.1","detour":"vpn"}]}})";
    CHECK_NOTHROW(parse_config(json));
}

TEST_CASE("dns detour: valid table outbound") {
    std::string json = R"({"outbounds":[{"tag":"tbl","type":"table","table":100}],
        "dns":{"servers":[{"tag":"tbl-dns","address":"10.8.0.2","detour":"tbl"}]}})";
    CHECK_NOTHROW(parse_config(json));
}

TEST_CASE("dns detour: valid urltest outbound") {
    std::string json = R"({"outbounds":[
        {"tag":"vpn","type":"interface","interface":"wg0"},
        {"tag":"ut","type":"urltest","url":"http://example.com","outbound_groups":[{"outbounds":["vpn"]}]}
    ],"dns":{"servers":[{"tag":"ut-dns","address":"10.8.0.3","detour":"ut"}]}})";
    CHECK_NOTHROW(parse_config(json));
}

TEST_CASE("dns detour: unknown outbound tag is rejected") {
    std::string json = R"({"outbounds":[{"tag":"vpn","type":"interface","interface":"wg0"}],
        "dns":{"servers":[{"tag":"vpn-dns","address":"10.8.0.1","detour":"nonexistent"}]}})";
    CHECK_THROWS_AS(parse_config(json), ConfigError);
}

TEST_CASE("dns detour: blackhole outbound is rejected") {
    std::string json = R"({"outbounds":[{"tag":"bh","type":"blackhole"}],
        "dns":{"servers":[{"tag":"bh-dns","address":"10.8.0.1","detour":"bh"}]}})";
    CHECK_THROWS_AS(parse_config(json), ConfigError);
}

TEST_CASE("dns detour: ignore outbound is rejected") {
    std::string json = R"({"outbounds":[{"tag":"ig","type":"ignore"}],
        "dns":{"servers":[{"tag":"ig-dns","address":"10.8.0.1","detour":"ig"}]}})";
    CHECK_THROWS_AS(parse_config(json), ConfigError);
}

TEST_CASE("dns detour: no detour field is accepted") {
    std::string json = R"({"dns":{"servers":[{"tag":"plain-dns","address":"8.8.8.8"}]}})";
    CHECK_NOTHROW(parse_config(json));
}

TEST_CASE("dns test server: valid listen parses") {
    std::string json = R"({"dns":{"test_server":{"listen":"127.0.0.88:53"}}})";
    auto cfg = parse_config(json);
    REQUIRE(cfg.dns.has_value());
    REQUIRE(cfg.dns->test_server.has_value());
    CHECK(cfg.dns->test_server->listen == "127.0.0.88:53");
    CHECK(!cfg.dns->test_server->answer_ipv4.has_value());
}

TEST_CASE("dns test server: explicit answer IPv4 parses") {
    std::string json = R"({"dns":{"test_server":{"listen":"127.0.0.88:53","answer_ipv4":"127.0.0.99"}}})";
    auto cfg = parse_config(json);
    REQUIRE(cfg.dns.has_value());
    REQUIRE(cfg.dns->test_server.has_value());
    CHECK(cfg.dns->test_server->answer_ipv4.value_or("") == "127.0.0.99");
}

TEST_CASE("dns test server: invalid listen is rejected") {
    std::string json = R"({"dns":{"test_server":{"listen":"not-an-ip:53"}}})";
    CHECK_THROWS_AS(parse_config(json), ConfigError);
}

TEST_CASE("dns test server: ipv6 listen is rejected") {
    std::string json = R"({"dns":{"test_server":{"listen":"[::1]:53"}}})";
    CHECK_THROWS_AS(parse_config(json), ConfigError);
}

TEST_CASE("dns test server: invalid answer IPv4 is rejected") {
    std::string json = R"({"dns":{"test_server":{"listen":"127.0.0.88:53","answer_ipv4":"example.com"}}})";
    CHECK_THROWS_AS(parse_config(json), ConfigError);
}

TEST_CASE("strict enforcement: daemon default parses") {
    std::string json = R"({"daemon":{"strict_enforcement":true}})";
    auto cfg = parse_config(json);
    REQUIRE(cfg.daemon.has_value());
    CHECK(cfg.daemon->strict_enforcement.value_or(false));
}

TEST_CASE("strict enforcement: outbound override parses") {
    std::string json = R"({
        "outbounds":[
            {"tag":"vpn","type":"interface","interface":"wg0","strict_enforcement":true}
        ]
    })";
    auto cfg = parse_config(json);
    REQUIRE(cfg.outbounds.has_value());
    REQUIRE(cfg.outbounds->size() == 1);
    CHECK(cfg.outbounds->front().strict_enforcement.value_or(false));
}

TEST_CASE("fwmark mask: invalid value is rejected during config parsing") {
    const std::string json = R"({
        "fwmark": {
            "mask": 4294901761
        }
    })";

    CHECK_THROWS_AS(parse_config(json), ConfigValidationError);
}

TEST_CASE("config parsing returns all collected validation errors") {
    const std::string json = R"({
        "lists_autoupdate": {
            "enabled": true
        },
        "fwmark": {
            "mask": 4294901761
        },
        "lists": {
            "bad-list": {}
        }
    })";

    try {
        (void)parse_config(json);
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
