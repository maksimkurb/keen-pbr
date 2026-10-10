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

namespace {
std::vector<Ipv4Net> lan(const char* cidr) {
    return {*parse_ipv4_net(cidr)};
}
} // namespace

TEST_CASE("nat parser: restricted rules are partial") {
    const auto ts = nat_coverage_for_interface(
        "-A POSTROUTING -j ts-postrouting\n"
        "-A ts-postrouting -m mark --mark 0x40000/0xff0000 -j MASQUERADE\n",
        "eth1");
    CHECK(ts.coverage == NatCoverage::partial);
    CHECK(ts.restriction == "-m mark --mark 0x40000/0xff0000");
    CHECK(cov("-A POSTROUTING -p udp -j MASQUERADE\n", "eth1") ==
          NatCoverage::partial);
    CHECK(cov("-A POSTROUTING -d 10.0.0.0/8 -j MASQUERADE\n", "eth1") ==
          NatCoverage::partial);
    CHECK(cov("-A POSTROUTING -i br0 -j MASQUERADE\n", "eth1") ==
          NatCoverage::partial);
}

TEST_CASE("nat parser: target options and comments do not restrict") {
    CHECK(cov("-A POSTROUTING -j SNAT --to-source 1.2.3.4 -o eth1\n", "eth1") ==
          NatCoverage::covered);
    CHECK(cov("-A POSTROUTING -o eth1 -j SNAT --to-source 1.2.3.4 --random\n",
              "eth1") == NatCoverage::covered);
    CHECK(cov("-A POSTROUTING -o eth1 -m comment --comment \"wan nat\" -j MASQUERADE\n",
              "eth1") == NatCoverage::covered);
    CHECK(cov("-A POSTROUTING ! -d 10.0.0.0/8 -o eth1 -j MASQUERADE\n", "eth1") ==
          NatCoverage::covered);
}

TEST_CASE("nat parser: only chains reachable from POSTROUTING count") {
    CHECK(cov("-N CUSTOM\n-A CUSTOM -o eth1 -j MASQUERADE\n", "eth1") ==
          NatCoverage::missing);
    CHECK(cov("-N CUSTOM\n-N INNER\n-A POSTROUTING -j CUSTOM\n"
              "-A CUSTOM -g INNER\n-A INNER -o eth1 -j MASQUERADE\n",
              "eth1") == NatCoverage::covered);
    CHECK(cov("-A PREROUTING -o eth1 -j MASQUERADE\n", "eth1") ==
          NatCoverage::missing);
}

TEST_CASE("nat parser: -s rules are judged against the LAN") {
    const std::string docker =
        "-A POSTROUTING -s 172.17.0.0/16 ! -o docker0 -j MASQUERADE\n";
    CHECK(cov(docker, "eth1") == NatCoverage::covered);  // LAN unknown
    CHECK(nat_coverage_for_interface(docker, "eth1", lan("192.168.1.0/24"))
              .coverage == NatCoverage::missing);

    const std::string ufw = "-A POSTROUTING -s 192.168.1.0/24 -o eth1 -j MASQUERADE\n";
    CHECK(nat_coverage_for_interface(ufw, "eth1", lan("192.168.1.0/24")).coverage ==
          NatCoverage::covered);
    CHECK(nat_coverage_for_interface(ufw, "eth1", lan("192.168.0.0/16")).coverage ==
          NatCoverage::partial);
    const auto partial = nat_coverage_for_interface(
        "-A POSTROUTING -s 192.168.1.128/25 -j MASQUERADE\n", "eth1",
        lan("192.168.1.0/24"));
    CHECK(partial.coverage == NatCoverage::partial);
    CHECK(partial.restriction == "-s 192.168.1.128/25");
    CHECK(nat_coverage_for_interface(
              "-A POSTROUTING -s 10.0.0.0/8 -j MASQUERADE\n", "eth1",
              {*parse_ipv4_net("10.1.0.0/24"), *parse_ipv4_net("10.2.0.0/24")})
              .coverage == NatCoverage::covered);
}

