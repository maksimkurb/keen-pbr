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

// Not on Keenetic: the overlap check is disabled there (see
// fwmark_conflict_check_enabled).
#ifndef KEEN_PBR_PLATFORM_KEENETIC
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

namespace {

// Marks 0x01000000..0x15000000 in mask 0xff000000.  keen-pbr routes mark N
// with priority 400+N (lowest 401) into table 150+N.
struct ForeignRuleFixture {
    OutboundMarkMap marks;
    std::vector<RuleSpec> own;
    static constexpr std::uint32_t kMask = 0xFF000000U;

    ForeignRuleFixture() {
        for (std::uint32_t i = 1; i <= 0x15; ++i) {
            marks["out" + std::to_string(i)] = i << 24U;
            RuleSpec spec;
            spec.priority = 400 + i;
            spec.fwmark = i << 24U;
            spec.fwmask = kMask;
            spec.table = 150 + i;
            own.push_back(spec);
        }
    }

    static DumpedRule rule(std::uint32_t priority, std::uint32_t fwmark,
                           std::uint32_t fwmask, bool invert, std::uint32_t table = 7120) {
        DumpedRule r;
        r.priority = priority;
        r.fwmark = fwmark;
        r.fwmask = fwmask;
        r.invert = invert;
        r.table = table;
        return r;
    }

    std::vector<HealthWarning> warnings(const DumpedRule& foreign) const {
        return evaluate_fwmark_conflict_warnings({}, {foreign}, own, kMask, {}, marks);
    }
};

bool has_code(const std::vector<HealthWarning>& warnings, HealthWarningCode code) {
    return std::any_of(warnings.begin(), warnings.end(),
                       [code](const HealthWarning& w) { return w.code == code; });
}

}  // namespace

TEST_CASE("foreign ip rule: not fwmark with a full mask is a precedence issue only") {
    ForeignRuleFixture f;
    const auto w = f.warnings(ForeignRuleFixture::rule(110, 0x1bd00, 0xffffffffU, true));
    REQUIRE(w.size() == 1);
    CHECK(w[0].code == HealthWarningCode::foreign_rule_precedence);
    CHECK(w[0].message.find("priority 110") != std::string::npos);
    CHECK(w[0].message.find("not fwmark 0x0001bd00/0xffffffff lookup 7120") != std::string::npos);
    CHECK(w[0].message.find("all outbounds") != std::string::npos);
    CHECK(w[0].message.find("table 7120 first") != std::string::npos);
    CHECK(w[0].message.find("fwmark.start") == std::string::npos);
}

TEST_CASE("foreign ip rule: a plain fwmark rule that cannot match our marks is ignored") {
    ForeignRuleFixture f;
    CHECK(f.warnings(ForeignRuleFixture::rule(110, 0x1bd00, 0xffffffffU, false)).empty());
}

TEST_CASE("foreign ip rule: a not-rule after keen-pbr's rules is ignored") {
    ForeignRuleFixture f;
    CHECK(f.warnings(ForeignRuleFixture::rule(500, 0x1bd00, 0xffffffffU, true)).empty());
}

TEST_CASE("foreign ip rule: our own mark value before our rules is both issues") {
    ForeignRuleFixture f;
    const auto w = f.warnings(ForeignRuleFixture::rule(100, 0x0a000000U, 0xff000000U, false));
    REQUIRE(w.size() == 2);
    CHECK(has_code(w, HealthWarningCode::fwmark_mask_conflict));
    CHECK(has_code(w, HealthWarningCode::foreign_rule_precedence));
    const auto& precedence = w[0].code == HealthWarningCode::foreign_rule_precedence ? w[0] : w[1];
    CHECK(precedence.message.find("outbounds: out10") != std::string::npos);
    CHECK(precedence.message.find("out11") == std::string::npos);
    const auto& conflict = w[0].code == HealthWarningCode::fwmark_mask_conflict ? w[0] : w[1];
    CHECK(conflict.message.find("fwmark.start/fwmark.mask") != std::string::npos);
}

TEST_CASE("foreign ip rule: our own mark value after our rules is a bit conflict only") {
    ForeignRuleFixture f;
    const auto w = f.warnings(ForeignRuleFixture::rule(30000, 0x0a000000U, 0xff000000U, false));
    REQUIRE(w.size() == 1);
    CHECK(w[0].code == HealthWarningCode::fwmark_mask_conflict);
}

TEST_CASE("foreign ip rule: expecting a value in our bits is a bit conflict") {
    ForeignRuleFixture f;
    const auto w = f.warnings(ForeignRuleFixture::rule(30000, 0x80000000U, 0x80000000U, false));
    REQUIRE(w.size() == 1);
    CHECK(w[0].code == HealthWarningCode::fwmark_mask_conflict);
}

TEST_CASE("foreign ip rule: telling our outbounds apart is a bit conflict") {
    ForeignRuleFixture f;
    // Matches 0x10000000 only among marks 0x01..0x15 (low nibble 0).
    const auto w = f.warnings(ForeignRuleFixture::rule(30000, 0x0, 0x0f000000U, false));
    REQUIRE(w.size() == 1);
    CHECK(w[0].code == HealthWarningCode::fwmark_mask_conflict);
}

TEST_CASE("foreign ip rule: bits outside ours that match everything are only a precedence issue") {
    ForeignRuleFixture f;
    const auto w = f.warnings(ForeignRuleFixture::rule(100, 0x0, 0x00ff0000U, false));
    REQUIRE(w.size() == 1);
    CHECK(w[0].code == HealthWarningCode::foreign_rule_precedence);
}

