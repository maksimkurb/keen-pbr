#include <doctest/doctest.h>

#include "../src/cmd/test_routing_report.hpp"

#include <iostream>

using namespace keen_pbr3;

namespace {

TestRoutingResult yandex_missing_ipset() {
    TestRoutingResult result;
    result.target = "yandex.ru";
    result.is_domain = true;
    result.resolved_ips = {"77.88.44.55", "77.88.55.88", "5.255.255.77"};
    TestRoutingDnsInfo dns;
    dns.resolver = "dnsmasq";
    dns.endpoint = "127.0.0.1:53";
    dns.instance = "uci:cfg01411c";
    result.dns = dns;

    RuleDiagnostic rule;
    rule.rule_index = 7;
    rule.rule.outbound = "home_auto";
    rule.rule.list = std::vector<std::string>{"russia"};
    rule.outbound = "home_auto";
    rule.target_in_lists = true;
    for (const auto& ip : result.resolved_ips) {
        TestRoutingEntry entry;
        entry.ip = ip;
        entry.expected_outbound = "home_auto";
        entry.actual_outbound = "wan_loadbalance";
        entry.ok = false;
        entry.matched_rule_index = 7;
        entry.list_match = ListMatchInfo{"russia", "ru"};
        entry.diagnosis = RoutingDiagnosis::MissingIpset;
        OutboundGroupBreakdown actual;
        actual.tag = "wan_loadbalance";
        actual.strategy = "balance";
        actual.members = {{"wan1", std::nullopt, 5, 83, true}, {"wan2", std::nullopt, 1, 17, true}};
        entry.outbound_group = actual;
        result.entries.push_back(entry);

        RuleIpDiagnostic row;
        row.ip = ip;
        row.in_lists = true;
        row.in_ipset = false;
        row.criteria_match = true;
        row.set_write_evidence = SetWriteEvidence{SetWriteEvidenceStatus::NoRecord, std::nullopt};
        rule.ip_rows.push_back(row);
    }
    result.rule_diagnostics.push_back(rule);
    return result;
}

} // namespace

namespace {

TestRoutingResult make_ok(TestRoutingResult result) {
    for (auto& entry : result.entries) {
        entry.ok = true;
        entry.actual_outbound = "home_auto";
        entry.diagnosis = RoutingDiagnosis::Ok;
        entry.outbound_group.reset();
    }
    return result;
}

const term::Style kPlain{};

} // namespace

TEST_CASE("report: an all-OK result is one line per step") {
    const auto text = format_test_routing_report(make_ok(yandex_missing_ipset()), {}, kPlain);
    CHECK(text ==
          "[ok] The routes match the rules - yandex.ru\n"
          "\n"
          "  [ok] DNS resolution - 77.88.44.55  77.88.55.88  5.255.255.77\n"
          "  [ok] List matching - russia (entry ru)\n"
          "  [ok] Rule selection - Rule #8 -> home_auto\n"
          "  [ok] Firewall check - 3 of 3 IPs match - home_auto\n"
          "\n"
          "  This checks the route from firewall state, not website connectivity.\n");
}

TEST_CASE("report: a missing ipset is explained step by step") {
    const auto text = format_test_routing_report(yandex_missing_ipset(), {}, kPlain);
    CHECK(text ==
          "[!!] IP not in the expected ipset - yandex.ru\n"
          "\n"
          "  [ok] DNS resolution\n"
          "      77.88.44.55  77.88.55.88  5.255.255.77\n"
          "      Resolved by dnsmasq 127.0.0.1:53 (uci:cfg01411c)\n"
          "      This check uses the router's DNS. Devices that use DoH, DoT or their own DNS "
          "server may get different addresses.\n"
          "  [ok] List matching\n"
          "      russia (entry ru)\n"
          "  [ok] Rule selection\n"
          "      Rule #8 -> home_auto\n"
          "  [!!] Firewall check - 0 of 3 IPs match\n"
          "      Expected  home_auto\n"
          "      Actual    wan_loadbalance  balance: wan1 83% - wan2 17%\n"
          "      - The IPs are not in this rule's ipset.\n"
          "      - keen-pbr has no record of adding them.\n"
          "\n"
          "  The domain is in the list, but its IPs are not in the rule's ipset yet. keen-pbr "
          "adds addresses when a client looks the domain up through the router's DNS.\n"
          "    1. Open this site on the client device and run the check again.\n"
          "    2. Still missing - clear the DNS cache on the device and in the browser, then try "
          "again.\n"
          "    3. Still missing - make sure the device does not use third-party DNS (DoH, DoT or "
          "its own DNS server) that keen-pbr cannot intercept. To check, open the keen-pbr home "
          "page from that device and look at the \"Domain-based routing\" section, and see the "
          "requests log.\n");
}

