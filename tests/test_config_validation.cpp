#include <doctest/doctest.h>

#include "../src/config/config.hpp"
#include "../src/config/config_writer.hpp"
#include "../src/config/routing_state.hpp"
#include "../src/util/system_info.hpp"
#include "../src/util/firewall_backend_utils.hpp"
#include "../src/util/kernel_capabilities.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <fstream>
#include <algorithm>
#include <cstdio>
#include <unistd.h>
#include <filesystem>
#include <iterator>
#include <stdexcept>

using namespace keen_pbr3;

namespace {

struct SystemInfoTestGuard {
    ~SystemInfoTestGuard() { reset_system_info_for_tests(); }
};

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
    validate_config(cfg);
    return cfg;
}

std::string read_full_reference_config_jsonc(const std::string& locale_suffix) {
    const std::filesystem::path relative_path =
        "docs/content/docs/configuration/full-reference-config" + locale_suffix + ".md";
    std::filesystem::path path = std::filesystem::current_path() / relative_path;
    if (!std::filesystem::exists(path)) {
        const std::filesystem::path source_file = __FILE__;
        path = source_file.parent_path().parent_path() / relative_path;
    }

    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("Cannot open full-reference config documentation: " + path.string());
    }
    const std::string document(std::istreambuf_iterator<char>(input), {});
    const std::string fence = "```json {filename=\"config.json\"}";
    const auto code_start = document.find(fence);
    if (code_start == std::string::npos) {
        throw std::runtime_error("Full-reference config JSONC code block was not found");
    }
    const auto body_start = document.find('\n', code_start + fence.size());
    const auto code_end = document.find("\n```", body_start);
    if (body_start == std::string::npos || code_end == std::string::npos) {
        throw std::runtime_error("Full-reference config JSONC code block is incomplete");
    }
    return document.substr(body_start + 1, code_end - body_start - 1);
}

void remove_platform_specific_config(nlohmann::json& config_json, FirewallBackend backend) {
#ifndef USE_KEENETIC_API
    auto& dns_servers = config_json["dns"]["servers"];
    dns_servers.erase(
        std::remove_if(dns_servers.begin(), dns_servers.end(), [](const auto& server) {
            return server.value("type", "static") == "keenetic";
        }),
        dns_servers.end());
#endif

    if (backend == FirewallBackend::iptables) {
        auto& route_rules = config_json["route"]["rules"];
        for (auto& rule : route_rules) {
            if (rule.contains("src_port") && rule.contains("dest_port") &&
                ((rule["src_port"].is_string() &&
                  rule["src_port"].get<std::string>().find(',') != std::string::npos) ||
                 (rule["dest_port"].is_string() &&
                  rule["dest_port"].get<std::string>().find(',') != std::string::npos))) {
                // iptables cannot combine a port list with both source and destination ports.
                rule.erase("src_port");
            }
        }
        route_rules.erase(
            std::remove_if(route_rules.begin(), route_rules.end(), [](const auto& rule) {
                return rule.contains("default_gateway");
            }),
            route_rules.end());
    }
}

} // namespace

TEST_CASE("full-reference config examples parse and validate in both locales") {
    for (const auto& locale_suffix : {std::string{}, std::string{".ru"}}) {
        INFO("locale suffix: " << (locale_suffix.empty() ? "EN" : "RU"));
        const auto reference_json = nlohmann::json::parse(
            read_full_reference_config_jsonc(locale_suffix), nullptr, true, true);

        for (const auto backend : {FirewallBackend::nftables, FirewallBackend::iptables}) {
#ifdef KEEN_PBR_PLATFORM_KEENETIC
            if (backend == FirewallBackend::nftables) {
                continue;
            }
#endif
            auto config_json = reference_json;
            remove_platform_specific_config(config_json, backend);
            set_detected_firewall_backend_for_tests(backend);
            CHECK_NOTHROW(validate_config(parse_config(config_json.dump())));
        }
        reset_detected_firewall_backend_for_tests();
    }
}

static std::vector<ConfigValidationIssue> validate_issues(
    const std::string& json, const ConfigValidationContext& context = {});

TEST_CASE("icmptest validation accepts a timing-safe complete probe set") {
    const auto cfg = parse_test_config(R"({"outbounds":[
      {"type":"interface","tag":"wan","interface":"wan"},
      {"type":"icmptest","tag":"auto","interval_ms":60000,
       "outbound_groups":[{"members":[{"outbound":"wan","target":"1.1.1.1"}]}]}
    ]})");
    REQUIRE(cfg.outbounds);
    REQUIRE(cfg.outbounds->at(1).outbound_groups);
    const auto& group = cfg.outbounds->at(1).outbound_groups->at(0);
    CHECK_FALSE(group.outbounds.has_value());
    CHECK_FALSE(group.candidates.has_value());
    REQUIRE(group.members);
    CHECK(group.members->at(0).outbound == "wan");
    CHECK(group.members->at(0).target == "1.1.1.1");
    CHECK_NOTHROW(parse_test_config(nlohmann::json(cfg).dump()));
}

#ifdef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("Keenetic platform build rejects balance strategy") {
    const auto issues = validate_issues(R"({
      "outbounds":[
        {"type":"interface","tag":"wan_a","interface":"wan_a"},
        {"type":"interface","tag":"wan_b","interface":"wan_b"},
        {"type":"urltest","tag":"auto","url":"http://example.test",
         "strategy":"balance","outbound_groups":[{"outbounds":["wan_a","wan_b"]}]}
      ]
    })");
    bool has_keenetic_error = false;
    for (const auto& issue : issues) {
        if (issue.path == "outbounds[2].strategy" &&
            issue.message.find("load balancing is not available on Keenetic") !=
                std::string::npos) {
            has_keenetic_error = true;
        }
    }
    CHECK(has_keenetic_error);
}

TEST_CASE("Keenetic platform build rejects the nftables backend and resolves auto to iptables") {
    const auto issues = validate_issues(R"({"daemon":{"firewall_backend":"nftables"}})");
    bool has_backend_error = false;
    for (const auto& issue : issues) {
        if (issue.path == "daemon.firewall_backend" &&
            issue.message.find("Keenetic supports only the iptables firewall backend") !=
                std::string::npos) {
            has_backend_error = true;
        }
    }
    CHECK(has_backend_error);

    SystemInfoTestGuard sys_guard;
    set_detected_firewall_backend_for_tests(FirewallBackend::nftables);
    CHECK(resolve_firewall_backend(FirewallBackendPreference::auto_detect) ==
          FirewallBackend::iptables);
    CHECK_THROWS_AS(resolve_firewall_backend(FirewallBackendPreference::nftables), FirewallError);
    reset_detected_firewall_backend_for_tests();
}
#else
TEST_CASE("test-group balance and default gateway rules are parsed") {
    const auto cfg = parse_test_config(R"({
      "daemon":{"firewall_backend":"nftables"},
      "outbounds":[
        {"type":"interface","tag":"wan_a","interface":"wan_a"},
        {"type":"interface","tag":"wan_b","interface":"wan_b"},
        {"type":"urltest","tag":"auto","url":"http://example.test",
         "strategy":"balance","outbound_groups":[{"outbounds":["wan_a","wan_b"]}]}
      ],
      "route":{"rules":[
        {"default_gateway":"ipv4","outbound":"auto"},
        {"default_gateway":"ipv6","outbound":"auto"}
      ]}
    })");
    CHECK(cfg.outbounds->at(2).strategy == api::Strategy::BALANCE);
    CHECK(cfg.route->rules->at(0).default_gateway == api::DefaultGateway::IPV4);
}

TEST_CASE("default gateway rejects the iptables backend while balance is accepted") {
    const auto issues = validate_issues(R"({
      "daemon":{"firewall_backend":"iptables"},
      "outbounds":[
        {"type":"interface","tag":"wan","interface":"wan"},
        {"type":"urltest","tag":"auto","url":"http://example.test",
         "strategy":"balance","outbound_groups":[{"outbounds":["wan"]}]}
      ],
      "route":{"rules":[{"default_gateway":"ipv4","outbound":"auto"}]}
    })");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "route.rules[0].default_gateway");
}

TEST_CASE("auto backend resolves to iptables, accepts balance and rejects default_gateway") {
    SystemInfoTestGuard sys_guard;
    set_detected_firewall_backend_for_tests(FirewallBackend::iptables);
    const auto issues = validate_issues(R"({
      "daemon":{"firewall_backend":"auto"},
      "outbounds":[
        {"type":"interface","tag":"wan","interface":"wan"},
        {"type":"urltest","tag":"auto","url":"http://example.test",
         "strategy":"balance","outbound_groups":[{"outbounds":["wan"]}]}
      ],
      "route":{"rules":[{"default_gateway":"ipv4","outbound":"auto"}]}
    })");
    bool has_balance_error = false;
    bool has_gateway_error = false;
    for (const auto& issue : issues) {
        if (issue.path == "outbounds[1].strategy") has_balance_error = true;
        if (issue.path == "route.rules[0].default_gateway" &&
            issue.message.find("auto-detected: iptables") != std::string::npos) {
            has_gateway_error = true;
        }
    }
    CHECK_FALSE(has_balance_error);
    CHECK(has_gateway_error);
    reset_detected_firewall_backend_for_tests();
}

