#include <doctest/doctest.h>

#include "../src/health/host_health_warnings.hpp"
#include "../src/health/routing_health_checker.hpp"

#include <map>

using namespace keen_pbr3;

namespace {

NatCoverage cov(const std::string& text, const std::string& iface) {
    return nat_coverage_for_interface(text, iface).coverage;
}

ProcFileReader fake_proc(std::map<std::string, std::string> files) {
    return [files = std::move(files)](const std::string& path)
               -> std::optional<std::string> {
        const auto it = files.find(path);
        if (it == files.end()) return std::nullopt;
        return it->second;
    };
}

const char* kAll = "/proc/sys/net/ipv4/conf/all/rp_filter";
const char* kEth1 = "/proc/sys/net/ipv4/conf/eth1/rp_filter";

} // namespace

TEST_CASE("nat parser: covered by plain MASQUERADE") {
    const std::string nat =
        "-P POSTROUTING ACCEPT\n-A POSTROUTING -o eth1 -j MASQUERADE\n";
    CHECK(cov(nat, "eth1") == NatCoverage::covered);
    CHECK(cov(nat, "eth2") == NatCoverage::missing);
}

TEST_CASE("nat parser: SNAT counts") {
    CHECK(cov("-A POSTROUTING -o eth1 -j SNAT --to-source 192.0.2.1\n", "eth1") ==
          NatCoverage::covered);
}

TEST_CASE("nat parser: no -o matches every interface") {
    CHECK(cov("-A POSTROUTING -j MASQUERADE\n", "eth9") == NatCoverage::covered);
}

TEST_CASE("nat parser: wildcard and negation") {
    CHECK(cov("-A POSTROUTING -o eth+ -j MASQUERADE\n", "eth1") ==
          NatCoverage::covered);
    CHECK(cov("-A POSTROUTING -o eth+ -j MASQUERADE\n", "ppp0") ==
          NatCoverage::missing);
    CHECK(cov("-A POSTROUTING ! -o docker0 -j MASQUERADE\n", "eth1") ==
          NatCoverage::covered);
    CHECK(cov("-A POSTROUTING ! -o docker0 -j MASQUERADE\n", "docker0") ==
          NatCoverage::missing);
}

TEST_CASE("nat parser: docker rule is only partial") {
    const auto r = nat_coverage_for_interface(
        "-A POSTROUTING -s 172.17.0.0/16 ! -o docker0 -j MASQUERADE\n", "eth1");
    CHECK(r.coverage == NatCoverage::partial);
    CHECK(r.restriction == "-s 172.17.0.0/16");
}

TEST_CASE("nat parser: other targets are ignored, multiple chains searched") {
    const std::string nat =
        "-N CUSTOM\n"
        "-A POSTROUTING -o eth1 -j ACCEPT\n"
        "-A POSTROUTING -s 10.0.0.0/8 -o eth1 -j MASQUERADE\n"
        "-A CUSTOM -o eth1 -j MASQUERADE\n";
    CHECK(cov(nat, "eth1") == NatCoverage::covered);
    CHECK(cov("-A POSTROUTING -o eth1 -j ACCEPT\n", "eth1") == NatCoverage::missing);
    CHECK(cov("-A POSTROUTING -s 0.0.0.0/0 -o eth1 -j MASQUERADE\n", "eth1") ==
          NatCoverage::covered);
}

TEST_CASE("rp_filter evaluation uses max(all, iface)") {
    const std::vector<BalanceCandidate> c{{"eth1", "wan1"}};
    CHECK(evaluate_rp_filter_warnings(c, fake_proc({{kAll, "0"}, {kEth1, "1"}}))
              .size() == 1);
    CHECK(evaluate_rp_filter_warnings(c, fake_proc({{kAll, "1"}, {kEth1, "2"}}))
              .empty());
    CHECK(evaluate_rp_filter_warnings(c, fake_proc({{kAll, "2"}, {kEth1, "0"}}))
              .empty());
    CHECK(evaluate_rp_filter_warnings(c, fake_proc({{kAll, "1"}})).empty());
    const auto w =
        evaluate_rp_filter_warnings(c, fake_proc({{kAll, "1"}, {kEth1, "0"}}));
    REQUIRE(w.size() == 1);
    CHECK(w[0].code == HealthWarningCode::rp_filter_strict);
    CHECK(w[0].interface == "eth1");
    CHECK(w[0].outbound == "wan1");
}

TEST_CASE("balance candidate interfaces resolve and deduplicate") {
    const Config cfg = parse_config(R"({"outbounds":[
      {"type":"interface","tag":"wan1","interface":"eth1"},
      {"type":"interface","tag":"wan2","interface":"eth2"},
      {"type":"interface","tag":"wan2b","interface":"eth2"},
      {"type":"table","tag":"tbl","table":100},
      {"type":"interface","tag":"unused","interface":"eth9"},
      {"type":"urltest","tag":"auto","url":"http://example.test","strategy":"balance",
       "outbound_groups":[{"outbounds":["wan1","wan2","wan2b","tbl"]}]},
      {"type":"urltest","tag":"prio","url":"http://example.test",
       "outbound_groups":[{"outbounds":["unused"]}]}
    ]})");
    const auto c = balance_candidate_interfaces(cfg);
#ifdef KEEN_PBR_PLATFORM_KEENETIC
    CHECK(c.empty());
#else
    REQUIRE(c.size() == 2);
    CHECK(c[0].interface == "eth1");
    CHECK(c[0].outbound_tag == "wan1");
    CHECK(c[1].interface == "eth2");
#endif
}

TEST_CASE("nat warnings map coverage to codes") {
    const std::vector<BalanceCandidate> c{{"eth1", "a"}, {"eth2", "b"}, {"eth3", "c"}};
    const auto w = evaluate_nat_warnings(
        c,
        "-A POSTROUTING -o eth1 -j MASQUERADE\n"
        "-A POSTROUTING -s 192.168.1.0/24 -o eth3 -j MASQUERADE\n");
    REQUIRE(w.size() == 2);
    CHECK(w[0].code == HealthWarningCode::nat_missing);
    CHECK(w[0].interface == "eth2");
    CHECK(w[1].code == HealthWarningCode::nat_partial);
    CHECK(w[1].message.find("192.168.1.0/24") != std::string::npos);
}

TEST_CASE("routing health JSON serialises warnings without changing overall") {
    RoutingHealthReport r;
    r.overall_ok = true;
    r.firewall_backend = FirewallBackend::iptables;
    HealthWarning w;
    w.code = HealthWarningCode::nat_missing;
    w.interface = "eth2";
    w.outbound = "wan2";
    w.message = "no nat";
    r.warnings.push_back(w);
    const auto j = routing_health_report_to_json(r);
    CHECK(j.at("overall") == "ok");
    REQUIRE(j.at("warnings").size() == 1);
    CHECK(j.at("warnings")[0].at("code") == "nat_missing");
    CHECK(j.at("warnings")[0].at("interface") == "eth2");
    CHECK(j.at("warnings")[0].at("outbound") == "wan2");
    CHECK(j.at("warnings")[0].at("message") == "no nat");
}