TEST_CASE("foreign ip rule: a rule without a fwmark selector is ignored") {
    ForeignRuleFixture f;
    CHECK(f.warnings(ForeignRuleFixture::rule(100, 0x0, 0x0, false)).empty());
    CHECK(f.warnings(ForeignRuleFixture::rule(100, 0x0, 0x0, true)).empty());
}

TEST_CASE("foreign ip rule: inverted and plain rules with the same values are separate") {
    ForeignRuleFixture f;
    const auto w = evaluate_fwmark_conflict_warnings(
        {},
        {ForeignRuleFixture::rule(110, 0x1bd00, 0xffffffffU, true),
         ForeignRuleFixture::rule(110, 0x1bd00, 0xffffffffU, false),
         ForeignRuleFixture::rule(110, 0x1bd00, 0xffffffffU, true)},
        f.own, ForeignRuleFixture::kMask, {}, f.marks);
    REQUIRE(w.size() == 1);
    CHECK(w[0].code == HealthWarningCode::foreign_rule_precedence);
}

TEST_CASE("foreign ip rule: the same table as keen-pbr's before it is harmless") {
    ForeignRuleFixture f;
    f.marks = {{"out1", 0x01000000U}};
    // out1 -> table 151.
    CHECK(f.warnings(ForeignRuleFixture::rule(110, 0x1bd00, 0xffffffffU, true, 151)).empty());
}

TEST_CASE("foreign ip rule: a mark keen-pbr has no rule for is harmless") {
    ForeignRuleFixture f;
    f.marks["unrouted"] = 0x30000000U;
    const auto w = f.warnings(ForeignRuleFixture::rule(110, 0x30000000U, 0xff000000U, false));
    // The mask-bit overlap warning still applies; only the precedence one is dropped.
    CHECK_FALSE(has_code(w, HealthWarningCode::foreign_rule_precedence));
}

TEST_CASE("foreign ip rule: only the outbounds with another table are named") {
    ForeignRuleFixture f;
    f.marks = {{"out1", 0x01000000U}, {"out2", 0x02000000U}};
    // "not fwmark 0": every marked packet; table 151 is out1's, out2 uses 152.
    const auto w = f.warnings(ForeignRuleFixture::rule(110, 0x0, 0xff000000U, true, 151));
    REQUIRE(w.size() == 1);
    CHECK(w[0].code == HealthWarningCode::foreign_rule_precedence);
    CHECK(w[0].message.find("outbounds: out2") != std::string::npos);
    CHECK(w[0].message.find("out1") == std::string::npos);
    CHECK(w[0].message.find("all outbounds") == std::string::npos);
}

TEST_CASE("foreign ip rule: a nop rule is harmless") {
    ForeignRuleFixture f;
    auto r = ForeignRuleFixture::rule(110, 0x1bd00, 0xffffffffU, true, 0);
    r.nop = true;
    CHECK(f.warnings(r).empty());
}

TEST_CASE("foreign ip rule: a goto before keen-pbr's rules is harmless") {
    ForeignRuleFixture f;
    auto r = ForeignRuleFixture::rule(110, 0x1bd00, 0xffffffffU, true, 0);
    r.goto_target = 300;
    CHECK(f.warnings(r).empty());
    r.goto_target = 401;  // lands on keen-pbr's first rule
    CHECK(f.warnings(r).empty());
}

TEST_CASE("foreign ip rule: a goto past keen-pbr's rules diverts") {
    ForeignRuleFixture f;
    auto r = ForeignRuleFixture::rule(110, 0x1bd00, 0xffffffffU, true, 0);
    r.goto_target = 32000;
    const auto w = f.warnings(r);
    REQUIRE(w.size() == 1);
    CHECK(w[0].code == HealthWarningCode::foreign_rule_precedence);
    CHECK(w[0].message.find("goto 32000") != std::string::npos);
    CHECK(w[0].message.find("lookup") == std::string::npos);
}

TEST_CASE("foreign ip rule: a blackhole differs from keen-pbr's lookup") {
    ForeignRuleFixture f;
    auto r = ForeignRuleFixture::rule(110, 0x1bd00, 0xffffffffU, true, 0);
    r.action = RuleAction::blackhole;
    const auto w = f.warnings(r);
    REQUIRE(w.size() == 1);
    CHECK(w[0].message.find("blackhole") != std::string::npos);
}

TEST_CASE("foreign ip rule: rules differing only in the table are judged separately") {
    ForeignRuleFixture f;
    f.marks = {{"out1", 0x01000000U}};
    const auto w = evaluate_fwmark_conflict_warnings(
        {},
        {ForeignRuleFixture::rule(110, 0x1bd00, 0xffffffffU, true, 151),
         ForeignRuleFixture::rule(110, 0x1bd00, 0xffffffffU, true, 7120)},
        f.own, ForeignRuleFixture::kMask, {}, f.marks);
    REQUIRE(w.size() == 1);
    CHECK(w[0].message.find("lookup 7120") != std::string::npos);
}

TEST_CASE("foreign ip rule: without own rules nothing is reported") {
    ForeignRuleFixture f;
    f.own.clear();
    CHECK(f.warnings(ForeignRuleFixture::rule(110, 0x1bd00, 0xffffffffU, true)).empty());
}

TEST_CASE("routing health JSON carries foreign_rule_precedence") {
    RoutingHealthReport r;
    r.overall_ok = true;
    r.firewall_backend = FirewallBackend::iptables;
    HealthWarning w;
    w.code = HealthWarningCode::foreign_rule_precedence;
    w.message = "precedence";
    r.warnings.push_back(w);
    CHECK(routing_health_report_to_json(r).at("warnings")[0].at("code") ==
          "foreign_rule_precedence");
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

#endif  // KEEN_PBR_PLATFORM_KEENETIC