namespace {

std::string balance_config_json(const std::string& daemon_json) {
    return R"({)" + daemon_json + R"("outbounds":[
        {"type":"interface","tag":"wan","interface":"wan"},
        {"type":"urltest","tag":"auto","url":"http://example.test",
         "strategy":"balance","outbound_groups":[{"outbounds":["wan"]}]},
        {"type":"urltest","tag":"prio","url":"http://example.test",
         "outbound_groups":[{"outbounds":["wan"]}]}
      ]})";
}

bool has_raw_balance_issue(const std::vector<ConfigValidationIssue>& issues,
                           const std::string& path) {
    for (const auto& issue : issues) {
        if (issue.path == path &&
            issue.message.find("raw table") != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("balance is rejected when IPv4 raw PREROUTING is requested") {
    const auto issues = validate_issues(
        balance_config_json(R"("daemon":{"firewall_backend":"iptables"},)"),
        ConfigValidationContext{RawPreroutingMode{true, false}});
    REQUIRE(issues.size() == 1);
    CHECK(has_raw_balance_issue(issues, "outbounds[1].strategy"));
}

TEST_CASE("balance is rejected with raw6 PREROUTING while IPv6 is enabled") {
    const auto issues = validate_issues(
        balance_config_json(R"("daemon":{"firewall_backend":"iptables"},)"),
        ConfigValidationContext{RawPreroutingMode{false, true}});
    CHECK(has_raw_balance_issue(issues, "outbounds[1].strategy"));
}

TEST_CASE("raw6 PREROUTING is ignored for balance when IPv6 is disabled") {
    const auto issues = validate_issues(
        balance_config_json(
            R"("daemon":{"firewall_backend":"iptables","ipv6_enabled":false},)"),
        ConfigValidationContext{RawPreroutingMode{false, true}});
    CHECK(issues.empty());
}

TEST_CASE("balance without raw PREROUTING is accepted") {
    CHECK(validate_issues(balance_config_json(
              R"("daemon":{"firewall_backend":"iptables"},)")).empty());
}

TEST_CASE("non-balance strategy is accepted with raw PREROUTING") {
    const auto issues = validate_issues(
        R"({"daemon":{"firewall_backend":"iptables"},"outbounds":[
        {"type":"interface","tag":"wan","interface":"wan"},
        {"type":"urltest","tag":"prio","url":"http://example.test",
         "outbound_groups":[{"outbounds":["wan"]}]}]})",
        ConfigValidationContext{RawPreroutingMode{true, true}});
    CHECK(issues.empty());
}

TEST_CASE("raw PREROUTING flags never conflict with balance on nftables") {
    const auto issues = validate_issues(
        balance_config_json(R"("daemon":{"firewall_backend":"nftables"},)"),
        ConfigValidationContext{RawPreroutingMode{true, true}});
    CHECK_FALSE(has_raw_balance_issue(issues, "outbounds[1].strategy"));
}

TEST_CASE("auto backend fallback identifies unavailable detection") {
    SystemInfoTestGuard sys_guard;
    set_host_tools_for_tests(HostTools{});
    const auto issues = validate_issues(R"({
      "daemon":{"firewall_backend":"auto"},
      "outbounds":[{"type":"interface","tag":"wan","interface":"wan"}],
      "route":{"rules":[{"default_gateway":"ipv4","outbound":"wan"}]}
    })");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].message.find("detection unavailable") != std::string::npos);
    reset_kernel_capabilities_for_tests();
    reset_detected_firewall_backend_for_tests();
}

TEST_CASE("auto backend resolves to nftables and accepts balance") {
    SystemInfoTestGuard sys_guard;
    set_detected_firewall_backend_for_tests(FirewallBackend::nftables);
    const auto cfg = parse_test_config(R"({
      "daemon":{"firewall_backend":"auto"},
      "outbounds":[
        {"type":"interface","tag":"wan_a","interface":"wan_a"},
        {"type":"interface","tag":"wan_b","interface":"wan_b"},
        {"type":"urltest","tag":"auto","url":"http://example.test",
         "strategy":"balance","outbound_groups":[{"outbounds":["wan_a","wan_b"]}]}
      ],
      "route":{"rules":[
        {"default_gateway":"ipv4","outbound":"auto"}
      ]}
    })");
    CHECK(cfg.outbounds->at(2).strategy == api::Strategy::BALANCE);
    CHECK(cfg.route->rules->at(0).default_gateway == api::DefaultGateway::IPV4);
    reset_detected_firewall_backend_for_tests();
}

TEST_CASE("resolver generation skips runtime backend checks") {
    SystemInfoTestGuard sys_guard;
    set_detected_firewall_backend_for_tests(FirewallBackend::iptables);
    const auto config = parse_config(R"({
      "daemon":{"firewall_backend":"auto"},
      "outbounds":[{"type":"interface","tag":"wan","interface":"wan"}],
      "lists":{"domains":{"domains":["example.com"]}},
      "dns":{
        "resolver_integration":"dnsmasq",
        "servers":[{"tag":"up","address":"1.1.1.1"}],
        "rules":[{"list":["domains"],"server":"up"}]
      },
      "route":{"rules":[{"default_gateway":"ipv4","outbound":"wan"}]}
    })");

    CHECK_THROWS_AS(validate_config(config), ConfigValidationError);
    CHECK_NOTHROW(validate_config(config, ConfigValidationMode::ResolverGeneration));

    auto invalid_dns = config;
    invalid_dns.dns->rules->at(0).server = "missing";
    CHECK_THROWS_AS(validate_config(invalid_dns, ConfigValidationMode::ResolverGeneration),
                    ConfigValidationError);

    reset_detected_firewall_backend_for_tests();
}

TEST_CASE("explicit nftables backend accepts balance") {
    const auto cfg = parse_test_config(R"({
      "daemon":{"firewall_backend":"nftables"},
      "outbounds":[
        {"type":"interface","tag":"wan_a","interface":"wan_a"},
        {"type":"interface","tag":"wan_b","interface":"wan_b"},
        {"type":"urltest","tag":"auto","url":"http://example.test",
         "strategy":"balance","outbound_groups":[{"outbounds":["wan_a","wan_b"]}]}
      ],
      "route":{"rules":[
        {"default_gateway":"ipv4","outbound":"auto"}
      ]}
    })");
    CHECK(cfg.outbounds->at(2).strategy == api::Strategy::BALANCE);
    CHECK(cfg.route->rules->at(0).default_gateway == api::DefaultGateway::IPV4);
}

#endif

TEST_CASE("icmptest migrates the legacy split probe form in memory") {
    const auto cfg = parse_test_config(R"({"outbounds":[
      {"type":"interface","tag":"wan","interface":"wan"},
      {"type":"icmptest","tag":"auto","interval_ms":60000,
       "outbound_groups":[{"outbounds":["wan"],"weight":2}],
       "probes":[{"outbound":"wan","target":"1.1.1.1"}]}
    ]})");
    const auto& group = cfg.outbounds->at(1).outbound_groups->at(0);
    CHECK_FALSE(group.outbounds.has_value());
    CHECK_FALSE(group.candidates.has_value());
    CHECK_FALSE(group.weight.has_value());
    REQUIRE(group.members);
    CHECK(group.members->at(0).outbound == "wan");
    CHECK(group.members->at(0).target == "1.1.1.1");
}

TEST_CASE("icmptest rejects mixed legacy and canonical forms") {
    const auto issues = validate_issues(R"({"outbounds":[
      {"type":"interface","tag":"wan","interface":"wan"},
      {"type":"icmptest","tag":"auto","interval_ms":60000,
       "outbound_groups":[{"outbounds":["wan"],
         "candidates":[{"outbound":"wan","target":"1.1.1.1"}]}],
       "probes":[{"outbound":"wan","target":"1.1.1.1"}]}
    ]})");
    CHECK_FALSE(issues.empty());
}

TEST_CASE("icmptest rejects an unused legacy probe") {
    const auto issues = validate_issues(R"({"outbounds":[
      {"type":"interface","tag":"wan","interface":"wan"},
      {"type":"interface","tag":"backup","interface":"backup"},
      {"type":"icmptest","tag":"auto","interval_ms":60000,
       "outbound_groups":[{"outbounds":["wan"]}],
       "probes":[{"outbound":"wan","target":"1.1.1.1"},
                 {"outbound":"backup","target":"9.9.9.9"}]}
    ]})");
    CHECK_FALSE(issues.empty());
}

TEST_CASE("icmptest validation rejects an interval shorter than the probe cycle") {
    const auto issues = validate_issues(R"({"outbounds":[
      {"type":"interface","tag":"wan","interface":"wan"},
      {"type":"icmptest","tag":"auto","interval_ms":1000,"count":10,
       "probe_timeout_ms":5000,"outbound_groups":[{"candidates":[
         {"outbound":"wan","target":"1.1.1.1"}]}]}
    ]})");
    CHECK_FALSE(issues.empty());
}

TEST_CASE("icmptest timing uses timeout for every attempt and post-attempt pauses") {
    const auto invalid = validate_issues(R"({"outbounds":[
      {"type":"interface","tag":"wan","interface":"wan"},
      {"type":"icmptest","tag":"auto","interval_ms":4249,"count":3,
       "probe_timeout_ms":1000,"packet_interval_ms":200,
       "outbound_groups":[{"candidates":[{"outbound":"wan","target":"1.1.1.1"}]}]}
    ]})");
    CHECK_FALSE(invalid.empty());
    CHECK_NOTHROW(parse_test_config(R"({"outbounds":[
      {"type":"interface","tag":"wan","interface":"wan"},
      {"type":"icmptest","tag":"auto","interval_ms":4250,"count":3,
       "probe_timeout_ms":1000,"packet_interval_ms":200,
       "outbound_groups":[{"candidates":[{"outbound":"wan","target":"1.1.1.1"}]}]}
    ]})"));
}

