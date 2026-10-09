#include <doctest/doctest.h>

#include "../src/cache/cache_manager.hpp"
#include "../src/cmd/test_routing.hpp"

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

TEST_CASE("compute_test_routing reports unknown when packet criteria are incomplete") {
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

TEST_CASE("compute_test_routing keeps no-list and default-gateway actual state honest") {
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
        config, cache, "203.0.113.10", &states);
    REQUIRE(v4.rule_diagnostics.size() == names.size());
    CHECK_FALSE(v4.rule_diagnostics[0].ip_rows[0].in_ipset.has_value());
    CHECK(v4.rule_diagnostics[1].ip_rows[0].in_ipset == std::optional<bool>(true));
    CHECK(v4.rule_diagnostics[2].ip_rows[0].in_ipset == std::optional<bool>(false));
    // The IPv6 unknown set is not applicable to an IPv4 address.
    CHECK(v4.rule_diagnostics[3].ip_rows[0].in_ipset == std::optional<bool>(false));

    const auto v6 = compute_test_routing(
        config, cache, "2001:db8::10", &states);
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
          {"resolved_ips", {"203.0.113.10"}},
          {"warnings", nlohmann::json::array()},
          {"dns_error", nullptr},
          {"entries",
           {{{"ip", "203.0.113.10"},
             {"expected_outbound", "vpn"},
             {"actual_outbound", "vpn"},
             {"ok", true},
             {"list_match", {{"list_name", "domains"}, {"via", "example.com"}}}}}}}}};

    std::ostringstream stdout_capture;
    std::ostringstream stderr_capture;
    auto* previous_stdout = std::cout.rdbuf(stdout_capture.rdbuf());
    auto* previous_stderr = std::cerr.rdbuf(stderr_capture.rdbuf());
    const int exit_code = run_test_routing_command(response);
    std::cout.rdbuf(previous_stdout);
    std::cerr.rdbuf(previous_stderr);

    CHECK(exit_code == 0);
    CHECK(stderr_capture.str().empty());
    CHECK(stdout_capture.str().find("Target: example.com") != std::string::npos);
    CHECK(stdout_capture.str().find("Expected Outbound") != std::string::npos);
    CHECK(stdout_capture.str().find("domains (via example.com)") != std::string::npos);
    CHECK(stdout_capture.str().find("{\"") == std::string::npos);
}

TEST_CASE("compute_test_routing matches domains through DomainIndex semantics") {
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