TEST_CASE("interface_subnets extracts networks of the named interfaces") {
    DumpedInterface a;
    a.name = "br0";
    a.ipv4_addresses = {"192.168.1.1/24"};
    DumpedInterface b;
    b.name = "eth9";
    b.ipv4_addresses = {"10.9.9.9/8"};
    const auto nets = interface_subnets({a, b}, {"br0"});
    REQUIRE(nets.size() == 1);
    CHECK(nets[0].addr == 0xC0A80100u);
    CHECK(nets[0].mask == 0xFFFFFF00u);
    CHECK(interface_subnets({a, b}, {}).empty());
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
        "-A POSTROUTING -p udp -o eth3 -j MASQUERADE\n");
    REQUIRE(w.size() == 2);
    CHECK(w[0].code == HealthWarningCode::nat_missing);
    CHECK(w[0].interface == "eth2");
    CHECK(w[1].code == HealthWarningCode::nat_partial);
    CHECK(w[1].message.find("-p udp") != std::string::npos);
    CHECK(w[0].message.find("nftables") != std::string::npos);
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

namespace {

constexpr std::uint32_t kDefaultMask = 0x00FF0000U;

std::uint32_t overlap_of(const std::string& rule, std::uint32_t mask = kDefaultMask) {
    const auto found = find_foreign_mark_overlaps("-A foo " + rule + "\n", mask);
    return found.empty() ? 0U : found[0].overlap;
}

}  // namespace

TEST_CASE("fwmark overlap: MARK target forms") {
    CHECK(overlap_of("-j MARK --set-xmark 0x40000/0xff0000") == 0x00FF0000U);
    CHECK(overlap_of("-j MARK --set-xmark 0x1/0xff") == 0U);
    CHECK(overlap_of("-j MARK --set-mark 0x1") == 0x00FF0000U);  // all bits
    CHECK(overlap_of("-j MARK --set-mark 0x1/0xf") == 0U);
    CHECK(overlap_of("-j MARK --or-mark 0x20000") == 0x00020000U);
    CHECK(overlap_of("-j MARK --or-mark 0x1") == 0U);
    CHECK(overlap_of("-j MARK --xor-mark 0x10000") == 0x00010000U);
    CHECK(overlap_of("-j MARK --and-mark 0xffff00ff") == 0U);
    CHECK(overlap_of("-j MARK --and-mark 0xff00ffff") == 0x00FF0000U);
    CHECK(overlap_of("-j MARK --and-mark 0xffffff00") == 0U);
}

TEST_CASE("fwmark overlap: CONNMARK target forms") {
    CHECK(overlap_of("-j CONNMARK --set-xmark 0x80000/0xf0000") == 0x000F0000U);
    CHECK(overlap_of("-j CONNMARK --set-xmark 0x1/0xff") == 0U);
    CHECK(overlap_of("-j CONNMARK --set-mark 0x5") == 0x00FF0000U);
    CHECK(overlap_of("-j CONNMARK --save-mark") == 0x00FF0000U);
    CHECK(overlap_of("-j CONNMARK --restore-mark") == 0x00FF0000U);
    CHECK(overlap_of("-j CONNMARK --save-mark --nfmask 0xff --ctmask 0xff") == 0U);
    CHECK(overlap_of("-j CONNMARK --save-mark --nfmask 0xff --ctmask 0xff0000") ==
          0x00FF0000U);
    CHECK(overlap_of("-j CONNMARK --restore-mark --nfmask 0xff00 --ctmask 0xff") == 0U);
    CHECK(overlap_of("-j CONNMARK --restore-mark --nfmask 0x10000 --ctmask 0xff") ==
          0x00010000U);
    CHECK(overlap_of("-j CONNMARK --save-mark --mask 0xffff") == 0U);
    CHECK(overlap_of("-j CONNMARK --restore-mark --mask 0xf0000") == 0x000F0000U);
    CHECK(overlap_of("-j CONNMARK --and-mark 0xff00ffff") == 0x00FF0000U);
    CHECK(overlap_of("-j CONNMARK --or-mark 0x10000") == 0x00010000U);
}

TEST_CASE("fwmark overlap: mark matches") {
    CHECK(overlap_of("-m mark --mark 0x40000/0xff0000 -j ACCEPT") == 0x00FF0000U);
    CHECK(overlap_of("-m connmark --mark 0x1/0xff -j ACCEPT") == 0U);
    CHECK(overlap_of("-m mark ! --mark 0x40000 -j ACCEPT") == 0x00040000U);
    CHECK(overlap_of("-m mark --mark 0x1 -j ACCEPT") == 0U);
    CHECK(overlap_of("-j ACCEPT") == 0U);
}