TEST_CASE("icmptest rejects duplicate candidates and unsafe breaker values") {
    const auto issues = validate_issues(R"({"outbounds":[
      {"type":"interface","tag":"wan","interface":"wan"},
      {"type":"icmptest","tag":"auto","interval_ms":60000,
       "outbound_groups":[
         {"candidates":[{"outbound":"wan","target":"1.1.1.1"}]},
         {"candidates":[{"outbound":"wan","target":"1.1.1.1"}]}],
       "circuit_breaker":{"failure_threshold":0,"success_threshold":21,
         "half_open_max_requests":0,"timeout_ms":1}}
    ]})");
    CHECK(issues.size() >= 5);
}

TEST_CASE("icmptest validation handles extreme integers without overflowing") {
    CHECK_NOTHROW(validate_issues(R"({"outbounds":[
      {"type":"interface","tag":"wan","interface":"wan"},
      {"type":"icmptest","tag":"auto","interval_ms":9223372036854775807,
       "count":9223372036854775807,"probe_timeout_ms":9223372036854775807,
       "packet_interval_ms":9223372036854775807,"max_rtt_ms":9223372036854775807,
       "outbound_groups":[{"candidates":[{"outbound":"wan","target":"1.1.1.1"}]}]}
    ]})"));
}

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

