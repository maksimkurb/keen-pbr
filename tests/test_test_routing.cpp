#include <doctest/doctest.h>

#include "../src/cache/cache_manager.hpp"
#include "../src/cmd/test_routing.hpp"
#include "../src/cmd/test_routing_json.hpp"
#include "../src/util/kernel_capabilities.hpp"
#include "fake_dns_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace keen_pbr3;

namespace {

std::filesystem::path make_temp_dir() {
    char path_template[] = "/tmp/keen-pbr-test-routing-XXXXXX";
    const char* created = mkdtemp(path_template);
    if (created == nullptr) {
        throw std::runtime_error("mkdtemp failed");
    }
    return std::filesystem::path(created);
}

// Replaces the system resolver for the lifetime of the object.
class ScopedDomainResolver {
public:
    explicit ScopedDomainResolver(std::vector<std::string> ips) {
        set_domain_resolver_for_tests(
            [ips = std::move(ips)](const std::string&) { return ips; });
    }
    ~ScopedDomainResolver() { set_domain_resolver_for_tests(nullptr); }
};

// compute_test_routing decides IPv6 from the cached capability snapshot; pin
// it so the tests do not depend on the host (or probe with a stubbed PATH).
class ScopedIpv6Capabilities {
public:
    ScopedIpv6Capabilities() {
        KernelCapabilities caps;
        caps.system_ipv6 = true;
        caps.iptables_ipv6 = true;
        caps.nftables_ipv6 = true;
        set_kernel_capabilities_for_tests(caps);
    }
    ~ScopedIpv6Capabilities() { set_kernel_capabilities_for_tests(std::nullopt); }
};

// Pins the cached host tool lookup so the firewall backend does not depend on
// which test filled the cache first or on the PATH a test overrides.
class ScopedIptablesTools {
public:
    ScopedIptablesTools() {
        HostTools tools;
        tools.iptables = true;
        tools.iptables_restore = true;
        tools.ip6tables = true;
        tools.ip6tables_restore = true;
        tools.ipset = true;
        set_host_tools_for_tests(tools);
    }
    ~ScopedIptablesTools() { set_host_tools_for_tests(std::nullopt); }
};

Config build_test_config() {
    Config config;
    config.lists = std::map<std::string, ListConfig>{};
    config.dns = DnsConfig{};
    return config;
}

class ScopedPathOverride {
public:
    explicit ScopedPathOverride(const std::string& value) {
        if (const char* current = std::getenv("PATH")) {
            previous_ = current;
        }
        (void)setenv("PATH", value.c_str(), 1);
    }

    ~ScopedPathOverride() {
        if (previous_.has_value()) {
            (void)setenv("PATH", previous_->c_str(), 1);
        } else {
            (void)unsetenv("PATH");
        }
    }

private:
    std::optional<std::string> previous_;
};

void write_executable(const std::filesystem::path& path, const std::string& contents) {
    {
        std::ofstream output(path);
        output << contents;
    }
    std::filesystem::permissions(
        path,
        std::filesystem::perms::owner_exec |
            std::filesystem::perms::group_exec |
            std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add);
}

} // namespace