TEST_CASE("fwmark overlap: keen-pbr rules are excluded") {
    const std::string text =
        "-N KeenPbrTable\n"
        "-A PREROUTING -j KeenPbrTable\n"
        "-A KeenPbrTable -j MARK --set-xmark 0x10000/0xff0000\n"
        "-A KeenPbrOutput -j CONNMARK --save-mark --nfmask 0xff0000 --ctmask 0xff0000\n"
        "-A PREROUTING -m comment --comment \"kpbr:v1:abc\" -j MARK --set-xmark 0x10000/0xff0000\n";
    CHECK(find_foreign_mark_overlaps(text, kDefaultMask).empty());
}

TEST_CASE("fwmark overlap: tailscale-like rules") {
    const std::string text =
        "-N ts-forward\n"
        "-A FORWARD -j ts-forward\n"
        "-A ts-postrouting -m mark --mark 0x40000/0xff0000 -j MASQUERADE\n"
        "-A ts-forward -i tailscale0 -j MARK --set-xmark 0x40000/0xff0000\n"
        "-A ts-forward -o tailscale0 -j ACCEPT\n";
    const auto hit = find_foreign_mark_overlaps(text, kDefaultMask);
    CHECK(hit.size() == 2);
    CHECK(find_foreign_mark_overlaps(text, 0xFF000000U).empty());

    // Identical IPv4/IPv6 text folds into one warning per distinct rule.
    const auto w = evaluate_fwmark_conflict_warnings(
        {{"mangle", text}, {"mangle", text}}, {}, {}, kDefaultMask);
    REQUIRE(w.size() == 2);
    CHECK(w[0].code == HealthWarningCode::fwmark_mask_conflict);
    CHECK(w[0].message.find("0x00ff0000") != std::string::npos);
    CHECK(w[0].message.find("ts-postrouting") != std::string::npos);
    CHECK(w[0].message.find("fwmark.start/fwmark.mask") != std::string::npos);
    CHECK(evaluate_fwmark_conflict_warnings({{"mangle", text}}, {}, {}, 0xFF000000U)
              .empty());
}

TEST_CASE("fwmark overlap: foreign ip rules") {
    DumpedRule foreign;
    foreign.priority = 5270;
    foreign.fwmark = 0x80000;
    foreign.fwmask = 0xff0000;
    foreign.table = 52;
    DumpedRule disjoint = foreign;
    disjoint.priority = 100;
    disjoint.fwmark = 0x1;
    disjoint.fwmask = 0xff;
    DumpedRule none;  // no fwmark selector
    none.priority = 32766;
    none.table = 254;
    DumpedRule own;
    own.priority = 100;
    own.fwmark = 0x10000;
    own.fwmask = 0xff0000;
    own.table = 150;
    RuleSpec own_spec;
    own_spec.priority = 100;
    own_spec.fwmark = 0x10000;
    own_spec.fwmask = 0xff0000;
    own_spec.table = 150;

    const auto hits = find_foreign_policy_rule_overlaps(
        {foreign, foreign, disjoint, none, own}, {own_spec}, kDefaultMask);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].priority == 5270);
    CHECK(hits[0].overlap == 0x00FF0000U);
    CHECK(find_foreign_policy_rule_overlaps({foreign}, {}, 0xFF000000U).empty());

    const auto w = evaluate_fwmark_conflict_warnings({}, {foreign}, {own_spec},
                                                     kDefaultMask);
    REQUIRE(w.size() == 1);
    CHECK(w[0].message.find("priority 5270") != std::string::npos);
    CHECK(w[0].message.find("0x00080000/0x00ff0000") != std::string::npos);
}

TEST_CASE("routing health JSON serialises fwmark_mask_conflict") {
    RoutingHealthReport r;
    r.overall_ok = true;
    r.firewall_backend = FirewallBackend::iptables;
    HealthWarning w;
    w.code = HealthWarningCode::fwmark_mask_conflict;
    w.message = "overlap";
    r.warnings.push_back(w);
    const auto j = routing_health_report_to_json(r);
    CHECK(j.at("overall") == "ok");
    CHECK(j.at("warnings")[0].at("code") == "fwmark_mask_conflict");
}