static std::vector<ConfigValidationIssue> validate_issues(
    const std::string& json, const ConfigValidationContext& context) {
    try {
        auto cfg = parse_config(json);
        if (!cfg.dns.has_value()) {
            cfg.dns = DnsConfig{};
        }
        if (!cfg.dns->servers.has_value()) {
            DnsServer fallback_server;
            fallback_server.tag = "default_dns";
            fallback_server.address = "127.0.0.1";
            cfg.dns->servers = std::vector<DnsServer>{fallback_server};
        }
        validate_config(cfg, ConfigValidationMode::Runtime, context);
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

TEST_CASE("urltest URL is required and limited to HTTP(S)") {
    const std::string prefix = R"({"outbounds":[{"tag":"vpn","type":"interface","interface":"wg0"},)";
    const std::string suffix = R"({"tag":"ut","type":"urltest","outbound_groups":[{"outbounds":["vpn"]}]}]})";
    CHECK_THROWS_AS(parse_test_config(prefix + suffix), ConfigError);
    CHECK_THROWS_AS(parse_test_config(prefix + R"({"tag":"ut","type":"urltest","url":"file:///tmp/x","outbound_groups":[{"outbounds":["vpn"]}]}]})"), ConfigError);
    CHECK_THROWS_AS(parse_test_config(prefix + R"({"tag":"ut","type":"urltest","url":"ftp://example.test/x","outbound_groups":[{"outbounds":["vpn"]}]}]})"), ConfigError);
    CHECK_NOTHROW(parse_test_config(prefix + R"({"tag":"ut","type":"urltest","url":"https://example.test/x","outbound_groups":[{"outbounds":["vpn"]}]}]})"));
}

TEST_CASE("urltest conntrack_on_switch accepts preserve and delete") {
    const std::string prefix = R"({"outbounds":[{"tag":"vpn","type":"interface","interface":"wg0"},)";
    const std::string suffix = R"(,"outbound_groups":[{"outbounds":["vpn"]}]}]})";
    CHECK_NOTHROW(parse_test_config(prefix + R"({"tag":"ut","type":"urltest","url":"https://example.test","conntrack_on_switch":"preserve")" + suffix));
    CHECK_NOTHROW(parse_test_config(prefix + R"({"tag":"ut","type":"urltest","url":"https://example.test","conntrack_on_switch":"delete")" + suffix));
    CHECK_THROWS_AS(parse_test_config(prefix + R"({"tag":"ut","type":"urltest","url":"https://example.test","conntrack_on_switch":"flush")" + suffix), ConfigError);
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

TEST_CASE("dns servers: keenetic type is rejected on KeeneticOS 2.x") {
    SystemInfoTestGuard guard;
    set_system_info_for_tests(SystemInfo{
        .os_type = "keenetic",
        .os_version = "2.16.D.12.0-12",
        .build_variant = "keenetic",
    });

    const auto issues = validate_issues(R"({
        "dns":{
            "servers":[{"tag":"router_dns","type":"keenetic"}],
            "fallback":["router_dns"],
            "system_resolver":{"address":"127.0.0.1"}
        }
    })");

    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "dns.servers[0].type");
    CHECK(issues[0].message.find("requires KeeneticOS 3.x or newer") != std::string::npos);
    CHECK(issues[0].message.find("2.16.D.12.0-12") != std::string::npos);
}

TEST_CASE("dns servers: keenetic type is accepted on KeeneticOS 3.x") {
    SystemInfoTestGuard guard;
    set_system_info_for_tests(SystemInfo{
        .os_type = "keenetic",
        .os_version = "3.9.0",
        .build_variant = "keenetic",
    });

    CHECK_NOTHROW(parse_test_config(R"({
        "dns":{
            "servers":[{"tag":"router_dns","type":"keenetic"}],
            "fallback":["router_dns"],
            "system_resolver":{"address":"127.0.0.1"}
        }
    })"));
}

TEST_CASE("dns servers: keenetic type is accepted when KeeneticOS version is temporarily unknown") {
    SystemInfoTestGuard guard;
    set_system_info_for_tests(SystemInfo{
        .os_type = "keenetic",
        .os_version = "unknown",
        .build_variant = "keenetic",
    });

    CHECK_NOTHROW(parse_test_config(R"({
        "dns":{
            "servers":[{"tag":"router_dns","type":"keenetic"}],
            "fallback":["router_dns"],
            "system_resolver":{"address":"127.0.0.1"}
        }
    })"));
}

#ifdef USE_KEENETIC_API
TEST_CASE("dns servers: at most one keenetic type server is allowed") {
    std::string json = R"({
        "dns":{
            "servers":[
                {"tag":"keen_a","type":"keenetic"},
                {"tag":"keen_b","type":"keenetic"}
            ],
            "fallback":["keen_a"]
        }
    })";
    CHECK_THROWS_AS(parse_test_config(json), ConfigError);
}
#endif

TEST_CASE("route rule enabled: parse and serialize cover true false omitted and null") {
    const auto cfg_true = parse_test_config(R"({
        "lists":{"ads":{"domains":["example.com"]}},
        "outbounds":[{"tag":"vpn","type":"interface","interface":"eth0"}],
        "route":{"rules":[{"enabled":true,"list":["ads"],"outbound":"vpn"}]}
    })");
    REQUIRE(cfg_true.route.has_value());
    REQUIRE(cfg_true.route->rules.has_value());
    REQUIRE(cfg_true.route->rules->size() == 1);
    CHECK(cfg_true.route->rules->at(0).enabled == std::optional<bool>(true));
    const nlohmann::json json_true = cfg_true;
    CHECK(json_true["route"]["rules"][0]["enabled"] == true);

    const auto cfg_false = parse_test_config(R"({
        "lists":{"ads":{"domains":["example.com"]}},
        "outbounds":[{"tag":"vpn","type":"interface","interface":"eth0"}],
        "route":{"rules":[{"enabled":false,"list":["ads"],"outbound":"vpn"}]}
    })");
    REQUIRE(cfg_false.route.has_value());
    REQUIRE(cfg_false.route->rules.has_value());
    REQUIRE(cfg_false.route->rules->size() == 1);
    CHECK(cfg_false.route->rules->at(0).enabled == std::optional<bool>(false));
    const nlohmann::json json_false = cfg_false;
    CHECK(json_false["route"]["rules"][0]["enabled"] == false);

    const auto cfg_omitted = parse_test_config(R"({
        "lists":{"ads":{"domains":["example.com"]}},
        "outbounds":[{"tag":"vpn","type":"interface","interface":"eth0"}],
        "route":{"rules":[{"list":["ads"],"outbound":"vpn"}]}
    })");
    REQUIRE(cfg_omitted.route.has_value());
    REQUIRE(cfg_omitted.route->rules.has_value());
    REQUIRE(cfg_omitted.route->rules->size() == 1);
    CHECK_FALSE(cfg_omitted.route->rules->at(0).enabled.has_value());
    const nlohmann::json json_omitted = cfg_omitted;
    CHECK(json_omitted["route"]["rules"][0]["enabled"].is_null());

    const auto cfg_null = parse_test_config(R"({
        "lists":{"ads":{"domains":["example.com"]}},
        "outbounds":[{"tag":"vpn","type":"interface","interface":"eth0"}],
        "route":{"rules":[{"enabled":null,"list":["ads"],"outbound":"vpn"}]}
    })");
    REQUIRE(cfg_null.route.has_value());
    REQUIRE(cfg_null.route->rules.has_value());
    REQUIRE(cfg_null.route->rules->size() == 1);
    CHECK_FALSE(cfg_null.route->rules->at(0).enabled.has_value());
    const nlohmann::json json_null = cfg_null;
    CHECK(json_null["route"]["rules"][0]["enabled"].is_null());
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

TEST_CASE("dns test server: deprecated and ignored, invalid values no longer rejected") {
    for (const char* json : {
             R"({"dns":{"dns_test_server":{"listen":"not-an-ip:53"}}})",
             R"({"dns":{"dns_test_server":{"listen":"[::1]:53"}}})",
             R"({"dns":{"dns_test_server":{"listen":"127.0.0.88:53","answer_ipv4":"example.com"}}})"}) {
        auto cfg = parse_test_config(json);
        const auto warnings = config_warnings(cfg);
        REQUIRE(warnings.size() == 1);
        CHECK(warnings.front().find("intercept.dns.marker") != std::string::npos);
    }
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

TEST_CASE("inline domains allow comments and malformed entries") {
    CHECK_NOTHROW(parse_test_config(
        R"({"lists":{"domains":{"domains":["*.google.com","_dns._udp.example.com.","# package note","   ","bad/domain"]}}})"));
    for (const std::string& domain : {
             "bad/domain", "bad domain", "example..com", "-bad.example",
             "example.com\nserver=/evil/1.1.1.1"}) {
        CAPTURE(domain);
        const nlohmann::json config = {
            {"lists", {{"domains", {{"domains", {domain}}}}}},
        };
        CHECK_NOTHROW(parse_test_config(config.dump()));
    }
}

TEST_CASE("remote list URLs allow only HTTP and HTTPS") {
    CHECK_NOTHROW(parse_test_config(
        R"({"lists":{"a":{"url":"http://example.com/a"},"b":{"url":"HTTPS://example.com/b"}}})"));
    for (const std::string& url : {
             "file:///etc/passwd", "ftp://example.com/list", "data:text/plain,example.com",
             "//example.com/list", "http://"}) {
        CAPTURE(url);
        const nlohmann::json config = {
            {"lists", {{"remote", {{"url", url}}}}},
        };
        CHECK_THROWS_AS(parse_test_config(config.dump()), ConfigValidationError);
    }
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
            {"list":["ads"],"outbound":"vpn","dscp":46,"src_port":"80,443","dest_port":"!10000-20000","src_addr":"10.0.0.1,2001:db8::1","dest_addr":"!192.168.0.0/16"}
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

TEST_CASE("route rule: dscp-only rule is accepted") {
    std::string json = R"({
        "route":{"rules":[
            {"outbound":"vpn","dscp":46}
        ]}
    })";
    CHECK_NOTHROW(parse_config(json));
}

TEST_CASE("route rule: dscp bounds are enforced") {
    auto low_issues = parse_issues(R"({"route":{"rules":[{"outbound":"vpn","dscp":0}]}})");
    REQUIRE_FALSE(low_issues.empty());
    CHECK(low_issues.front().path == "route.rules[0].dscp");

    auto high_issues = parse_issues(R"({"route":{"rules":[{"outbound":"vpn","dscp":64}]}})");
    REQUIRE_FALSE(high_issues.empty());
    CHECK(high_issues.front().path == "route.rules[0].dscp");

    auto type_issues = parse_issues(R"({"route":{"rules":[{"outbound":"vpn","dscp":"46"}]}})");
    REQUIRE_FALSE(type_issues.empty());
    CHECK(type_issues.front().path == "route.rules[0].dscp");
}

TEST_CASE("route rule: firewall criteria carries dscp") {
    auto cfg = parse_config(R"({"route":{"rules":[{"outbound":"vpn","dscp":63}]}})");
    REQUIRE(cfg.route.has_value());
    REQUIRE(cfg.route->rules.has_value());
    auto criteria = build_firewall_rule_criteria(cfg.route->rules->front());
    REQUIRE(criteria.dscp.has_value());
    CHECK(*criteria.dscp == 63);
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

TEST_CASE("route rule: iptables rejects multiport src_port combined with dest_port") {
    const auto issues = validate_issues(R"({
        "daemon":{"firewall_backend":"iptables"},
        "outbounds":[{"tag":"vpn","type":"interface","interface":"eth0"}],
        "route":{"rules":[
            {"outbound":"vpn","src_port":"555,666","dest_port":"555-666"}
        ]}
    })");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "route.rules[0].src_port");
    CHECK(issues[0].message.find("This is a xt_multiport module limitation") != std::string::npos);
}

TEST_CASE("route rule: iptables rejects multiport dest_port combined with src_port") {
    const auto issues = validate_issues(R"({
        "daemon":{"firewall_backend":"iptables"},
        "outbounds":[{"tag":"vpn","type":"interface","interface":"eth0"}],
        "route":{"rules":[
            {"outbound":"vpn","src_port":"555-666","dest_port":"555,666"}
        ]}
    })");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "route.rules[0].dest_port");
}

TEST_CASE("route rule: iptables allows src_port and dest_port ranges together") {
    CHECK_NOTHROW(parse_test_config(R"({
        "daemon":{"firewall_backend":"iptables"},
        "outbounds":[{"tag":"vpn","type":"interface","interface":"eth0"}],
        "route":{"rules":[
            {"outbound":"vpn","src_port":"555-666","dest_port":"777-888"}
        ]}
    })"));
}

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("route rule: nftables allows mixed multiport and dest_port") {
    CHECK_NOTHROW(parse_test_config(R"({
        "daemon":{"firewall_backend":"nftables"},
        "outbounds":[{"tag":"vpn","type":"interface","interface":"eth0"}],
        "route":{"rules":[
            {"outbound":"vpn","src_port":"555,666","dest_port":"555-666"}
        ]}
    })"));
}
#endif

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("route rule: auto allows mixed multiport and dest_port") {
    CHECK_NOTHROW(parse_test_config(R"({
        "daemon":{"firewall_backend":"auto"},
        "outbounds":[{"tag":"vpn","type":"interface","interface":"eth0"}],
        "route":{"rules":[
            {"outbound":"vpn","src_port":"555,666","dest_port":"555-666"}
        ]}
    })"));
}
#endif

TEST_CASE("route inbound_interfaces: omitted is accepted") {
    CHECK_NOTHROW(parse_test_config(R"({"lists":{"ads":{"domains":["example.com"]}},"outbounds":[{"tag":"vpn","type":"interface","interface":"eth0"}],"route":{"rules":[{"list":["ads"],"outbound":"vpn"}]}})"));
}

TEST_CASE("route inbound_interfaces: empty array is accepted") {
    CHECK_NOTHROW(parse_test_config(R"({"lists":{"ads":{"domains":["example.com"]}},"outbounds":[{"tag":"vpn","type":"interface","interface":"eth0"}],"route":{"inbound_interfaces":[],"rules":[{"list":["ads"],"outbound":"vpn"}]}})"));
}

TEST_CASE("route inbound_interfaces: valid entries are parsed") {
    auto cfg = parse_test_config(
        R"({"lists":{"ads":{"domains":["example.com"]}},"outbounds":[{"tag":"vpn","type":"interface","interface":"eth0"}],"route":{"inbound_interfaces":["br0","wg0"],"rules":[{"list":["ads"],"outbound":"vpn"}]}})");
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

TEST_CASE("route inbound_interfaces: restore control characters are rejected") {
    const auto issues = parse_issues(
        "{\"route\":{\"inbound_interfaces\":[\"br0\\n-A KeenPbrTable -j DROP\"],"
        "\"rules\":[{\"list\":[\"ads\"],\"outbound\":\"vpn\"}]}}");
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().path == "route.inbound_interfaces[0]");
}

TEST_CASE("route inbound_interfaces: Linux-invalid names are rejected") {
    for (const std::string& iface : {".", "..", "bad/name", "bad:name",
                                     "bad name", "bad\"name", "bad\\name",
                                     "eth+", "0123456789abcdef"}) {
        const auto issues = parse_issues(
            "{\"route\":{\"inbound_interfaces\":[" +
            nlohmann::json(iface).dump() +
            "],\"rules\":[{\"list\":[\"ads\"],\"outbound\":\"vpn\"}]}}");
        CAPTURE(iface);
        REQUIRE_FALSE(issues.empty());
        CHECK(issues.front().path == "route.inbound_interfaces[0]");
    }
}

TEST_CASE("route inbound_interfaces: valid future interface need not exist") {
    CHECK_NOTHROW(parse_test_config(
        R"({"route":{"inbound_interfaces":["vpn_future@1"],"rules":[]}})"));
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

TEST_CASE("iproute.table_start: value 150 is accepted") {
    CHECK_NOTHROW(parse_test_config(R"({"iproute":{"table_start":150}})"));
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

TEST_CASE("iproute.process_router_traffic: boolean accepted, default unset") {
    CHECK_NOTHROW(parse_test_config(R"({"iproute":{"process_router_traffic":true}})"));
    CHECK_NOTHROW(parse_test_config(R"({"iproute":{"process_router_traffic":false}})"));
    CHECK_THROWS_AS(
        parse_test_config(R"({"iproute":{"process_router_traffic":"yes"}})"),
        ConfigValidationError
    );
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

TEST_CASE("fwmark mask: single F nibble is accepted during config parsing") {
    const std::string json = R"({
        "fwmark": {
            "mask": "0x000F0000"
        }
    })";

    CHECK_NOTHROW(parse_test_config(json));
}

TEST_CASE("fwmark mask: multiple consecutive F nibbles are accepted during config parsing") {
    const std::string json = R"({
        "fwmark": {
            "mask": "0x0FFF0000"
        }
    })";

    CHECK_NOTHROW(parse_test_config(json));
}

TEST_CASE("fwmark mask: non-consecutive F nibbles are rejected during config parsing") {
    const std::string json = R"({
        "fwmark": {
            "mask": "0x0F0F0000"
        }
    })";

    CHECK_THROWS_AS(parse_test_config(json), ConfigValidationError);
}

TEST_CASE("fwmark mask: validator rejects more routable outbounds than mask allows") {
    nlohmann::json config;
    config["fwmark"] = {
        {"mask", "0x0000F000"}
    };
    config["outbounds"] = nlohmann::json::array();

    for (int i = 0; i < 17; ++i) {
        config["outbounds"].push_back({
            {"tag", "wan" + std::to_string(i)},
            {"type", "interface"},
            {"interface", "wg" + std::to_string(i)}
        });
    }

    const auto issues = validate_issues(config.dump());
    REQUIRE_FALSE(issues.empty());

    bool saw_capacity_error = false;
    for (const auto& issue : issues) {
        if (issue.path != "outbounds") {
            continue;
        }

        if (issue.message.find("maximum 16 supported with current fwmark.mask") !=
            std::string::npos) {
            saw_capacity_error = true;
            break;
        }
    }

    CHECK(saw_capacity_error);
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

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("daemon.firewall_backend: accepts nftables") {
    auto cfg = parse_test_config(R"({"daemon":{"firewall_backend":"nftables"}})");
    CHECK(firewall_backend_preference(cfg) == FirewallBackendPreference::nftables);
}
#endif

TEST_CASE("daemon.firewall_backend: rejects non-string value") {
    const auto issues = parse_issues(R"({"daemon":{"firewall_backend":true}})");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "daemon.firewall_backend");
}

TEST_CASE("daemon.firewall_backend: rejects unsupported value") {
    CHECK_THROWS_AS(parse_test_config(R"({"daemon":{"firewall_backend":"pf"}})"), ConfigError);
}

TEST_CASE("daemon.skip_marked_packets: defaults to true behavior when absent") {
    auto cfg = parse_test_config(R"({"daemon":{}})");
    REQUIRE(cfg.daemon.has_value());
    CHECK_FALSE(cfg.daemon->skip_marked_packets.has_value());
}

TEST_CASE("daemon.skip_marked_packets: accepts true") {
    auto cfg = parse_test_config(R"({"daemon":{"skip_marked_packets":true}})");
    REQUIRE(cfg.daemon.has_value());
    REQUIRE(cfg.daemon->skip_marked_packets.has_value());
    CHECK(*cfg.daemon->skip_marked_packets);
}

TEST_CASE("daemon.skip_marked_packets: accepts false") {
    auto cfg = parse_test_config(R"({"daemon":{"skip_marked_packets":false}})");
    REQUIRE(cfg.daemon.has_value());
    REQUIRE(cfg.daemon->skip_marked_packets.has_value());
    CHECK_FALSE(*cfg.daemon->skip_marked_packets);
}

TEST_CASE("daemon.skip_marked_packets: accepts null") {
    auto cfg = parse_test_config(R"({"daemon":{"skip_marked_packets":null}})");
    REQUIRE(cfg.daemon.has_value());
    CHECK_FALSE(cfg.daemon->skip_marked_packets.has_value());
}

TEST_CASE("daemon.skip_marked_packets: rejects non-boolean value") {
    const auto issues = parse_issues(R"({"daemon":{"skip_marked_packets":"yes"}})");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "daemon.skip_marked_packets");
}

TEST_CASE("daemon.clear_dynamic_sets_on_apply: accepts explicit policy") {
    auto enabled = parse_test_config(
        R"({"daemon":{"clear_dynamic_sets_on_apply":true}})");
    auto disabled = parse_test_config(
        R"({"daemon":{"clear_dynamic_sets_on_apply":false}})");
    REQUIRE(enabled.daemon->clear_dynamic_sets_on_apply.has_value());
    REQUIRE(disabled.daemon->clear_dynamic_sets_on_apply.has_value());
    CHECK(*enabled.daemon->clear_dynamic_sets_on_apply);
    CHECK_FALSE(*disabled.daemon->clear_dynamic_sets_on_apply);
}

TEST_CASE("daemon.clear_dynamic_sets_on_apply: null uses default behavior") {
    auto cfg = parse_test_config(
        R"({"daemon":{"clear_dynamic_sets_on_apply":null}})");
    REQUIRE(cfg.daemon.has_value());
    CHECK_FALSE(cfg.daemon->clear_dynamic_sets_on_apply.has_value());
}

TEST_CASE("daemon.clear_dynamic_sets_on_apply: rejects non-boolean value") {
    const auto issues = parse_issues(
        R"({"daemon":{"clear_dynamic_sets_on_apply":"yes"}})");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "daemon.clear_dynamic_sets_on_apply");
}

TEST_CASE("daemon ipset capacities: accept positive uint32 values") {
    const auto cfg = parse_test_config(
        R"({"daemon":{"ipset_hashsize":1024,"ipset_maxelem":65536}})");
    REQUIRE(cfg.daemon.has_value());
    REQUIRE(cfg.daemon->ipset_hashsize.has_value());
    REQUIRE(cfg.daemon->ipset_maxelem.has_value());
    CHECK(*cfg.daemon->ipset_hashsize == 1024);
    CHECK(*cfg.daemon->ipset_maxelem == 65536);
}

TEST_CASE("daemon ipset capacities: omitted and null remain unset") {
    const auto omitted = parse_test_config(R"({"daemon":{}})");
    const auto null_values = parse_test_config(
        R"({"daemon":{"ipset_hashsize":null,"ipset_maxelem":null}})");
    REQUIRE(omitted.daemon.has_value());
    REQUIRE(null_values.daemon.has_value());
    CHECK_FALSE(omitted.daemon->ipset_hashsize.has_value());
    CHECK_FALSE(omitted.daemon->ipset_maxelem.has_value());
    CHECK_FALSE(null_values.daemon->ipset_hashsize.has_value());
    CHECK_FALSE(null_values.daemon->ipset_maxelem.has_value());
}

TEST_CASE("daemon ipset capacities: reject non-positive and out-of-range values") {
    for (const auto& field : {"ipset_hashsize", "ipset_maxelem"}) {
        for (const auto& value : {"0", "-1", "4294967296"}) {
            const auto issues = validate_issues(
                std::string("{\"daemon\":{\"") + field + "\":" + value + "}}");
            REQUIRE(issues.size() == 1);
            CHECK(issues[0].path == std::string("daemon.") + field);
        }
    }
    const auto hashsize_overflow = validate_issues(
        R"({"daemon":{"ipset_hashsize":2147483649}})");
    REQUIRE(hashsize_overflow.size() == 1);
    CHECK(hashsize_overflow[0].path == "daemon.ipset_hashsize");
}

TEST_CASE("daemon ipset capacities: reject non-integer values") {
    const auto issues = parse_issues(
        R"({"daemon":{"ipset_hashsize":"1024","ipset_maxelem":1.5}})");
    REQUIRE(issues.size() == 2);
    CHECK(issues[0].path == "daemon.ipset_hashsize");
    CHECK(issues[1].path == "daemon.ipset_maxelem");
}

TEST_CASE("daemon.reuse_static_sets_on_runtime_refresh: accepts explicit policy") {
    auto enabled = parse_test_config(
        R"({"daemon":{"reuse_static_sets_on_runtime_refresh":true}})");
    auto disabled = parse_test_config(
        R"({"daemon":{"reuse_static_sets_on_runtime_refresh":false}})");
    REQUIRE(enabled.daemon->reuse_static_sets_on_runtime_refresh.has_value());
    REQUIRE(disabled.daemon->reuse_static_sets_on_runtime_refresh.has_value());
    CHECK(*enabled.daemon->reuse_static_sets_on_runtime_refresh);
    CHECK_FALSE(*disabled.daemon->reuse_static_sets_on_runtime_refresh);
}

TEST_CASE("daemon.reuse_static_sets_on_runtime_refresh: null uses default behavior") {
    auto cfg = parse_test_config(
        R"({"daemon":{"reuse_static_sets_on_runtime_refresh":null}})");
    REQUIRE(cfg.daemon.has_value());
    CHECK_FALSE(cfg.daemon->reuse_static_sets_on_runtime_refresh.has_value());
}

TEST_CASE("daemon.reuse_static_sets_on_runtime_refresh: rejects non-boolean value") {
    const auto issues = parse_issues(
        R"({"daemon":{"reuse_static_sets_on_runtime_refresh":"yes"}})");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "daemon.reuse_static_sets_on_runtime_refresh");
}

TEST_CASE("daemon.ipv6_enabled: defaults to true behavior when absent") {
    auto cfg = parse_test_config(R"({"daemon":{}})");
    REQUIRE(cfg.daemon.has_value());
    CHECK_FALSE(cfg.daemon->ipv6_enabled.has_value());
}

TEST_CASE("daemon.ipv6_enabled: accepts false") {
    auto cfg = parse_test_config(R"({"daemon":{"ipv6_enabled":false}})");
    REQUIRE(cfg.daemon.has_value());
    REQUIRE(cfg.daemon->ipv6_enabled.has_value());
    CHECK_FALSE(*cfg.daemon->ipv6_enabled);
}

TEST_CASE("daemon.ipv6_enabled: accepts null") {
    auto cfg = parse_test_config(R"({"daemon":{"ipv6_enabled":null}})");
    REQUIRE(cfg.daemon.has_value());
    CHECK_FALSE(cfg.daemon->ipv6_enabled.has_value());
}

TEST_CASE("daemon.ipv6_enabled: rejects non-boolean value") {
    const auto issues = parse_issues(R"({"daemon":{"ipv6_enabled":"yes"}})");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "daemon.ipv6_enabled");
}

TEST_CASE("route rule: unknown outbound tag is rejected") {
    const auto issues = validate_issues(R"({
        "lists":{"blocked":{"ip_cidrs":["10.0.0.0/8"]}},
        "outbounds":[{"tag":"wan","type":"interface","interface":"eth0"}],
        "route":{"rules":[{"list":["blocked"],"outbound":"missing"}]}
    })");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "route.rules[0].outbound");
    CHECK(issues[0].message.find("unknown outbound") != std::string::npos);
}

TEST_CASE("route rule: unknown list name is rejected") {
    const auto issues = validate_issues(R"({
        "lists":{"blocked":{"ip_cidrs":["10.0.0.0/8"]}},
        "outbounds":[{"tag":"wan","type":"interface","interface":"eth0"}],
        "route":{"rules":[{"list":["ghost"],"outbound":"wan"}]}
    })");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "route.rules[0].list[0]");
    CHECK(issues[0].message.find("unknown list") != std::string::npos);
}

TEST_CASE("interface outbound: empty interface name is rejected") {
    const auto issues = validate_issues(R"({
        "outbounds":[{"tag":"wan","type":"interface","interface":""}]
    })");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "outbounds[0].interface");
}

TEST_CASE("daemon execution timeout must be positive") {
    const auto issues = validate_issues(R"({"daemon":{"exec_timeout_seconds":0}})");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "daemon.exec_timeout_seconds");
}