TEST_CASE("report: addresses with different outcomes get their own block") {
    auto result = yandex_missing_ipset();
    result.entries[0] = make_ok(yandex_missing_ipset()).entries[0];
    result.rule_diagnostics[0].ip_rows[0].in_ipset = true;
    const auto text = format_test_routing_report(result, {}, kPlain);

    CHECK(text.find("[!!] Firewall check - 1 of 3 IPs match\n"
                    "      77.88.44.55: Matches\n"
                    "      Expected  home_auto\n"
                    "      Actual    home_auto\n"
                    "      77.88.55.88, 5.255.255.77: Mismatch\n"
                    "      Expected  home_auto\n"
                    "      Actual    wan_loadbalance  balance: wan1 83% - wan2 17%\n"
                    "      - The IPs are not in this rule's ipset.\n") != std::string::npos);
    // The advice names only the addresses it applies to.
    CHECK(text.find("\n  77.88.55.88, 5.255.255.77:\n  The domain is in the list") !=
          std::string::npos);
    CHECK(text.find("Route problems") == std::string::npos);
}

TEST_CASE("report: different problems get a heading each") {
    auto result = yandex_missing_ipset();
    result.entries[0].diagnosis = RoutingDiagnosis::Mismatch;
    const auto text = format_test_routing_report(result, {}, kPlain);

    CHECK(text.rfind("[!!] Route problems detected - yandex.ru\n", 0) == 0);
    CHECK(text.find("\n  IP not in the expected ipset\n") != std::string::npos);
    CHECK(text.find("\n  The route differs from the expected route\n") != std::string::npos);
}

TEST_CASE("report: a default-gateway rule that skips the address says why") {
    auto result = yandex_missing_ipset();
    result.is_domain = false;
    result.target = "10.8.0.5";
    result.resolved_ips.clear();
    result.dns.reset();
    result.entries.resize(1);
    result.entries[0].ip = "10.8.0.5";
    result.entries[0].diagnosis = RoutingDiagnosis::Mismatch;
    result.rule_diagnostics[0].ip_rows.resize(1);
    DefaultGatewayDecision decision;
    decision.rule_index = 13;
    decision.uses_default_gateway = false;
    decision.bypass = DefaultGatewayBypassEntry{"10.8.0.0/24", DefaultGatewayBypassSource::Route,
                                                std::string("wg0")};
    result.entries[0].default_gateway = decision;

    const auto text = format_test_routing_report(result, {}, kPlain);
    CHECK(text.find("  [ok] Rule selection\n"
                    "      Rule #14 (default gateway) skipped: 10.8.0.5 has its own route "
                    "10.8.0.0/24 (wg0)\n"
                    "      Rule #8 -> home_auto\n") != std::string::npos);
}

TEST_CASE("report: a default-gateway skip also shows in the compact form") {
    auto result = make_ok(yandex_missing_ipset());
    result.entries.resize(1);
    result.rule_diagnostics[0].ip_rows.resize(1);
    DefaultGatewayDecision decision;
    decision.rule_index = 13;
    decision.uses_default_gateway = false;
    decision.bypass = DefaultGatewayBypassEntry{"10.8.0.0/24", DefaultGatewayBypassSource::Route,
                                                std::string("wg0")};
    result.entries[0].default_gateway = decision;

    const auto text = format_test_routing_report(result, {}, kPlain);
    CHECK(text.find("[ok] Rule selection - Rule #14 (default gateway) skipped: 10.8.0.5") ==
          std::string::npos);
    CHECK(text.find("[ok] Rule selection - Rule #14 (default gateway) skipped: 77.88.44.55 has "
                    "its own route 10.8.0.0/24 (wg0); Rule #8 -> home_auto\n") !=
          std::string::npos);
}

