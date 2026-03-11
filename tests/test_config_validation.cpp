#include <doctest/doctest.h>

#include "../src/config/config.hpp"

#include <string>

using namespace keen_pbr3;

// Helper: build a minimal valid config JSON with a single list entry.
static std::string list_config_json(const std::string& list_name,
                                    const std::string& list_body = R"({"ip_cidrs":["10.0.0.1"]})") {
    return R"({"lists":{")" + list_name + R"(":)" + list_body + R"(}})";
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