TEST_CASE("daemon execution kill grace may be zero but not negative") {
    CHECK(validate_issues(R"({"daemon":{"exec_kill_grace_seconds":0}})").empty());
    const auto issues = validate_issues(R"({"daemon":{"exec_kill_grace_seconds":-1}})");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "daemon.exec_kill_grace_seconds");
}

TEST_CASE("iproute rule priority start must be positive") {
    const auto issues = validate_issues(R"({"iproute":{"rule_priority_start":0}})");
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "iproute.rule_priority_start");
}

TEST_CASE("device name may be empty and is limited to 128 characters") {
    CHECK(validate_issues(R"({"device_name":""})").empty());
    CHECK(validate_issues(R"({"device_name":"Home router"})").empty());

    std::string unicode_name;
    for (int i = 0; i < 128; ++i) unicode_name += "я";
    CHECK(validate_issues(nlohmann::json{{"device_name", unicode_name}}.dump()).empty());

    const std::string long_name(129, 'x');
    const auto issues = validate_issues(nlohmann::json{{"device_name", long_name}}.dump());
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].path == "device_name");
}

TEST_CASE("intercept: defaults are accepted and absent values stay optional") {
    const auto cfg = parse_test_config(R"({"intercept":{}})");
    REQUIRE(cfg.intercept.has_value());
    CHECK_FALSE(cfg.intercept->enabled.has_value());
    const auto full = parse_test_config(R"({"intercept":{
      "enabled":true,"min_ttl_ms":300000,"max_ttl_ms":86400000,
      "dns":{"enabled":true,"queue_num":9053,"hold_timeout_ms":30,
             "marker":{"domain":"check.keen.pbr","answer_ipv4":"127.0.0.88"}},
      "l7":{"enabled":true,"nflog_group":9054,"tls":true,"http":true,"quic":true}}})");
    CHECK(*full.intercept->dns->queue_num == 9053);
    CHECK(*full.intercept->l7->nflog_group == 9054);
    CHECK_NOTHROW(parse_test_config(nlohmann::json(full).dump()));
}