TEST_CASE("report: a rule that needs more request fields names the flags") {
    auto result = yandex_missing_ipset();
    result.entries.resize(1);
    result.rule_diagnostics[0].ip_rows.resize(1);
    auto& entry = result.entries[0];
    entry.expected_outbound = "(unknown)";
    entry.actual_outbound = "(unknown)";
    entry.matched_rule_index.reset();
    entry.undetermined_rule_index = 7;
    entry.missing_criteria = {"src_addr", "src_port"};
    entry.diagnosis = RoutingDiagnosis::Criteria;

    const auto text = format_test_routing_report(result, {}, kPlain);
    CHECK(text.rfind("[!!] The route cannot be determined yet - yandex.ru\n", 0) == 0);
    CHECK(text.find("      Pass --src-addr --src-port to pin down the rule.\n"
                    "      Rule #8 (-> home_auto) checks \"Source IP\" and \"Source port\". Until "
                    "those fields are filled in, there is no telling whether the traffic matches "
                    "it.\n") != std::string::npos);
}

TEST_CASE("report: unreadable routes are explained once, in the rule step") {
    auto result = yandex_missing_ipset();
    result.entries.resize(1);
    result.rule_diagnostics[0].ip_rows.resize(1);
    auto& entry = result.entries[0];
    entry.expected_outbound = "(unknown)";
    entry.actual_outbound = "(unknown)";
    entry.matched_rule_index.reset();
    entry.undetermined_rule_index = 7;
    entry.default_gateway_directness = true;
    entry.diagnosis = RoutingDiagnosis::NetworkState;

    const auto text = format_test_routing_report(result, {}, kPlain);
    CHECK(text.rfind("[!!] Route depends on the network state - yandex.ru\n", 0) == 0);
    CHECK(text.find("keen-pbr couldn't read the router's routes, so it can't tell whether "
                    "77.88.44.55 does.") != std::string::npos);
    CHECK(text.find("Pass --") == std::string::npos);
}

TEST_CASE("report: a domain that resolved to nothing") {
    TestRoutingResult result;
    result.target = "example.invalid";
    result.is_domain = true;
    TestRoutingEntry entry;
    entry.ip = "(no IPs resolved)";
    entry.expected_outbound = "(default)";
    entry.actual_outbound = "(unknown)";
    entry.diagnosis = RoutingDiagnosis::Dns;
    result.entries.push_back(entry);

    const auto text = format_test_routing_report(result, {}, kPlain);
    CHECK(text ==
          "[!!] No IP address resolved - example.invalid\n"
          "\n"
          "  [!!] DNS resolution\n"
          "      DNS returned no IP addresses for this domain.\n"
          "      This check uses the router's DNS. Devices that use DoH, DoT or their own DNS "
          "server may get different addresses.\n"
          "  - List matching\n"
          "      No match\n"
          "  - Rule selection\n"
          "      No rule matched. The system route is used.\n"
          "\n"
          "  Without an IP address, firewall membership cannot be checked.\n"
          "  Check the domain and the router's DNS, then try again.\n");
}

TEST_CASE("report: packet criteria are echoed under the verdict") {
    TestRoutingCriteria criteria;
    criteria.proto = "udp";
    criteria.dest_port = 53;
    const auto text = format_test_routing_report(yandex_missing_ipset(), criteria, kPlain);
    CHECK(text.find("yandex.ru\nPacket: proto=udp dest_port=53\n\n") != std::string::npos);
}

TEST_CASE("report: color follows the style and the text stays ASCII") {
    term::Style style;
    style.color = true;
    const auto text = format_test_routing_report(make_ok(yandex_missing_ipset()), {}, style);

    CHECK(text.find("\033[32m[ok]\033[0m \033[1mThe routes match the rules\033[0m - yandex.ru\n") == 0);
    CHECK(text.find("Rule #8 -> home_auto") != std::string::npos);
    for (const char c : text) CHECK(static_cast<unsigned char>(c) < 0x80);

    const auto plain = format_test_routing_report(make_ok(yandex_missing_ipset()), {}, kPlain);
    CHECK(plain.find('\033') == std::string::npos);
}