TEST_CASE("compute_test_routing resolves domain through the system resolver") {
    ScopedIpv6Capabilities ipv6_capabilities;
    const auto temp_dir = make_temp_dir();
    CacheManager cache(temp_dir);
    cache.ensure_dir();

    ScopedDomainResolver resolver({"10.0.0.53", "2001:db8::53"});

    Config config = build_test_config();

    const auto list_path = temp_dir / "resolved-ip-list.txt";
    {
        std::ofstream list(list_path);
        list << "10.0.0.53/32\n";
    }
    const auto domain_list_path = temp_dir / "domain-list.txt";
    {
        std::ofstream list(domain_list_path);
        list << "www.example.com\n";
    }
    ListConfig ip_list;
    ip_list.file = list_path.string();
    ListConfig domain_list;
    domain_list.file = domain_list_path.string();
    config.lists = std::map<std::string, ListConfig>{
        {"resolved_ips", ip_list}, {"domains", domain_list}};
    RouteRule ip_rule;
    ip_rule.outbound = "vpn";
    ip_rule.list = std::vector<std::string>{"resolved_ips"};
    ip_rule.proto = "tcp";
    ip_rule.dest_port = "443";
    RouteRule domain_rule;
    domain_rule.outbound = "wan";
    domain_rule.list = std::vector<std::string>{"domains"};
    domain_rule.proto = "udp";
    domain_rule.dest_port = "80";
    RouteConfig route;
    route.rules = std::vector<RouteRule>{ip_rule, domain_rule};
    config.route = route;

    const auto result = compute_test_routing(config, cache, "www.example.com");

    CHECK(result.is_domain);
    CHECK(result.resolved_ips == std::vector<std::string>{"10.0.0.53", "2001:db8::53"});
    REQUIRE(result.entries.size() == 2);
    CHECK(result.entries[0].ip == "10.0.0.53");
    CHECK(result.entries[1].ip == "2001:db8::53");
    CHECK_FALSE(result.dns_error.has_value());
    REQUIRE(result.rule_diagnostics.size() == 2);
    const auto& ip_diagnostic = result.rule_diagnostics[0];
    CHECK_FALSE(ip_diagnostic.target_in_lists);
    REQUIRE(ip_diagnostic.ip_rows.size() == 2);
    CHECK(ip_diagnostic.ip_rows[0].in_lists);
    REQUIRE(ip_diagnostic.ip_rows[0].list_match.has_value());
    CHECK(ip_diagnostic.ip_rows[0].list_match->list_name == "resolved_ips");
    CHECK(ip_diagnostic.ip_rows[0].list_match->via == "10.0.0.53");
    CHECK_FALSE(ip_diagnostic.ip_rows[1].in_lists);
    CHECK_FALSE(ip_diagnostic.ip_rows[1].list_match.has_value());

    const auto& domain_diagnostic = result.rule_diagnostics[1];
    CHECK(domain_diagnostic.target_in_lists);
    REQUIRE(domain_diagnostic.ip_rows.size() == 2);
    for (const auto& ip_row : domain_diagnostic.ip_rows) {
        CHECK(ip_row.in_lists);
        REQUIRE(ip_row.list_match.has_value());
        CHECK(ip_row.list_match->list_name == "domains");
        CHECK(ip_row.list_match->via == "www.example.com");
    }

    TestRoutingCriteria tcp443;
    tcp443.proto = "tcp";
    tcp443.dest_port = 443;
    const auto tcp_result = compute_test_routing(
        config, cache, "www.example.com", tcp443);
    REQUIRE(tcp_result.entries.size() == 2);
    CHECK(tcp_result.entries[0].expected_outbound == "vpn");
    CHECK(tcp_result.entries[0].matched_rule_index == 0);
    CHECK(tcp_result.entries[1].expected_outbound == "(default)");

    TestRoutingCriteria udp80;
    udp80.proto = "udp";
    udp80.dest_port = 80;
    const auto udp_result = compute_test_routing(
        config, cache, "www.example.com", udp80);
    REQUIRE(udp_result.entries.size() == 2);
    CHECK(udp_result.entries[0].expected_outbound == "wan");
    CHECK(udp_result.entries[0].matched_rule_index == 1);
    CHECK(udp_result.entries[1].expected_outbound == "wan");

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("compute_test_routing reports no IPs when the system resolver cannot resolve the domain") {
    ScopedIpv6Capabilities ipv6_capabilities;
    const auto temp_dir = make_temp_dir();
    CacheManager cache(temp_dir);
    cache.ensure_dir();

    Config config = build_test_config();

    const auto result = compute_test_routing(config, cache, "example.invalid");

    CHECK(result.is_domain);
    CHECK(result.resolved_ips.empty());
    REQUIRE(result.entries.size() == 1);
    CHECK(result.entries.front().ip == "(no IPs resolved)");

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("compute_test_routing includes route rule conditions in diagnostics") {
    ScopedIpv6Capabilities ipv6_capabilities;
    const auto temp_dir = make_temp_dir();
    CacheManager cache(temp_dir);
    cache.ensure_dir();

    Config config = build_test_config();
    RouteRule rule;
    rule.outbound = "vpn";
    rule.list = std::vector<std::string>{"work", "media"};
    rule.proto = "tcp";
    rule.src_addr = "192.168.1.0/24";
    rule.dest_addr = "10.0.0.0/8";
    rule.src_port = "1024-65535";
    rule.dest_port = "443";

    RouteConfig route;
    route.rules = std::vector<RouteRule>{rule};
    config.route = route;

    const auto result = compute_test_routing(config, cache, "8.8.8.8");

    REQUIRE(result.rule_diagnostics.size() == 1);
    const auto& diagnostic_rule = result.rule_diagnostics.front().rule;
    CHECK(diagnostic_rule.outbound == "vpn");
    REQUIRE(diagnostic_rule.list.has_value());
    CHECK(*diagnostic_rule.list == std::vector<std::string>{"work", "media"});
    CHECK(diagnostic_rule.proto == "tcp");
    CHECK(diagnostic_rule.src_addr == "192.168.1.0/24");
    CHECK(diagnostic_rule.dest_addr == "10.0.0.0/8");
    CHECK(diagnostic_rule.src_port == "1024-65535");
    CHECK(diagnostic_rule.dest_port == "443");

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("compute_test_routing filters expected rules by packet criteria") {
    ScopedIpv6Capabilities ipv6_capabilities;
    const auto temp_dir = make_temp_dir();
    CacheManager cache(temp_dir / "cache");
    cache.ensure_dir();

    const auto list_path = temp_dir / "remote.txt";
    {
        std::ofstream list(list_path);
        list << "203.0.113.10/32\n";
    }

    Config config = build_test_config();
    ListConfig remote;
    remote.file = list_path.string();
    config.lists = std::map<std::string, ListConfig>{{"remote", remote}};

    RouteRule tcp_rule;
    tcp_rule.outbound = "vpn";
    tcp_rule.list = std::vector<std::string>{"remote"};
    tcp_rule.proto = "tcp";
    tcp_rule.dest_port = "443";
    RouteRule udp_rule;
    udp_rule.outbound = "wan";
    udp_rule.list = std::vector<std::string>{"remote"};
    udp_rule.proto = "udp";
    udp_rule.dest_port = "80";
    RouteConfig route;
    route.rules = std::vector<RouteRule>{tcp_rule, udp_rule};
    config.route = route;

    TestRoutingCriteria tcp443;
    tcp443.proto = "tcp";
    tcp443.dest_port = 443;
    const auto tcp_result = compute_test_routing(
        config, cache, "203.0.113.10", tcp443);
    REQUIRE(tcp_result.entries.size() == 1);
    CHECK(tcp_result.entries.front().expected_outbound == "vpn");
    CHECK(tcp_result.entries.front().matched_rule_index == 0);
    CHECK(tcp_result.entries.front().criteria_match == true);
    CHECK(tcp_result.rule_diagnostics[0].ip_rows[0].criteria_match == true);
    CHECK(tcp_result.rule_diagnostics[1].ip_rows[0].criteria_match == false);

    TestRoutingCriteria udp80;
    udp80.proto = "udp";
    udp80.dest_port = 80;
    const auto udp_result = compute_test_routing(
        config, cache, "203.0.113.10", udp80);
    CHECK(udp_result.entries.front().expected_outbound == "wan");
    CHECK(udp_result.entries.front().matched_rule_index == 1);

    TestRoutingCriteria tcp80;
    tcp80.proto = "tcp";
    tcp80.dest_port = 80;
    const auto no_match = compute_test_routing(
        config, cache, "203.0.113.10", tcp80);
    CHECK(no_match.entries.front().expected_outbound == "(default)");
    CHECK_FALSE(no_match.entries.front().matched_rule_index.has_value());
    CHECK(no_match.entries.front().criteria_match == false);

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("compute_test_routing evaluates source CIDR and negation") {
    ScopedIpv6Capabilities ipv6_capabilities;
    const auto temp_dir = make_temp_dir();
    CacheManager cache(temp_dir / "cache");
    cache.ensure_dir();

    const auto list_path = temp_dir / "remote.txt";
    {
        std::ofstream list(list_path);
        list << "203.0.113.10/32\n";
    }
    Config config = build_test_config();
    ListConfig remote;
    remote.file = list_path.string();
    config.lists = std::map<std::string, ListConfig>{{"remote", remote}};

    RouteRule local_rule;
    local_rule.outbound = "local";
    local_rule.list = std::vector<std::string>{"remote"};
    local_rule.proto = "tcp";
    local_rule.dest_port = "443";
    local_rule.dscp = 46;
    local_rule.src_addr = "192.168.1.0/24";
    RouteRule outside_rule = local_rule;
    outside_rule.outbound = "outside";
    outside_rule.src_addr = "!192.168.1.0/24";
    outside_rule.src_port = "!1000-2000";
    outside_rule.dest_port = "!80";
    RouteConfig route;
    route.rules = std::vector<RouteRule>{local_rule, outside_rule};
    config.route = route;

    TestRoutingCriteria local_packet;
    local_packet.proto = "tcp";
    local_packet.dest_port = 443;
    local_packet.src_port = 51514;
    local_packet.dscp = 46;
    local_packet.src_addr = "192.168.1.20";
    const auto local_result = compute_test_routing(
        config, cache, "203.0.113.10", local_packet);
    CHECK(local_result.entries.front().expected_outbound == "local");
    CHECK(local_result.entries.front().matched_rule_index == 0);

    local_packet.src_addr = "10.0.0.20";
    const auto outside_result = compute_test_routing(
        config, cache, "203.0.113.10", local_packet);
    CHECK(outside_result.entries.front().expected_outbound == "outside");
    CHECK(outside_result.entries.front().matched_rule_index == 1);

    local_packet.src_addr = "2001:db8::20";
    const auto opposite_family = compute_test_routing(
        config, cache, "203.0.113.10", local_packet);
    CHECK(opposite_family.entries.front().expected_outbound == "(unknown)");
    CHECK_FALSE(opposite_family.entries.front().criteria_match.has_value());
    CHECK_FALSE(opposite_family.warnings.empty());

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("compute_test_routing negated address list means none of the addresses") {
    // The firewall lowers the same rule to one rule on an ipset (iptables) or
    // `!= {a, b}` (nftables): a destination in either address does not match.
    ScopedIpv6Capabilities ipv6_capabilities;
    const auto temp_dir = make_temp_dir();
    CacheManager cache(temp_dir / "cache");
    cache.ensure_dir();

    Config config = build_test_config();
    RouteRule rule;
    rule.outbound = "local";
    rule.dest_addr = "!8.8.8.0/24,1.1.1.0/24";
    RouteConfig route;
    route.rules = std::vector<RouteRule>{rule};
    config.route = route;

    TestRoutingCriteria packet;
    packet.proto = "tcp";
    for (const char* inside : {"8.8.8.8", "1.1.1.1"}) {
        const auto result = compute_test_routing(config, cache, inside, packet);
        CHECK(result.entries.front().expected_outbound != "local");
    }
    const auto outside = compute_test_routing(config, cache, "9.9.9.9", packet);
    CHECK(outside.entries.front().expected_outbound == "local");

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("compute_test_routing treats an omitted DSCP as zero") {
    ScopedIpv6Capabilities ipv6_capabilities;
    const auto temp_dir = make_temp_dir();
    CacheManager cache(temp_dir / "cache");
    cache.ensure_dir();

    const auto list_path = temp_dir / "remote.txt";
    {
        std::ofstream list(list_path);
        list << "203.0.113.10/32\n";
    }
    Config config = build_test_config();
    ListConfig remote;
    remote.file = list_path.string();
    config.lists = std::map<std::string, ListConfig>{{"remote", remote}};
    RouteRule dscp_rule;
    dscp_rule.outbound = "wan";
    dscp_rule.dscp = 44;
    RouteRule list_rule;
    list_rule.outbound = "vpn";
    list_rule.list = std::vector<std::string>{"remote"};
    RouteConfig route;
    route.rules = std::vector<RouteRule>{dscp_rule, list_rule};
    config.route = route;

    TestRoutingCriteria packet;
    packet.proto = "tcp";
    packet.dest_port = 443;
    const auto unmarked = compute_test_routing(config, cache, "203.0.113.10", packet);
    CHECK(unmarked.entries.front().expected_outbound == "vpn");
    CHECK(unmarked.entries.front().matched_rule_index == 1);

    packet.dscp = 44;
    const auto marked = compute_test_routing(config, cache, "203.0.113.10", packet);
    CHECK(marked.entries.front().expected_outbound == "wan");
    CHECK(marked.entries.front().matched_rule_index == 0);

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("compute_test_routing does not evaluate IPv6 addresses when IPv6 is disabled") {
    ScopedIpv6Capabilities ipv6_capabilities;
    const auto temp_dir = make_temp_dir();
    CacheManager cache(temp_dir / "cache");
    cache.ensure_dir();

    ScopedDomainResolver resolver({"10.0.0.53", "2001:db8::53"});

    Config config = build_test_config();
    RouteRule rule;
    rule.outbound = "vpn";
    rule.proto = "tcp";
    RouteConfig route;
    route.rules = std::vector<RouteRule>{rule};
    config.route = route;

    TestRoutingCriteria criteria;
    criteria.proto = "tcp";
    criteria.dest_port = 443;
    const auto result = compute_test_routing(
        config, cache, "www.example.com", criteria, nullptr, {}, nullptr, false);

    CHECK_FALSE(result.ipv6_enabled);
    CHECK(result.resolved_ips == std::vector<std::string>{"10.0.0.53", "2001:db8::53"});
    REQUIRE(result.entries.size() == 1);
    CHECK(result.entries.front().ip == "10.0.0.53");
    REQUIRE(result.rule_diagnostics.size() == 1);
    REQUIRE(result.rule_diagnostics.front().ip_rows.size() == 1);
    CHECK(result.rule_diagnostics.front().ip_rows.front().ip == "10.0.0.53");
    // The "could not be evaluated" warning may only come from entries that
    // were evaluated, never from the skipped IPv6 address.
    const bool any_unknown = std::any_of(
        result.entries.begin(), result.entries.end(), [](const TestRoutingEntry& e) {
            return e.expected_outbound == "(unknown)" || e.actual_outbound == "(unknown)";
        });
    const bool has_warning = std::any_of(
        result.warnings.begin(), result.warnings.end(), [](const std::string& w) {
            return w.find("could not be evaluated") != std::string::npos;
        });
    CHECK(has_warning == any_unknown);

    const auto literal = compute_test_routing(
        config, cache, "2001:db8::1", criteria, nullptr, {}, nullptr, false);
    CHECK_FALSE(literal.ipv6_enabled);
    CHECK(literal.entries.empty());
    CHECK(literal.rule_diagnostics.front().ip_rows.empty());

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("compute_test_routing reports unknown when packet criteria are incomplete") {
    ScopedIpv6Capabilities ipv6_capabilities;
    const auto temp_dir = make_temp_dir();
    CacheManager cache(temp_dir / "cache");
    cache.ensure_dir();

    const auto list_path = temp_dir / "remote.txt";
    {
        std::ofstream list(list_path);
        list << "203.0.113.10/32\n";
    }
    Config config = build_test_config();
    ListConfig remote;
    remote.file = list_path.string();
    config.lists = std::map<std::string, ListConfig>{{"remote", remote}};
    RouteRule source_rule;
    source_rule.outbound = "vpn";
    source_rule.list = std::vector<std::string>{"remote"};
    source_rule.proto = "tcp";
    source_rule.dest_port = "443";
    source_rule.src_addr = "192.168.1.0/24";
    RouteConfig route;
    route.rules = std::vector<RouteRule>{source_rule};
    config.route = route;

    TestRoutingCriteria incomplete;
    incomplete.proto = "tcp";
    incomplete.dest_port = 443;
    const auto result = compute_test_routing(
        config, cache, "203.0.113.10", incomplete);
    REQUIRE(result.entries.size() == 1);
    CHECK(result.entries.front().expected_outbound == "(unknown)");
    CHECK_FALSE(result.entries.front().matched_rule_index.has_value());
    CHECK_FALSE(result.entries.front().criteria_match.has_value());
    REQUIRE(result.rule_diagnostics.front().ip_rows.size() == 1);
    CHECK_FALSE(result.rule_diagnostics.front().ip_rows.front().criteria_match.has_value());

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("compute_test_routing names the rule and fields that block a decision") {
    ScopedIpv6Capabilities ipv6_capabilities;
    const auto temp_dir = make_temp_dir();
    CacheManager cache(temp_dir / "cache");
    cache.ensure_dir();

    ScopedDomainResolver resolver({"10.0.0.53"});
    Config config = build_test_config();
    const auto domain_list_path = temp_dir / "ya-domains.txt";
    {
        std::ofstream list(domain_list_path);
        list << "ya.ru\n";
    }
    ListConfig domains;
    domains.file = domain_list_path.string();
    config.lists = std::map<std::string, ListConfig>{{"ya", domains}};
    RouteRule source_rule;
    source_rule.outbound = "direct";
    source_rule.src_addr = "1.2.3.4";
    RouteRule list_rule;
    list_rule.outbound = "vpn";
    list_rule.list = std::vector<std::string>{"ya"};
    RouteConfig route;
    route.rules = std::vector<RouteRule>{source_rule, list_rule};
    config.route = route;

    TestRoutingCriteria packet;
    packet.proto = "tcp";
    packet.dest_port = 443;
    const auto blocked = compute_test_routing(config, cache, "ya.ru", packet);
    REQUIRE_FALSE(blocked.entries.empty());
    const auto& entry = blocked.entries.front();
    CHECK(entry.expected_outbound == "(unknown)");
    CHECK(entry.undetermined_rule_index == 0);
    CHECK(entry.missing_criteria == std::vector<std::string>{"src_addr"});
    CHECK_FALSE(entry.default_gateway_directness);

    packet.src_addr = "10.0.0.1";
    const auto resolved = compute_test_routing(config, cache, "ya.ru", packet);
    REQUIRE_FALSE(resolved.entries.empty());
    CHECK(resolved.entries.front().expected_outbound == "vpn");
    CHECK(resolved.entries.front().matched_rule_index == 1);
    CHECK_FALSE(resolved.entries.front().undetermined_rule_index.has_value());
    CHECK(resolved.entries.front().missing_criteria.empty());

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("compute_test_routing keeps no-list and default-gateway actual state honest") {
    ScopedIpv6Capabilities ipv6_capabilities;
    const auto temp_dir = make_temp_dir();
    const auto empty_bin = temp_dir / "empty-bin";
    std::filesystem::create_directories(empty_bin);
    ScopedPathOverride path_override(empty_bin.string());

    CacheManager cache(temp_dir / "cache");
    cache.ensure_dir();
    Config config = build_test_config();
    DaemonConfig daemon;
    daemon.firewall_backend = api::DaemonConfigFirewallBackend::IPTABLES;
    config.daemon = daemon;
    Outbound outbound;
    outbound.tag = "vpn";
    outbound.type = OutboundType::TABLE;
    outbound.table = 100;
    config.outbounds = std::vector<Outbound>{outbound};
    RouteRule rule;
    rule.outbound = "vpn";
    rule.proto = "tcp";
    rule.dest_port = "443";
    rule.dest_addr = "203.0.113.0/24";
    RouteConfig route;
    route.rules = std::vector<RouteRule>{rule};
    config.route = route;

    TestRoutingCriteria packet;
    packet.proto = "tcp";
    packet.dest_port = 443;
    const auto no_realized_state = compute_test_routing(
        config, cache, "203.0.113.10", packet);
    CHECK(no_realized_state.entries.front().expected_outbound == "vpn");
    CHECK(no_realized_state.entries.front().actual_outbound == "(unknown)");
    CHECK_FALSE(no_realized_state.entries.front().ok);

    RuleState realized_rule;
    realized_rule.rule_index = 0;
    realized_rule.outbound_tag = "vpn";
    realized_rule.action_type = RuleActionType::Mark;
    const std::vector<RuleState> realized_rules{realized_rule};
    const auto realized_state = compute_test_routing(
        config, cache, "203.0.113.10", packet, &realized_rules);
    CHECK(realized_state.entries.front().actual_outbound == "vpn");
    CHECK(realized_state.entries.front().ok);

    RouteRule gateway_rule;
    gateway_rule.outbound = "vpn";
    gateway_rule.default_gateway = api::DefaultGateway::IPV4;
    route.rules = std::vector<RouteRule>{gateway_rule};
    config.route = route;
    const auto ipv6_result = compute_test_routing(
        config, cache, "2001:db8::10", packet);
    CHECK(ipv6_result.entries.front().expected_outbound == "(default)");
    CHECK(ipv6_result.entries.front().criteria_match == false);

    const auto ipv4_result = compute_test_routing(
        config, cache, "203.0.113.10", packet);
    CHECK(ipv4_result.entries.front().expected_outbound == "(unknown)");
    CHECK_FALSE(ipv4_result.entries.front().criteria_match.has_value());

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("compute_test_routing uses realized iptables set names") {
    ScopedIpv6Capabilities ipv6_capabilities;
    const auto temp_dir = make_temp_dir();
    const auto bin_dir = temp_dir / "bin";
    const auto invocation_log = temp_dir / "ipset-invocations.txt";
    std::filesystem::create_directories(bin_dir);

    write_executable(bin_dir / "iptables", "#!/bin/sh\nexit 0\n");
    write_executable(
        bin_dir / "ipset",
        "#!/bin/sh\n"
        "echo test >> " + invocation_log.string() + "\n"
        "if [ \"$1\" = test ] && [ \"$2\" = kpbr4_remote ] && "
        "[ \"$3\" = 203.0.113.10 ]; then\n"
        "  exit 0\n"
        "fi\n"
        "exit 1\n");
    ScopedPathOverride path_override(bin_dir.string() + ":/usr/bin:/bin");

    const auto list_path = temp_dir / "remote.txt";
    {
        std::ofstream list(list_path);
        list << "203.0.113.10/32\n";
    }

    CacheManager cache(temp_dir / "cache");
    cache.ensure_dir();

    Config config = build_test_config();
    ListConfig list;
    list.file = list_path.string();
    config.lists = std::map<std::string, ListConfig>{{"remote", list}};

    DaemonConfig daemon;
    daemon.firewall_backend = api::DaemonConfigFirewallBackend::IPTABLES;
    config.daemon = daemon;

    Outbound outbound;
    outbound.tag = "vpn";
    outbound.type = OutboundType::TABLE;
    outbound.table = 100;
    config.outbounds = std::vector<Outbound>{outbound};

    RouteRule rule;
    rule.outbound = "vpn";
    rule.list = std::vector<std::string>{"remote"};
    RouteConfig route;
    route.rules = std::vector<RouteRule>{rule};
    config.route = route;

    RuleState realized;
    realized.rule_index = 0;
    realized.list_names = {"remote"};
    realized.set_names = {"kpbr4_remote", "kpbr4d_remote"};
    realized.outbound_tag = "vpn";
    realized.action_type = RuleActionType::Mark;
    const std::vector<RuleState> realized_rules{realized};

    const auto result = compute_test_routing(
        config, cache, "203.0.113.10", TestRoutingCriteria{}, &realized_rules,
        [](const std::string& set_name, const std::string& ip) {
            CHECK(set_name == "kpbr4d_remote");
            CHECK(ip == "203.0.113.10");
            return SetWriteEvidence{
                SetWriteEvidenceStatus::Recorded, std::optional<uint64_t>(7)};
        });

    REQUIRE(result.entries.size() == 1);
    CHECK(result.entries.front().expected_outbound == "vpn");
    CHECK(result.entries.front().actual_outbound == "vpn");
    CHECK(result.entries.front().ok);
    REQUIRE(result.rule_diagnostics.size() == 1);
    REQUIRE(result.rule_diagnostics.front().ip_rows.size() == 1);
    CHECK(result.rule_diagnostics.front().ip_rows.front().in_lists);
    REQUIRE(result.rule_diagnostics.front().ip_rows.front().list_match.has_value());
    CHECK(result.rule_diagnostics.front().ip_rows.front().list_match->list_name == "remote");
    REQUIRE(result.rule_diagnostics.front().ip_rows.front().in_ipset.has_value());
    CHECK(*result.rule_diagnostics.front().ip_rows.front().in_ipset);
    REQUIRE(result.rule_diagnostics.front().ip_rows.front().set_write_evidence.has_value());
    CHECK(result.rule_diagnostics.front().ip_rows.front().set_write_evidence->status ==
          SetWriteEvidenceStatus::Recorded);
    CHECK(result.rule_diagnostics.front().ip_rows.front().set_write_evidence->age_seconds == 7);

    std::ifstream invocations(invocation_log);
    const std::string invocation_contents{
        std::istreambuf_iterator<char>(invocations),
        std::istreambuf_iterator<char>()};
    CHECK(invocation_contents == "test\n");

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("compute_test_routing keeps unknown set membership from proving absence") {
    const auto temp_dir = make_temp_dir();
    const auto bin_dir = temp_dir / "bin";
    std::filesystem::create_directories(bin_dir);
    write_executable(
        bin_dir / "ipset",
        "#!/bin/sh\n"
        "case \"$2\" in\n"
        "  kpbr4_true|kpbr6_true) exit 0 ;;\n"
        "  kpbr4_unknown|kpbr6_unknown) exit 127 ;;\n"
        "  *) exit 1 ;;\n"
        "esac\n");
    ScopedPathOverride path_override(bin_dir.string());
    ScopedIptablesTools iptables_tools;

    CacheManager cache(temp_dir / "cache");
    cache.ensure_dir();
    Config config = build_test_config();
    DaemonConfig daemon;
    daemon.firewall_backend = api::DaemonConfigFirewallBackend::IPTABLES;
    config.daemon = daemon;

    std::vector<RouteRule> route_rules(8);
    for (auto& rule : route_rules) rule.outbound = "vpn";
    RouteConfig route;
    route.rules = std::move(route_rules);
    config.route = route;

    const std::vector<std::vector<std::string>> names{
        {"kpbr4_false", "kpbr4_unknown"},
        {"kpbr4_true", "kpbr4_unknown"},
        {"kpbr4_false", "kpbr4_false2"},
        {"kpbr4_false", "kpbr6_unknown"},
        {"kpbr6_false", "kpbr6_unknown"},
        {"kpbr6_true", "kpbr6_unknown"},
        {"kpbr6_false", "kpbr6_false2"},
        {"kpbr6_false", "kpbr4_unknown"},
    };
    std::vector<RuleState> states;
    states.reserve(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        RuleState state;
        state.rule_index = i;
        state.set_names = names[i];
        state.outbound_tag = "vpn";
        state.action_type = RuleActionType::Mark;
        states.push_back(std::move(state));
    }

    const auto v4 = compute_test_routing(
        config, cache, "203.0.113.10", TestRoutingCriteria{}, &states, {}, nullptr, true);
    REQUIRE(v4.rule_diagnostics.size() == names.size());
    CHECK_FALSE(v4.rule_diagnostics[0].ip_rows[0].in_ipset.has_value());
    CHECK(v4.rule_diagnostics[1].ip_rows[0].in_ipset == std::optional<bool>(true));
    CHECK(v4.rule_diagnostics[2].ip_rows[0].in_ipset == std::optional<bool>(false));
    // The IPv6 unknown set is not applicable to an IPv4 address.
    CHECK(v4.rule_diagnostics[3].ip_rows[0].in_ipset == std::optional<bool>(false));

    const auto v6 = compute_test_routing(
        config, cache, "2001:db8::10", TestRoutingCriteria{}, &states, {}, nullptr, true);
    REQUIRE(v6.rule_diagnostics.size() == names.size());
    CHECK_FALSE(v6.rule_diagnostics[4].ip_rows[0].in_ipset.has_value());
    CHECK(v6.rule_diagnostics[5].ip_rows[0].in_ipset == std::optional<bool>(true));
    CHECK(v6.rule_diagnostics[6].ip_rows[0].in_ipset == std::optional<bool>(false));
    // The IPv4 unknown set is not applicable to an IPv6 address.
    CHECK(v6.rule_diagnostics[7].ip_rows[0].in_ipset == std::optional<bool>(false));

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("daemon test-routing response is rendered as a human-readable table") {
    const nlohmann::json response = {
        {"ok", true},
        {"result",
         {{"target", "example.com"},
          {"is_domain", true},
          {"no_matching_rule", false},
          {"ipv6_enabled", true},
          {"resolved_ips", {"203.0.113.10"}},
          {"warnings", nlohmann::json::array()},
          {"dns_error", nullptr},
          {"rule_diagnostics", nlohmann::json::array()},
          {"results",
           {{{"ip", "203.0.113.10"},
             {"expected_outbound", "vpn"},
             {"actual_outbound", "vpn"},
             {"ok", true},
             {"diagnosis", "ok"},
             {"list_match", {{"list", "domains"}, {"via", "example.com"}}}}}}}}};

    std::ostringstream stdout_capture;
    std::ostringstream stderr_capture;
    auto* previous_stdout = std::cout.rdbuf(stdout_capture.rdbuf());
    auto* previous_stderr = std::cerr.rdbuf(stderr_capture.rdbuf());
    TestRoutingCriteria criteria;
    criteria.proto = "tcp";
    criteria.dest_port = 443;
    const int exit_code = run_test_routing_command(response, criteria);
    std::cout.rdbuf(previous_stdout);
    std::cerr.rdbuf(previous_stderr);

    CHECK(exit_code == 0);
    CHECK(stderr_capture.str().empty());
    CHECK(stdout_capture.str().find("Target: example.com") != std::string::npos);
    CHECK(stdout_capture.str().find("Packet: proto=tcp dest_port=443") != std::string::npos);
    CHECK(stdout_capture.str().find("Expected Outbound") != std::string::npos);
    CHECK(stdout_capture.str().find("domains (via example.com)") != std::string::npos);
    CHECK(stdout_capture.str().find("{\"") == std::string::npos);
}

TEST_CASE("compute_test_routing matches domains through DomainIndex semantics") {
    ScopedIpv6Capabilities ipv6_capabilities;
    const auto temp_dir = make_temp_dir();
    CacheManager cache(temp_dir);
    cache.ensure_dir();
    ScopedDomainResolver resolver({"10.0.0.53"});

    Config config = build_test_config();

    ListConfig domains;
    domains.domains = std::vector<std::string>{"*.Example.COM", "other.test"};
    ListConfig no_match;
    no_match.domains = std::vector<std::string>{"badexample.com", "sub.example.com.evil"};
    config.lists = std::map<std::string, ListConfig>{{"domains", domains}, {"no_match", no_match}};
    RouteRule rule;
    rule.outbound = "vpn";
    rule.list = std::vector<std::string>{"no_match", "domains"};
    RouteConfig route;
    route.rules = std::vector<RouteRule>{rule};
    config.route = route;

    // Case-insensitive, wildcard prefix stripped, label-boundary suffix match.
    const auto result = compute_test_routing(config, cache, "A.B.Example.com");
    REQUIRE(result.rule_diagnostics.size() == 1);
    CHECK(result.rule_diagnostics[0].target_in_lists);
    REQUIRE(result.rule_diagnostics[0].target_match.has_value());
    CHECK(result.rule_diagnostics[0].target_match->list_name == "domains");
    CHECK(result.rule_diagnostics[0].target_match->via == "example.com");

    const auto unrelated = compute_test_routing(config, cache, "notexample.com");
    REQUIRE(unrelated.rule_diagnostics.size() == 1);
    CHECK_FALSE(unrelated.rule_diagnostics[0].target_in_lists);

    std::filesystem::remove_all(temp_dir);
}

namespace {

RoutingDnsResolver dnsmasq_resolver_at(std::uint16_t port) {
    RoutingDnsResolver r;
    r.source = RoutingDnsSource::Dnsmasq;
    r.address = "127.0.0.1";
    r.port = port;
    r.instance = "uci:test";
    return r;
}

std::uint16_t qtype_of(const fake_dns::Bytes& query) {
    std::size_t pos = 12;
    while (pos < query.size() && query[pos] != 0) pos += 1u + query[pos];
    pos += 1;
    return static_cast<std::uint16_t>((query[pos] << 8) | query[pos + 1]);
}

TestRoutingResult routing_with_resolver(const RoutingDnsResolver& resolver) {
    const auto temp_dir = make_temp_dir();
    CacheManager cache(temp_dir);
    cache.ensure_dir();
    const Config config = build_test_config();
    auto result = compute_test_routing(config, cache, "example.test", TestRoutingCriteria{},
                                       nullptr, {}, &resolver);
    std::filesystem::remove_all(temp_dir);
    return result;
}

} // namespace

TEST_CASE("compute_test_routing resolves A and AAAA through dnsmasq") {
    ScopedIpv6Capabilities ipv6_capabilities;
    FakeDnsServer srv([](const fake_dns::Bytes& q, bool) {
        return qtype_of(q) == 28
            ? fake_dns::make_reply(q, 0, {fake_dns::aaaa_rr("2001:db8::1")})
            : fake_dns::make_reply(q, 0, {fake_dns::a_rr("192.0.2.10")});
    });
    const auto result = routing_with_resolver(dnsmasq_resolver_at(srv.port()));
    REQUIRE(result.resolved_ips.size() == 2);
    CHECK(result.resolved_ips[0] == "192.0.2.10");
    CHECK(result.resolved_ips[1] == "2001:db8::1");
    REQUIRE(result.dns.has_value());
    CHECK(result.dns->resolver == "dnsmasq");
    CHECK(result.dns->endpoint == "127.0.0.1:" + std::to_string(srv.port()));
    CHECK(result.dns->instance == "uci:test");
    CHECK_FALSE(result.dns_error.has_value());
}

TEST_CASE("compute_test_routing tolerates AAAA NODATA from dnsmasq") {
    ScopedIpv6Capabilities ipv6_capabilities;
    FakeDnsServer srv([](const fake_dns::Bytes& q, bool) {
        return qtype_of(q) == 28
            ? fake_dns::make_reply(q, 0)
            : fake_dns::make_reply(q, 0, {fake_dns::a_rr("192.0.2.10")});
    });
    const auto result = routing_with_resolver(dnsmasq_resolver_at(srv.port()));
    CHECK(result.resolved_ips.size() == 1);
    CHECK_FALSE(result.dns_error.has_value());
    CHECK(result.warnings.empty());
}

TEST_CASE("compute_test_routing reports NXDOMAIN from dnsmasq") {
    ScopedIpv6Capabilities ipv6_capabilities;
    FakeDnsServer srv([](const fake_dns::Bytes& q, bool) {
        return fake_dns::make_reply(q, 3);
    });
    const auto result = routing_with_resolver(dnsmasq_resolver_at(srv.port()));
    CHECK(result.resolved_ips.empty());
    REQUIRE(result.dns_error.has_value());
    CHECK(result.dns_error->find("nxdomain") != std::string::npos);
}

TEST_CASE("compute_test_routing does not query when dnsmasq is unavailable") {
    ScopedIpv6Capabilities ipv6_capabilities;
    std::atomic<int> queries{0};
    FakeDnsServer srv([&queries](const fake_dns::Bytes& q, bool) {
        ++queries;
        return fake_dns::make_reply(q, 0, {fake_dns::a_rr("192.0.2.10")});
    });
    auto resolver = dnsmasq_resolver_at(srv.port());
    resolver.unavailable = "no endpoint";
    const auto result = routing_with_resolver(resolver);
    CHECK(result.resolved_ips.empty());
    REQUIRE(result.dns_error.has_value());
    CHECK(result.dns_error->rfind("DNS resolution skipped:", 0) == 0);
    CHECK(queries.load() == 0);
}

TEST_CASE("default_routing_dns_resolver follows the integration setting") {
    ScopedIpv6Capabilities ipv6_capabilities;
    Config off = build_test_config();
    off.dns->resolver_integration = ResolverIntegrationMode::NONE;
    CHECK(default_routing_dns_resolver(off).source == RoutingDnsSource::System);

    Config on = build_test_config();
    on.dns->resolver_integration = ResolverIntegrationMode::DNSMASQ;
    const auto r = default_routing_dns_resolver(on);
    CHECK(r.source == RoutingDnsSource::Dnsmasq);
    CHECK(r.unavailable.has_value());
}

TEST_CASE("compute_test_routing leaves dns unset for IP targets") {
    ScopedIpv6Capabilities ipv6_capabilities;
    const auto temp_dir = make_temp_dir();
    CacheManager cache(temp_dir);
    cache.ensure_dir();
    const auto result = compute_test_routing(build_test_config(), cache, "192.0.2.1");
    CHECK_FALSE(result.dns.has_value());
    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("parse_test_routing_request accepts packet fields and rejects bad ones") {
    const auto request = parse_test_routing_request(
        {{"target", "203.0.113.10"}, {"proto", "udp"}, {"dest_port", 53},
         {"src_addr", "192.168.1.10"}, {"src_port", 5353}, {"dscp", 44}});
    CHECK(request.target == "203.0.113.10");
    CHECK(request.criteria.proto == std::optional<std::string>("udp"));
    CHECK(request.criteria.dest_port == std::optional<uint16_t>(53));
    CHECK(request.criteria.src_addr == std::optional<std::string>("192.168.1.10"));
    CHECK(request.criteria.src_port == std::optional<uint16_t>(5353));
    CHECK(request.criteria.dscp == std::optional<uint8_t>(44));

    CHECK_FALSE(parse_test_routing_request({{"target", "example.com"}}).criteria.has_any());
    CHECK_THROWS_AS(parse_test_routing_request({{"target", ""}}), TestRoutingRequestError);
    CHECK_THROWS_AS(parse_test_routing_request({{"target", "x"}, {"proto", "icmp"}}),
                    TestRoutingRequestError);
    CHECK_THROWS_AS(parse_test_routing_request({{"target", "x"}, {"dscp", 64}}),
                    TestRoutingRequestError);
    CHECK_THROWS_AS(parse_test_routing_request({{"target", "x"}, {"dest_port", 0}}),
                    TestRoutingRequestError);
    CHECK_THROWS_AS(parse_test_routing_request(
                        {{"target", "203.0.113.10"}, {"src_addr", "2001:db8::1"}}),
                    TestRoutingRequestError);
    CHECK_THROWS_AS(parse_test_routing_request(nlohmann::json::array()),
                    TestRoutingRequestError);
}

TEST_CASE("test-routing result survives the API JSON round trip") {
    TestRoutingResult result;
    result.target = "example.com";
    result.is_domain = true;
    result.resolved_ips = {"203.0.113.10"};
    result.warnings = {"w"};
    result.dns = TestRoutingDnsInfo{"dnsmasq", "127.0.0.1:53", "uci:x", "ok", "nodata", {}};
    TestRoutingEntry entry;
    entry.ip = "203.0.113.10";
    entry.expected_outbound = "vpn";
    entry.actual_outbound = "vpn";
    entry.ok = true;
    entry.matched_rule_index = 3;
    entry.criteria_match = true;
    entry.list_match = ListMatchInfo{"domains", "example.com"};
    result.entries.push_back(entry);

    const auto json = test_routing_result_to_json(result);
    CHECK(json.at("results").at(0).at("list_match").at("list") == "domains");

    const auto back = test_routing_result_from_json(json);
    CHECK(back.target == "example.com");
    CHECK(back.is_domain);
    CHECK(back.warnings == std::vector<std::string>{"w"});
    REQUIRE(back.dns.has_value());
    CHECK(back.dns->resolver == "dnsmasq");
    CHECK(back.dns->aaaa_status == std::optional<std::string>("nodata"));
    REQUIRE(back.entries.size() == 1);
    CHECK(back.entries[0].expected_outbound == "vpn");
    CHECK(back.entries[0].matched_rule_index == std::optional<int>(3));
    REQUIRE(back.entries[0].list_match.has_value());
    CHECK(back.entries[0].list_match->via == "example.com");
}

namespace {

Config default_gateway_config(const std::string& outbound_tag = "vpn") {
    Config config = build_test_config();
    Outbound outbound;
    outbound.tag = outbound_tag;
    outbound.type = OutboundType::TABLE;
    outbound.table = 100;
    config.outbounds = std::vector<Outbound>{outbound};
    RouteRule rule;
    rule.outbound = outbound_tag;
    rule.default_gateway = api::DefaultGateway::IPV4;
    RouteConfig route;
    route.rules = std::vector<RouteRule>{rule};
    config.route = route;
    return config;
}

TestRoutingNetworkSnapshot lan_snapshot() {
    TestRoutingNetworkSnapshot network;
    DumpedRoute vpn_route;
    vpn_route.destination = "10.8.0.0/24";
    vpn_route.interface = "wg0";
    vpn_route.family = AF_INET;
    DumpedRoute default_route;
    default_route.destination = "default";
    default_route.family = AF_INET;
    network.main_routes = {vpn_route, default_route};
    DumpedInterface lan;
    lan.name = "br0";
    lan.ipv4_addresses = {"192.168.1.1/24"};
    network.interfaces = {lan};
    return network;
}

RuleState realized_gateway_rule(const std::string& outbound_tag,
                                std::vector<std::string> bypass) {
    RuleState state;
    state.rule_index = 0;
    state.outbound_tag = outbound_tag;
    state.action_type = RuleActionType::Mark;
    state.criteria = build_firewall_rule_criteria(RouteRule{});
    state.criteria.default_gateway = DefaultGatewayFamily::Ipv4;
    state.criteria.default_gateway_bypass = std::move(bypass);
    return state;
}

TestRoutingResult run_gateway(const Config& config,
                              const std::string& target,
                              const TestRoutingNetworkSnapshot* network,
                              const std::vector<RuleState>* realized = nullptr) {
    static std::filesystem::path dir = make_temp_dir();
    CacheManager cache(dir);
    cache.ensure_dir();
    TestRoutingCriteria packet;
    packet.proto = "tcp";
    return compute_test_routing(config, cache, target, packet, realized, {}, nullptr,
                                true, network, nullptr);
}

} // namespace

TEST_CASE("compute_test_routing: public address uses a default_gateway rule") {
    const Config config = default_gateway_config();
    const auto network = lan_snapshot();
    const std::vector<RuleState> realized{realized_gateway_rule(
        "vpn", {"10.8.0.0/24", "127.0.0.0/8", "192.168.1.0/24"})};

    const auto result = run_gateway(config, "1.2.3.4", &network, &realized);
    const auto& entry = result.entries.front();
    CHECK(entry.expected_outbound == "vpn");
    CHECK(entry.actual_outbound == "vpn");
    CHECK(entry.ok);
    CHECK_FALSE(entry.default_gateway_directness);
    REQUIRE(entry.default_gateway.has_value());
    CHECK(entry.default_gateway->rule_index == 0);
    CHECK(entry.default_gateway->uses_default_gateway == std::optional<bool>(true));
    CHECK_FALSE(entry.default_gateway->bypass.has_value());
    CHECK(entry.default_gateway->applied_uses_default_gateway == std::optional<bool>(true));
}

TEST_CASE("compute_test_routing: address with its own route skips a default_gateway rule") {
    const Config config = default_gateway_config();
    const auto network = lan_snapshot();
    const std::vector<RuleState> realized{realized_gateway_rule(
        "vpn", {"10.8.0.0/24", "192.168.1.0/24"})};

    const auto route = run_gateway(config, "10.8.0.5", &network, &realized);
    const auto& route_entry = route.entries.front();
    CHECK(route_entry.expected_outbound == "(default)");
    CHECK(route_entry.actual_outbound == "(default)");
    REQUIRE(route_entry.default_gateway.has_value());
    CHECK(route_entry.default_gateway->uses_default_gateway == std::optional<bool>(false));
    REQUIRE(route_entry.default_gateway->bypass.has_value());
    CHECK(route_entry.default_gateway->bypass->cidr == "10.8.0.0/24");
    CHECK(route_entry.default_gateway->bypass->source == DefaultGatewayBypassSource::Route);
    CHECK(route_entry.default_gateway->bypass->interface == std::optional<std::string>("wg0"));

    const auto address = run_gateway(config, "192.168.1.50", &network, &realized);
    REQUIRE(address.entries.front().default_gateway.has_value());
    const auto& bypass = address.entries.front().default_gateway->bypass;
    REQUIRE(bypass.has_value());
    CHECK(bypass->cidr == "192.168.1.0/24");
    CHECK(bypass->source == DefaultGatewayBypassSource::Address);
    CHECK(bypass->interface == std::optional<std::string>("br0"));

    const auto reserved = run_gateway(config, "169.254.1.1", &network, &realized);
    REQUIRE(reserved.entries.front().default_gateway.has_value());
    const auto& reserved_bypass = reserved.entries.front().default_gateway->bypass;
    REQUIRE(reserved_bypass.has_value());
    CHECK(reserved_bypass->cidr == "169.254.0.0/16");
    CHECK(reserved_bypass->source == DefaultGatewayBypassSource::Reserved);
}

TEST_CASE("compute_test_routing: default_gateway rule stays unknown without a network snapshot") {
    const Config config = default_gateway_config();
    const auto result = run_gateway(config, "1.2.3.4", nullptr);
    const auto& entry = result.entries.front();
    CHECK(entry.expected_outbound == "(unknown)");
    CHECK(entry.default_gateway_directness);
    CHECK_FALSE(entry.default_gateway.has_value());
}

TEST_CASE("compute_test_routing: applied bypass list that differs from the routes is reported") {
    const Config config = default_gateway_config();
    const auto network = lan_snapshot();
    // The firewall was applied before the VPN route appeared.
    const std::vector<RuleState> stale{realized_gateway_rule("vpn", {"192.168.1.0/24"})};

    const auto result = run_gateway(config, "10.8.0.5", &network, &stale);
    const auto& entry = result.entries.front();
    CHECK(entry.expected_outbound == "(default)");
    CHECK(entry.actual_outbound == "vpn");
    CHECK_FALSE(entry.ok);
    REQUIRE(entry.default_gateway.has_value());
    CHECK(entry.default_gateway->uses_default_gateway == std::optional<bool>(false));
    CHECK(entry.default_gateway->applied_uses_default_gateway == std::optional<bool>(true));
}

namespace {

TestRoutingResult run_target_only(const Config& config,
                                  const std::string& target,
                                  const TestRoutingNetworkSnapshot* network,
                                  const std::vector<RuleState>* realized = nullptr) {
    static std::filesystem::path dir = make_temp_dir();
    CacheManager cache(dir);
    cache.ensure_dir();
    return compute_test_routing(config, cache, target, TestRoutingCriteria{}, realized, {},
                                nullptr, true, network, nullptr);
}

} // namespace

TEST_CASE("compute_test_routing: target-only request evaluates a default_gateway rule") {
    const Config config = default_gateway_config();
    const auto network = lan_snapshot();
    const std::vector<RuleState> realized{realized_gateway_rule(
        "vpn", {"10.8.0.0/24", "127.0.0.0/8", "192.168.1.0/24"})};

    const auto result = run_target_only(config, "1.2.3.4", &network, &realized);
    const auto& entry = result.entries.front();
    CHECK(entry.expected_outbound == "vpn");
    CHECK(entry.actual_outbound == "vpn");
    CHECK(entry.ok);
    CHECK_FALSE(entry.criteria_match.has_value());
    REQUIRE(entry.default_gateway.has_value());
    CHECK(entry.default_gateway->uses_default_gateway == std::optional<bool>(true));
    CHECK(entry.default_gateway->applied_uses_default_gateway == std::optional<bool>(true));

    // An address with its own route is not carried by the default gateway.
    const auto lan = run_target_only(config, "192.168.1.50", &network, &realized);
    CHECK(lan.entries.front().expected_outbound == "(default)");
    CHECK(lan.entries.front().actual_outbound == "(default)");
    CHECK(lan.entries.front().ok);
    REQUIRE(lan.entries.front().default_gateway.has_value());
    CHECK(lan.entries.front().default_gateway->uses_default_gateway == std::optional<bool>(false));
}

TEST_CASE("compute_test_routing: target-only default_gateway rule is ignored without routes") {
    const Config config = default_gateway_config();
    const auto result = run_target_only(config, "1.2.3.4", nullptr);
    const auto& entry = result.entries.front();
    // Without the router's routes the rule cannot be decided; as before, a
    // target-only request does not turn into an unknown because of it.
    CHECK(entry.expected_outbound == "(default)");
    CHECK_FALSE(entry.default_gateway.has_value());
    CHECK_FALSE(entry.default_gateway_directness);
}

TEST_CASE("compute_test_routing: target-only request evaluates a dest_addr-only rule") {
    Config config = default_gateway_config();
    auto& rule = config.route->rules->front();
    rule.default_gateway.reset();
    rule.dest_addr = "1.2.3.0/24";
    RuleState state = realized_gateway_rule("vpn", {});
    state.criteria = build_firewall_rule_criteria(rule);

    const std::vector<RuleState> realized{state};
    const auto hit = run_target_only(config, "1.2.3.4", nullptr, &realized);
    CHECK(hit.entries.front().expected_outbound == "vpn");
    CHECK(hit.entries.front().actual_outbound == "vpn");
    CHECK(hit.entries.front().ok);

    const auto miss = run_target_only(config, "5.6.7.8", nullptr, &realized);
    CHECK(miss.entries.front().expected_outbound == "(default)");
    CHECK(miss.entries.front().actual_outbound == "(default)");
}

TEST_CASE("compute_test_routing: target-only request still skips list-less rules with ports") {
    Config config = default_gateway_config();
    config.route->rules->front().dest_port = "443";
    const auto network = lan_snapshot();
    RuleState state = realized_gateway_rule("vpn", {});
    state.criteria = build_firewall_rule_criteria(config.route->rules->front());
    const std::vector<RuleState> realized{state};

    const auto result = run_target_only(config, "1.2.3.4", &network, &realized);
    CHECK(result.entries.front().expected_outbound == "(default)");
    CHECK_FALSE(result.entries.front().default_gateway.has_value());
}

TEST_CASE("compute_test_routing: default_gateway rule of the other family is skipped") {
    const Config config = default_gateway_config();
    const auto network = lan_snapshot();
    const auto result = run_gateway(config, "2001:db8::10", &network);
    CHECK(result.entries.front().expected_outbound == "(default)");
    CHECK_FALSE(result.entries.front().default_gateway.has_value());
}

TEST_CASE("test routing json round-trips default gateway and outbound group") {
    TestRoutingResult result;
    result.target = "1.2.3.4";
    TestRoutingEntry entry;
    entry.ip = "1.2.3.4";
    entry.expected_outbound = "lb";
    entry.actual_outbound = "lb";
    entry.ok = true;
    DefaultGatewayDecision decision;
    decision.rule_index = 13;
    decision.uses_default_gateway = false;
    decision.applied_uses_default_gateway = true;
    decision.bypass = DefaultGatewayBypassEntry{
        "10.8.0.0/24", DefaultGatewayBypassSource::Route, std::string("wg0")};
    entry.default_gateway = decision;
    OutboundGroupBreakdown group;
    group.tag = "lb";
    group.strategy = "balance";
    group.members.push_back({"wan1", std::string("eth3"), 3u, 75u, true});
    group.members.push_back({"wan2", std::nullopt, 1u, 0u, false});
    entry.outbound_group = group;
    result.entries.push_back(entry);

    const auto json = test_routing_result_to_json(result);
    const auto& jentry = json.at("results").at(0);
    CHECK(jentry.at("default_gateway").at("bypass").at("source") == "route");
    CHECK(jentry.at("outbound_group").at("strategy") == "balance");

    const auto back = test_routing_result_from_json(json).entries.at(0);
    REQUIRE(back.default_gateway.has_value());
    CHECK(back.default_gateway->rule_index == 13);
    CHECK(back.default_gateway->uses_default_gateway == std::optional<bool>(false));
    CHECK(back.default_gateway->applied_uses_default_gateway == std::optional<bool>(true));
    REQUIRE(back.default_gateway->bypass.has_value());
    CHECK(back.default_gateway->bypass->cidr == "10.8.0.0/24");
    CHECK(back.default_gateway->bypass->interface == std::optional<std::string>("wg0"));
    REQUIRE(back.outbound_group.has_value());
    CHECK(back.outbound_group->strategy == "balance");
    REQUIRE(back.outbound_group->members.size() == 2);
    CHECK(back.outbound_group->members[0].share_percent == std::optional<uint32_t>(75));
    CHECK(back.outbound_group->members[0].interface_name == std::optional<std::string>("eth3"));
    CHECK(back.outbound_group->members[1].in_use == false);
    CHECK(back.outbound_group->members[1].share_percent == std::optional<uint32_t>(0));
}

namespace {

// A urltest group "lb" over interface outbounds wan1..wanN with the given
// weights; one rule sends everything to it.
Config group_config(const std::vector<uint32_t>& weights, api::TestGroupStrategy strategy) {
    Config config = build_test_config();
    std::vector<Outbound> outbounds;
    OutboundGroup tier;
    std::vector<api::OutboundGroupMember> members;
    for (size_t i = 0; i < weights.size(); ++i) {
        Outbound child;
        child.tag = "wan" + std::to_string(i + 1);
        child.type = OutboundType::INTERFACE;
        child.interface = "eth" + std::to_string(i + 1);
        outbounds.push_back(child);
        api::OutboundGroupMember member;
        member.outbound = child.tag;
        member.weight = weights[i];
        members.push_back(member);
    }
    tier.members = members;
    Outbound group;
    group.tag = "lb";
    group.type = OutboundType::URLTEST;
    group.strategy = strategy;
    group.outbound_groups = std::vector<OutboundGroup>{tier};
    outbounds.push_back(group);
    config.outbounds = outbounds;
    RouteRule rule;
    rule.outbound = "lb";
    rule.proto = "tcp";
    RouteConfig route;
    route.rules = std::vector<RouteRule>{rule};
    config.route = route;
    return config;
}

TestRoutingGroupState group_state_for(size_t members) {
    TestRoutingGroupState state;
    for (size_t i = 0; i < members; ++i) {
        state.outbound_marks["wan" + std::to_string(i + 1)] = 0x100 + static_cast<uint32_t>(i);
    }
    state.outbound_marks["lb"] = 0x200;
    return state;
}

RuleState group_rule_state(std::vector<FirewallBalanceCandidate> candidates) {
    RuleState state;
    state.rule_index = 0;
    state.outbound_tag = "lb";
    state.action_type = RuleActionType::Mark;
    state.fwmark = 0x200;
    state.balance_candidates = std::move(candidates);
    return state;
}

TestRoutingEntry run_group(const Config& config,
                           const std::vector<RuleState>& realized,
                           const TestRoutingGroupState& state,
                           const std::string& target = "203.0.113.10") {
    static std::filesystem::path dir = make_temp_dir();
    CacheManager cache(dir);
    cache.ensure_dir();
    TestRoutingCriteria packet;
    packet.proto = "tcp";
    return compute_test_routing(config, cache, target, packet, &realized, {}, nullptr,
                                true, nullptr, &state).entries.front();
}

} // namespace

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("compute_test_routing: balance group shares sum to 100") {
    struct Case {
        std::vector<uint32_t> weights;
        std::vector<uint32_t> shares;
    };
    const std::vector<Case> cases{
        {{1, 1}, {50, 50}}, {{3, 1}, {75, 25}}, {{1, 1, 1}, {34, 33, 33}}};
    for (const auto& c : cases) {
        const auto config = group_config(c.weights, api::TestGroupStrategy::BALANCE);
        std::vector<FirewallBalanceCandidate> candidates;
        for (size_t i = 0; i < c.weights.size(); ++i) {
            candidates.push_back({0x100 + static_cast<uint32_t>(i), true, true, c.weights[i]});
        }
        const auto entry = run_group(config, {group_rule_state(candidates)},
                                     group_state_for(c.weights.size()));
        REQUIRE(entry.outbound_group.has_value());
        CHECK(entry.outbound_group->strategy == "balance");
        REQUIRE(entry.outbound_group->members.size() == c.shares.size());
        uint32_t sum = 0;
        for (size_t i = 0; i < c.shares.size(); ++i) {
            const auto& member = entry.outbound_group->members[i];
            CHECK(member.in_use);
            CHECK(member.share_percent == std::optional<uint32_t>(c.shares[i]));
            CHECK(member.weight == std::optional<uint32_t>(c.weights[i]));
            CHECK(member.interface_name == std::optional<std::string>("eth" + std::to_string(i + 1)));
            sum += *member.share_percent;
        }
        CHECK(sum == 100);
    }
}

TEST_CASE("compute_test_routing: balance member missing from the candidates is not in use") {
    const auto config = group_config({1, 1}, api::TestGroupStrategy::BALANCE);
    const auto entry = run_group(config, {group_rule_state({{0x100, true, true, 1}})},
                                 group_state_for(2));
    REQUIRE(entry.outbound_group.has_value());
    CHECK(entry.outbound_group->members[0].in_use);
    CHECK(entry.outbound_group->members[0].share_percent == std::optional<uint32_t>(100));
    CHECK_FALSE(entry.outbound_group->members[1].in_use);
    CHECK(entry.outbound_group->members[1].share_percent == std::optional<uint32_t>(0));

    const auto none = run_group(config, {group_rule_state({})}, group_state_for(2));
    REQUIRE(none.outbound_group.has_value());
    for (const auto& member : none.outbound_group->members) CHECK_FALSE(member.in_use);
}

TEST_CASE("compute_test_routing: balance breakdown honours the address family") {
    const auto config = group_config({1, 1}, api::TestGroupStrategy::BALANCE);
    const auto realized = group_rule_state(
        {{0x100, true, false, 1}, {0x101, false, true, 1}});

    const auto v4 = run_group(config, {realized}, group_state_for(2));
    REQUIRE(v4.outbound_group.has_value());
    CHECK(v4.outbound_group->members[0].in_use);
    CHECK_FALSE(v4.outbound_group->members[1].in_use);
    CHECK(v4.outbound_group->members[0].share_percent == std::optional<uint32_t>(100));

    const auto v6 = run_group(config, {realized}, group_state_for(2), "2001:db8::10");
    REQUIRE(v6.outbound_group.has_value());
    CHECK_FALSE(v6.outbound_group->members[0].in_use);
    CHECK(v6.outbound_group->members[1].in_use);
}
#endif

TEST_CASE("compute_test_routing: priority group marks the selected member") {
    const auto config = group_config({1, 1}, api::TestGroupStrategy::PRIORITY);
    auto state = group_state_for(2);
    state.urltest_selections["lb"] = "wan2";
    const auto entry = run_group(config, {group_rule_state({})}, state);
    REQUIRE(entry.outbound_group.has_value());
    CHECK(entry.outbound_group->strategy == "priority");
    REQUIRE(entry.outbound_group->members.size() == 2);
    CHECK_FALSE(entry.outbound_group->members[0].in_use);
    CHECK(entry.outbound_group->members[1].in_use);
    CHECK_FALSE(entry.outbound_group->members[1].weight.has_value());
    CHECK_FALSE(entry.outbound_group->members[1].share_percent.has_value());
}

TEST_CASE("compute_test_routing: no group state means no outbound group") {
    const auto config = group_config({1, 1}, api::TestGroupStrategy::PRIORITY);
    static std::filesystem::path dir = make_temp_dir();
    CacheManager cache(dir);
    cache.ensure_dir();
    const std::vector<RuleState> realized{group_rule_state({})};
    const auto result = compute_test_routing(config, cache, "203.0.113.10", TestRoutingCriteria{},
                                             &realized);
    CHECK_FALSE(result.entries.front().outbound_group.has_value());
}

namespace {

// One rule ("vpn", list-backed) with one row for 1.2.3.4, plus an entry the
// rule selected.
TestRoutingResult diagnosis_fixture() {
    TestRoutingResult result;
    result.target = "example.com";
    result.is_domain = true;
    result.resolved_ips = {"1.2.3.4"};

    TestRoutingEntry entry;
    entry.ip = "1.2.3.4";
    entry.expected_outbound = "vpn";
    entry.actual_outbound = "(default)";
    entry.ok = false;
    entry.matched_rule_index = 2;
    result.entries.push_back(entry);

    RuleDiagnostic rule;
    rule.rule_index = 2;
    rule.rule.outbound = "vpn";
    rule.rule.list = std::vector<std::string>{"vpn"};
    rule.outbound = "vpn";
    RuleIpDiagnostic row;
    row.ip = "1.2.3.4";
    row.in_lists = true;
    row.in_ipset = false;
    row.criteria_match = true;
    rule.ip_rows.push_back(row);
    result.rule_diagnostics.push_back(rule);
    return result;
}

RuleDiagnostic other_rule_holding_ip(int index, const std::string& outbound, bool in_lists) {
    RuleDiagnostic other;
    other.rule_index = index;
    other.rule.outbound = outbound;
    other.rule.list = std::vector<std::string>{outbound};
    other.outbound = outbound;
    RuleIpDiagnostic row;
    row.ip = "1.2.3.4";
    row.in_lists = in_lists;
    row.in_ipset = true;
    other.ip_rows.push_back(row);
    return other;
}

RoutingDiagnosis diagnose(const TestRoutingResult& result) {
    return diagnose_routing_entry(result, result.entries.front());
}

} // namespace

TEST_CASE("diagnose_routing_entry: dns when the domain resolved to nothing") {
    auto result = diagnosis_fixture();
    result.resolved_ips.clear();
    result.entries.front().ip = "(no IPs resolved)";
    result.entries.front().actual_outbound = "(unknown)";
    CHECK(diagnose(result) == RoutingDiagnosis::Dns);

    // An IP target has nothing to resolve.
    auto literal = diagnosis_fixture();
    literal.is_domain = false;
    literal.resolved_ips.clear();
    CHECK(diagnose(literal) == RoutingDiagnosis::MissingIpset);
}

TEST_CASE("diagnose_routing_entry: criteria and network_state for an undecided rule") {
    auto result = diagnosis_fixture();
    auto& entry = result.entries.front();
    entry.expected_outbound = "(unknown)";
    entry.actual_outbound = "(unknown)";
    CHECK(diagnose(result) == RoutingDiagnosis::Criteria);
    entry.default_gateway_directness = true;
    CHECK(diagnose(result) == RoutingDiagnosis::NetworkState);
}

TEST_CASE("diagnose_routing_entry: missing_ipset and firewall") {
    auto result = diagnosis_fixture();
    CHECK(diagnose(result) == RoutingDiagnosis::MissingIpset);

    // Known absence from the set stays a missing set when the actual outbound
    // is unknown; only an unanswered membership check is a firewall issue.
    result.entries.front().actual_outbound = "(unknown)";
    CHECK(diagnose(result) == RoutingDiagnosis::MissingIpset);
    result.rule_diagnostics.front().ip_rows.front().in_ipset = std::nullopt;
    CHECK(diagnose(result) == RoutingDiagnosis::Firewall);

    result.entries.front().actual_outbound = "(default)";
    CHECK(diagnose(result) == RoutingDiagnosis::Mismatch);
}

TEST_CASE("diagnose_routing_entry: ok") {
    auto result = diagnosis_fixture();
    result.entries.front().actual_outbound = "vpn";
    result.entries.front().ok = true;
    CHECK(diagnose(result) == RoutingDiagnosis::Ok);

    TestRoutingResult none;
    none.target = "1.2.3.4";
    TestRoutingEntry entry;
    entry.ip = "1.2.3.4";
    entry.expected_outbound = "(default)";
    entry.actual_outbound = "(default)";
    entry.ok = true;
    none.entries.push_back(entry);
    CHECK(diagnose(none) == RoutingDiagnosis::Ok);
}

TEST_CASE("diagnose_routing_entry: other_ipset and conflicting_ipsets follow the actual outbound") {
    auto result = diagnosis_fixture();
    result.entries.front().actual_outbound = "wan";
    result.rule_diagnostics.insert(result.rule_diagnostics.begin(),
                                   other_rule_holding_ip(1, "wan", true));
    CHECK(diagnose(result) == RoutingDiagnosis::OtherIpset);

    // The other rule's packet criteria rule it out as the culprit.
    result.rule_diagnostics.front().ip_rows.front().criteria_match = false;
    CHECK(diagnose(result) == RoutingDiagnosis::MissingIpset);
    result.rule_diagnostics.front().ip_rows.front().criteria_match = std::nullopt;

    // The expected rule's own set holds the address too.
    result.rule_diagnostics.back().ip_rows.front().in_ipset = true;
    CHECK(diagnose(result) == RoutingDiagnosis::ConflictingIpsets);
}

TEST_CASE("diagnose_routing_entry: stale_ipset without a selected rule") {
    auto result = diagnosis_fixture();
    auto& entry = result.entries.front();
    entry.expected_outbound = "(default)";
    entry.actual_outbound = "wan";
    entry.matched_rule_index = std::nullopt;
    result.rule_diagnostics.clear();
    result.rule_diagnostics.push_back(other_rule_holding_ip(1, "wan", false));
    CHECK(diagnose(result) == RoutingDiagnosis::StaleIpset);

    result.rule_diagnostics.front().rule.enabled = false;
    CHECK(diagnose(result) == RoutingDiagnosis::Mismatch);
}

TEST_CASE("diagnose_routing_entry: expected rule falls back to the first list match") {
    auto result = diagnosis_fixture();
    result.entries.front().matched_rule_index = std::nullopt;
    CHECK(diagnose(result) == RoutingDiagnosis::MissingIpset);

    // A different outbound than the rule's: no rule is credited.
    result.entries.front().expected_outbound = "other";
    CHECK(diagnose(result) == RoutingDiagnosis::Mismatch);
}

TEST_CASE("test-routing JSON carries the diagnosis of every entry") {
    for (const auto code : {RoutingDiagnosis::Dns, RoutingDiagnosis::Criteria,
                            RoutingDiagnosis::NetworkState, RoutingDiagnosis::MissingIpset,
                            RoutingDiagnosis::Firewall, RoutingDiagnosis::Ok,
                            RoutingDiagnosis::OtherIpset, RoutingDiagnosis::ConflictingIpsets,
                            RoutingDiagnosis::StaleIpset, RoutingDiagnosis::Mismatch}) {
        auto result = diagnosis_fixture();
        result.entries.front().diagnosis = code;
        const auto json = test_routing_result_to_json(result);
        CHECK(json.at("results").at(0).at("diagnosis") == routing_diagnosis_name(code));
        CHECK(test_routing_result_from_json(json).entries.front().diagnosis == code);
    }
}

TEST_CASE("compute_test_routing stores the diagnosis on each entry") {
    ScopedIpv6Capabilities ipv6_capabilities;
    const auto temp_dir = make_temp_dir();
    CacheManager cache(temp_dir);
    cache.ensure_dir();

    const auto result = compute_test_routing(build_test_config(), cache, "example.invalid");
    REQUIRE(result.entries.size() == 1);
    CHECK(result.entries.front().diagnosis == RoutingDiagnosis::Dns);

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("test-routing --json passes the daemon response through and keeps the exit code") {
    const nlohmann::json body = {
        {"target", "203.0.113.10"},
        {"is_domain", false},
        {"no_matching_rule", false},
        {"ipv6_enabled", true},
        {"resolved_ips", nlohmann::json::array()},
        {"warnings", {"kept"}},
        {"rule_diagnostics", nlohmann::json::array()},
        {"results",
         {{{"ip", "203.0.113.10"},
           {"expected_outbound", "vpn"},
           {"actual_outbound", "wan"},
           {"ok", false},
           {"diagnosis", "missing_ipset"}}}}};

    std::ostringstream stdout_capture;
    std::ostringstream stderr_capture;
    auto* previous_stdout = std::cout.rdbuf(stdout_capture.rdbuf());
    auto* previous_stderr = std::cerr.rdbuf(stderr_capture.rdbuf());
    const int exit_code = run_test_routing_command({{"ok", true}, {"result", body}}, {}, {true});
    std::cout.rdbuf(previous_stdout);
    std::cerr.rdbuf(previous_stderr);

    CHECK(exit_code == 1);
    CHECK(stderr_capture.str().empty());
    CHECK(nlohmann::json::parse(stdout_capture.str()) == body);
}