TEST_CASE("intercept: rejects invalid queue/group numbers") {
    for (const char* bad : {"0", "65536", "-1", "64511", "65023"}) {
        const auto q = validate_issues(std::string(R"({"intercept":{"dns":{"queue_num":)") + bad + "}}}");
        REQUIRE(q.size() == 1);
        CHECK(q[0].path == "intercept.dns.queue_num");
        const auto g = validate_issues(std::string(R"({"intercept":{"l7":{"nflog_group":)") + bad + "}}}");
        REQUIRE(g.size() == 1);
        CHECK(g[0].path == "intercept.l7.nflog_group");
    }
    CHECK(validate_issues(R"({"intercept":{"dns":{"queue_num":1},"l7":{"nflog_group":65535}}})").empty());
}

TEST_CASE("intercept: hold timeout, ttl range and marker are validated") {
    CHECK(validate_issues(R"({"intercept":{"dns":{"hold_timeout_ms":5}}})").empty());
    CHECK(validate_issues(R"({"intercept":{"dns":{"hold_timeout_ms":500}}})").empty());
    for (const char* bad : {"4", "501", "0"}) {
        const auto issues = validate_issues(
            std::string(R"({"intercept":{"dns":{"hold_timeout_ms":)") + bad + "}}}");
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].path == "intercept.dns.hold_timeout_ms");
    }

    auto ttl = validate_issues(R"({"intercept":{"min_ttl_ms":600000,"max_ttl_ms":300000}})");
    REQUIRE(ttl.size() == 1);
    CHECK(ttl[0].path == "intercept.min_ttl_ms");
    CHECK(validate_issues(R"({"intercept":{"min_ttl_ms":300000,"max_ttl_ms":300000}})").empty());
    CHECK(validate_issues(R"({"intercept":{"min_ttl_ms":0}})").size() == 1);
    // A lone min above the default max is also inconsistent.
    CHECK(validate_issues(R"({"intercept":{"min_ttl_ms":90000000}})").size() == 1);
    CHECK(validate_issues(R"({"intercept":{"min_ttl_ms":4294967295999,"max_ttl_ms":4294967295999}})").empty());
    CHECK_FALSE(validate_issues(R"({"intercept":{"min_ttl_ms":4294967296000}})").empty());
    const auto global_ttl_overflow = validate_issues(
        R"({"intercept":{"min_ttl_ms":4294967296000,"max_ttl_ms":4294967296000}})");
    REQUIRE(global_ttl_overflow.size() == 2);
    CHECK(global_ttl_overflow[0].path == "intercept.min_ttl_ms");
    CHECK(global_ttl_overflow[1].path == "intercept.max_ttl_ms");
    const auto max_ttl_overflow =
        validate_issues(R"({"intercept":{"min_ttl_ms":1000,"max_ttl_ms":4294967296000}})");
    REQUIRE(max_ttl_overflow.size() == 1);
    CHECK(max_ttl_overflow[0].path == "intercept.max_ttl_ms");
    CHECK(validate_issues(R"({"intercept":{"min_ttl_ms":1001,"max_ttl_ms":1999}})").empty());
    CHECK(validate_issues(R"({"intercept":{"min_ttl_ms":999}})").size() == 1);
    CHECK(validate_issues(R"({"intercept":{"min_ttl_ms":1999,"max_ttl_ms":1001}})").size() == 1);
    CHECK_FALSE(validate_issues(R"({"intercept":{"max_ttl_ms":999}})").empty());
    CHECK(validate_issues(R"({"lists":{"ttl":{"domains":["example.com"],"ttl_ms":4294967295999}}})").empty());
    for (const auto* bad : {"-1", "4294967296000"}) {
        const auto issues = validate_issues(
            std::string(R"({"lists":{"ttl":{"domains":["example.com"],"ttl_ms":)") + bad + "}}}");
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].path == "lists.ttl.ttl_ms");
    }

    for (const char* bad : {"", "-bad.example", "a..b", "bad domain", "a.b-"}) {
        const auto issues = validate_issues(
            std::string(R"({"intercept":{"dns":{"marker":{"domain":")") + bad + R"("}}}})");
        REQUIRE(issues.size() == 1);
        CHECK(issues[0].path == "intercept.dns.marker.domain");
    }
    CHECK(validate_issues(R"({"intercept":{"dns":{"marker":{"domain":"check.example.org"}}}})").empty());

    const auto ip = validate_issues(R"({"intercept":{"dns":{"marker":{"answer_ipv4":"999.1.1.1"}}}})");
    REQUIRE(ip.size() == 1);
    CHECK(ip[0].path == "intercept.dns.marker.answer_ipv4");
    CHECK(validate_issues(R"({"intercept":{"dns":{"marker":{"answer_ipv4":"::1"}}}})").size() == 1);
}

TEST_CASE("config warnings: deprecated resolver fields are reported in one warning") {
    auto cfg = parse_config(R"({
        "lists": {"l": {"domains": ["example.com"]}},
        "daemon": {"resolver_ready_timeout_seconds": 30},
        "dns": {
            "servers": [{"tag":"plain_dns","address":"8.8.8.8"}],
            "rules": [{"list":["l"],"server":"plain_dns"}],
            "fallback": ["plain_dns"],
            "system_resolver": {"address": "127.0.0.1"},
            "resolver_integration": "dnsmasq"
        }
    })");
    CHECK_NOTHROW(validate_config(cfg));
    const auto warnings = config_warnings(cfg);
    REQUIRE(warnings.size() == 1);
    for (const char* field : {"dns.system_resolver",
                              "daemon.resolver_ready_timeout_seconds"}) {
        CAPTURE(field);
        CHECK(warnings.front().find(field) != std::string::npos);
    }
    CHECK(warnings.front().find("dns.rules") == std::string::npos);
    CHECK(warnings.front().find("ignored: deprecated since 3.0.0") != std::string::npos);
}

TEST_CASE("config warnings: no deprecation warning without deprecated fields") {
    auto cfg = parse_config(R"({
        "dns": {"servers": [{"tag":"plain_dns","address":"8.8.8.8"}]}
    })");
    CHECK(config_warnings(cfg).empty());
    CHECK(config_warnings(parse_config("{}")).empty());
}

TEST_CASE("config warnings: rules and fallback are ignored when integration is none") {
    const char* base = R"({
        "lists": {"l": {"domains": ["example.com"]}},
        "dns": {
            "servers": [{"tag":"plain_dns","address":"8.8.8.8"}],
            "resolver_integration": "none",
            %s
        }
    })";
    auto render = [&](const char* body) {
        char buf[1024];
        std::snprintf(buf, sizeof(buf), base, body);
        return parse_config(buf);
    };
    for (const char* body : {R"("rules": [{"list":["l"],"server":"plain_dns"}])",
                             R"("fallback": ["plain_dns"])"}) {
        const auto warnings = config_warnings(render(body));
        REQUIRE(warnings.size() == 1);
        CHECK(warnings.front() ==
              "dns.rules/dns.fallback ignored: dns.resolver_integration is \"none\"");
    }
    CHECK(config_warnings(render(R"("rules": [], "fallback": [])")).empty());
}

TEST_CASE("effective resolver integration: explicit value wins, absent follows dns.rules") {
    auto mode = [](const char* json) {
        return effective_resolver_integration(parse_config(json));
    };
    CHECK(mode(R"({})") == ResolverIntegrationMode::NONE);
    CHECK(mode(R"({"dns":{"resolver_integration":"none",
        "rules":[{"list":["l"],"server":"s"}]}})") == ResolverIntegrationMode::NONE);
    CHECK(mode(R"({"dns":{"resolver_integration":"dnsmasq"}})") ==
          ResolverIntegrationMode::DNSMASQ);
    CHECK(mode(R"({"dns":{"rules":[{"list":["l"],"server":"s"}]}})") ==
          ResolverIntegrationMode::DNSMASQ);
    CHECK(mode(R"({"dns":{"rules":[]}})") == ResolverIntegrationMode::NONE);
    CHECK(mode(R"({"dns":{"system_resolver":{"address":"127.0.0.1"}}})") ==
          ResolverIntegrationMode::NONE);
    CHECK(std::string(resolver_integration_name(ResolverIntegrationMode::DNSMASQ)) == "dnsmasq");
    CHECK(std::string(resolver_integration_name(ResolverIntegrationMode::NONE)) == "none");
}

TEST_CASE("dns rules validation: unknown server, list and fallback tags are rejected") {
    const std::string lists = R"("lists":{"l":{"domains":["example.com"]}},)";
    const auto ok = validate_issues("{" + lists +
        R"("dns":{"servers":[{"tag":"s","address":"8.8.8.8"}],
        "rules":[{"list":["l"],"server":"s"}],"fallback":["s"]}})");
    CHECK(ok.empty());

    const auto bad_server = validate_issues("{" + lists +
        R"("dns":{"servers":[{"tag":"s","address":"8.8.8.8"}],
        "rules":[{"list":["l"],"server":"nope"}]}})");
    REQUIRE(bad_server.size() == 1);
    CHECK(bad_server[0].path == "dns.rules[0].server");
    CHECK(bad_server[0].message.find("unknown DNS server tag 'nope'") != std::string::npos);

    const auto bad_list = validate_issues("{" + lists +
        R"("dns":{"servers":[{"tag":"s","address":"8.8.8.8"}],
        "rules":[{"list":["missing"],"server":"s"}]}})");
    REQUIRE(bad_list.size() == 1);
    CHECK(bad_list[0].path == "dns.rules[0].list[0]");

    const auto bad_fallback = validate_issues(
        R"({"dns":{"servers":[{"tag":"s","address":"8.8.8.8"}],"fallback":["nope"]}})");
    REQUIRE(bad_fallback.size() == 1);
    CHECK(bad_fallback[0].path == "dns.fallback[0]");
    CHECK(bad_fallback[0].message.find("unknown DNS server tag") != std::string::npos);
}

TEST_CASE("shipped example configs validate and include default local_networks rule") {
    // Read and parse each shipped example config file
    std::vector<std::pair<std::string, std::string>> example_configs = {
        {"config.example.json", "config.example.json"},
        {"packages/common/config.full.example.json", "packages/common/config.full.example.json"},
        {"packages/common/config.headless.example.json", "packages/common/config.headless.example.json"},
        {"packages/keenetic/keen-pbr/files/opt/etc/keen-pbr/config.full.example.json", "packages/keenetic/keen-pbr/files/opt/etc/keen-pbr/config.full.example.json"},
        {"packages/keenetic/keen-pbr/files/opt/etc/keen-pbr/config.headless.example.json", "packages/keenetic/keen-pbr/files/opt/etc/keen-pbr/config.headless.example.json"}
    };

    for (const auto& entry : example_configs) {
        const std::string& label = entry.first;
        const std::string& path = entry.second;
        CAPTURE(label);

        // Read the JSON file
        std::ifstream file(path);
        REQUIRE(file.is_open());
        nlohmann::json json;
        REQUIRE_NOTHROW(json = nlohmann::json::parse(file));

        // Verify lists contains local_networks
        REQUIRE(json.contains("lists"));
        REQUIRE(json["lists"].contains("local_networks"));
        const auto& local_nets = json["lists"]["local_networks"];
        REQUIRE(local_nets.contains("ip_cidrs"));
        REQUIRE(local_nets["ip_cidrs"].is_array());
        // Check that it contains at least the core local ranges
        auto cidrs_json = local_nets["ip_cidrs"];
        std::vector<std::string> cidrs;
        for (const auto& cidr : cidrs_json) {
            cidrs.push_back(cidr);
        }
        CHECK(std::find(cidrs.begin(), cidrs.end(), "127.0.0.0/8") != cidrs.end());
        CHECK(std::find(cidrs.begin(), cidrs.end(), "10.0.0.0/8") != cidrs.end());
        CHECK(std::find(cidrs.begin(), cidrs.end(), "192.168.0.0/16") != cidrs.end());

        // Verify outbounds contains an ignore type with tag direct_local
        REQUIRE(json.contains("outbounds"));
        REQUIRE(json["outbounds"].is_array());
        bool found_direct_local = false;
        for (const auto& ob : json["outbounds"]) {
            if (ob.contains("tag") && ob["tag"] == "direct_local") {
                REQUIRE(ob.contains("type"));
                CHECK(ob["type"] == "ignore");
                found_direct_local = true;
                break;
            }
        }
        CHECK(found_direct_local);

        // Verify route.rules exists and first rule targets local_networks to direct_local
        REQUIRE(json.contains("route"));
        REQUIRE(json["route"].contains("rules"));
        REQUIRE(json["route"]["rules"].is_array());
        REQUIRE(json["route"]["rules"].size() > 0);

        const auto& first_rule = json["route"]["rules"][0];
        REQUIRE(first_rule.contains("list"));
        REQUIRE(first_rule["list"].is_array());
        CHECK(first_rule["list"][0] == "local_networks");
        REQUIRE(first_rule.contains("outbound"));
        CHECK(first_rule["outbound"] == "direct_local");

        // Parse as config and validate it
        Config cfg = parse_test_config(json.dump());
        CHECK_NOTHROW(validate_config(cfg));
    }
}

// =============================================================================
// outbound_groups: unified `members` schema and legacy upgrade
// =============================================================================

namespace {

const char* const kMainFormatConfig = R"({
  "outbounds":[
    {"type":"interface","tag":"a","interface":"eth0"},
    {"type":"interface","tag":"b","interface":"eth1"},
    {"type":"interface","tag":"c","interface":"eth2"},
    {"type":"interface","tag":"d","interface":"eth3"},
    {"type":"urltest","tag":"web","url":"http://example.test",
     "outbound_groups":[
       {"weight":3,"outbounds":["c"]},
       {"weight":1,"outbounds":["a","b"]},
       {"outbounds":["d"]}]},
    {"type":"icmptest","tag":"ping","interval_ms":60000,
     "outbound_groups":[
       {"weight":2,"candidates":[{"outbound":"c","target":"9.9.9.9"}]},
       {"candidates":[{"outbound":"a","target":"1.1.1.1"},
                      {"outbound":"b","target":"2606:4700:4700::1111"}]}]}
  ]})";

std::vector<std::string> member_tags(const Outbound& outbound, size_t group) {
    return outbound_group_tags(outbound.outbound_groups->at(group));
}

} // namespace

TEST_CASE("legacy urltest and icmptest groups normalize to members in weight order") {
    const auto cfg = parse_test_config(kMainFormatConfig);
    const auto& web = cfg.outbounds->at(4);
    REQUIRE(web.outbound_groups->size() == 3);
    // Stable sort by legacy weight (default 1): [a,b] (1), [d] (1), [c] (3).
    CHECK(member_tags(web, 0) == std::vector<std::string>{"a", "b"});
    CHECK(member_tags(web, 1) == std::vector<std::string>{"d"});
    CHECK(member_tags(web, 2) == std::vector<std::string>{"c"});
    for (const auto& group : *web.outbound_groups) {
        CHECK_FALSE(group.outbounds.has_value());
        CHECK_FALSE(group.candidates.has_value());
        CHECK_FALSE(group.weight.has_value());
        for (const auto& member : *group.members) {
            CHECK_FALSE(member.target.has_value());
            CHECK_FALSE(member.weight.has_value());
        }
    }
    const auto& ping = cfg.outbounds->at(5);
    REQUIRE(ping.outbound_groups->size() == 2);
    CHECK(member_tags(ping, 0) == std::vector<std::string>{"a", "b"});
    CHECK(member_tags(ping, 1) == std::vector<std::string>{"c"});
    CHECK(outbound_group_target(ping.outbound_groups->at(0), "b") ==
          "2606:4700:4700::1111");
    CHECK(outbound_group_target(ping.outbound_groups->at(1), "c") == "9.9.9.9");
}

TEST_CASE("upgraded main-format config serializes to members and round-trips") {
    const auto cfg = parse_test_config(kMainFormatConfig);
    const auto json = nlohmann::json(cfg);
    for (const auto& outbound : json["outbounds"]) {
        if (!outbound.contains("outbound_groups")) continue;
        for (const auto& group : outbound["outbound_groups"]) {
            CHECK(group["candidates"].is_null());
            CHECK(group["outbounds"].is_null());
            CHECK(group["weight"].is_null());
            CHECK(group["members"].is_array());
        }
    }
    const auto serialized = json.dump();
    const auto reparsed = parse_test_config(serialized);
    CHECK(nlohmann::json(reparsed) == nlohmann::json(cfg));
    CHECK_FALSE(upgraded_config_text(serialized).has_value());
}

TEST_CASE("upgraded_config_text decides whether the file needs a rewrite") {
    const auto upgraded = upgraded_config_text(kMainFormatConfig);
    REQUIRE(upgraded.has_value());
    CHECK(upgraded->find("\"members\"") != std::string::npos);
    CHECK(upgraded->find("\"candidates\"") == std::string::npos);
    CHECK(upgraded->find("\"weight\"") == std::string::npos);
    // The written text is itself valid, and needs no further upgrade.
    CHECK_NOTHROW(parse_test_config(*upgraded));
    CHECK(nlohmann::json(parse_test_config(*upgraded)) ==
          nlohmann::json(parse_test_config(kMainFormatConfig)));
    CHECK_FALSE(upgraded_config_text(*upgraded).has_value());
    // Canonical, unrelated, and malformed input is left alone.
    CHECK_FALSE(upgraded_config_text(R"({"outbounds":[
      {"type":"urltest","tag":"u","outbound_groups":[{"members":[{"outbound":"a"}]}]}]})")
                    .has_value());
    CHECK_FALSE(upgraded_config_text("{}").has_value());
    CHECK_FALSE(upgraded_config_text("not json").has_value());
}

TEST_CASE("canonical members keep order, target and weight") {
    const auto cfg = parse_test_config(R"({"outbounds":[
      {"type":"interface","tag":"a","interface":"eth0"},
      {"type":"interface","tag":"b","interface":"eth1"},
      {"type":"urltest","tag":"web","url":"http://example.test",
       "outbound_groups":[{"members":[{"outbound":"a","weight":7},{"outbound":"b"}]}]}
    ]})");
    const auto& group = cfg.outbounds->at(2).outbound_groups->at(0);
    CHECK(outbound_group_balance_weight(group, "a") == 7);
    CHECK(outbound_group_balance_weight(group, "b") == 1);
    CHECK(outbound_group_balance_weight(group, "missing") == 1);
}

TEST_CASE("outbound_group members are validated") {
    const auto wrap = [](const std::string& type_fields, const std::string& members) {
        return R"({"outbounds":[
          {"type":"interface","tag":"a","interface":"eth0"},
          {"type":"interface","tag":"b","interface":"eth1"},
          {)" + type_fields + R"(,"outbound_groups":[)" + members + R"(]}]})";
    };
    const std::string urltest = R"("type":"urltest","tag":"u","url":"http://example.test")";
    const std::string icmptest = R"("type":"icmptest","tag":"u","interval_ms":60000)";

    SUBCASE("mixing members with legacy fields is rejected") {
        CHECK_FALSE(validate_issues(wrap(urltest,
            R"({"members":[{"outbound":"a"}],"outbounds":["b"]})")).empty());
        CHECK_FALSE(validate_issues(wrap(urltest,
            R"({"members":[{"outbound":"a"}],"weight":2})")).empty());
        CHECK_FALSE(validate_issues(wrap(icmptest,
            R"({"members":[{"outbound":"a","target":"1.1.1.1"}],
                "candidates":[{"outbound":"b","target":"1.1.1.1"}]})")).empty());
    }
    SUBCASE("weight must be between 1 and 100") {
        CHECK(validate_issues(wrap(urltest,
            R"({"members":[{"outbound":"a","weight":1},{"outbound":"b","weight":100}]})")).empty());
        CHECK_FALSE(validate_issues(wrap(urltest,
            R"({"members":[{"outbound":"a","weight":0}]})")).empty());
        CHECK_FALSE(validate_issues(wrap(urltest,
            R"({"members":[{"outbound":"a","weight":101}]})")).empty());
    }
    SUBCASE("unknown and duplicate members are rejected") {
        CHECK_FALSE(validate_issues(wrap(urltest,
            R"({"members":[{"outbound":"nope"}]})")).empty());
        CHECK_FALSE(validate_issues(wrap(urltest,
            R"({"members":[{"outbound":"a"}]},{"members":[{"outbound":"a"}]})")).empty());
    }
    SUBCASE("icmptest members need a literal target, urltest members must not have one") {
        CHECK_FALSE(validate_issues(wrap(icmptest,
            R"({"members":[{"outbound":"a"}]})")).empty());
        CHECK_FALSE(validate_issues(wrap(icmptest,
            R"({"members":[{"outbound":"a","target":"example.com"}]})")).empty());
        CHECK(validate_issues(wrap(icmptest,
            R"({"members":[{"outbound":"a","target":"1.1.1.1"}]})")).empty());
        CHECK_FALSE(validate_issues(wrap(urltest,
            R"({"members":[{"outbound":"a","target":"1.1.1.1"}]})")).empty());
    }
    SUBCASE("a group without members is rejected") {
        CHECK_FALSE(validate_issues(wrap(urltest, R"({"members":[]})")).empty());
        CHECK_FALSE(validate_issues(wrap(urltest, R"({})")).empty());
    }
}

TEST_CASE("upgrade_config_file_if_needed rewrites once and keeps the first backup") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() /
                         ("kpbr-upgrade-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    const std::string path = (dir / "config.json").string();
    const auto read = [](const std::string& file) {
        std::ifstream in(file);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    {
        std::ofstream out(path);
        out << kMainFormatConfig;
    }
    CHECK(upgrade_config_file_if_needed(path, kMainFormatConfig));
    CHECK(read(path + ".bak-pre-members") == kMainFormatConfig);
    const auto upgraded = read(path);
    CHECK(upgraded.find("\"members\"") != std::string::npos);
    CHECK_NOTHROW(parse_test_config(upgraded));
    // Already upgraded: nothing to do, file and backup untouched.
    CHECK_FALSE(upgrade_config_file_if_needed(path, upgraded));
    CHECK(read(path) == upgraded);
    // A later legacy rewrite never overwrites the existing backup.
    CHECK(upgrade_config_file_if_needed(path, R"({"outbounds":[{"type":"urltest","tag":"u","outbound_groups":[{"outbounds":["a"]}]}]})"));
    CHECK(read(path + ".bak-pre-members") == kMainFormatConfig);
    fs::remove_all(dir);
}

TEST_CASE("validation paths address the submitted document by index and key") {
    const auto issues = validate_issues(R"({
        "outbounds":[
            {"type":"interface","tag":"wan","interface":"wan"},
            {"type":"icmptest","tag":"probe","outbound_groups":[
                {"members":[{"outbound":"wan","target":"1.1.1.1"}]},
                {"members":[{"outbound":"wan","target":"not-an-ip"}]}
            ]}
        ],
        "dns":{"servers":[
            {"tag":"a","address":"1.1.1.1"},
            {"tag":"b","address":"8.8.8.8","detour":"missing"}
        ]},
        "lists":{"bad.name":{"domains":[]}}
    })");
    const auto has_path = [&](const std::string& path) {
        return std::any_of(issues.begin(), issues.end(),
                           [&](const auto& issue) { return issue.path == path; });
    };
    CHECK(has_path("outbounds[1].outbound_groups[1].members[0].target"));
    CHECK(has_path("outbounds[1].outbound_groups[1].members[0].outbound"));
    CHECK(has_path("dns.servers[1].detour"));
    CHECK(has_path("lists[\"bad.name\"]"));
}

TEST_CASE("legacy migration issues point at the original json location") {
    const auto issues = validate_issues(R"({
        "outbounds":[
            {"type":"interface","tag":"wan","interface":"wan"},
            {"type":"urltest","tag":"auto","url":"http://example.test",
             "outbound_groups":[{"members":[{"outbound":"wan"}],"outbounds":["wan"]}]}
        ]
    })");
    REQUIRE_FALSE(issues.empty());
    CHECK(issues.front().path == "outbounds[1].outbound_groups[0]");
}
