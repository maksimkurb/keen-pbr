#include <doctest/doctest.h>

#include "../src/config/config.hpp"
#include "../src/config/routing_state.hpp"
#include "../src/firewall/ipset_restore_pipe.hpp"
#include "../src/firewall/iptables.hpp"
#include "../src/lists/list_entry_visitor.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace keen_pbr3 {

// Friend class with test access to IptablesFirewall private methods.
class IptablesBuilderTest {
public:
  // Public mirror of PendingRule for use in test functions.
  struct RuleDesc {
    std::string set_name;
    bool ipv6;
    bool direct = false;
    enum Action { Mark, Drop, Pass } action;
    uint32_t fwmark;
    ProtoPortFilter filter;
  };

  static std::string build_ipset_create_line(const std::string &name,
                                             const std::string &family_str,
                                             uint32_t timeout) {
    IptablesFirewall::PendingSet ps;
    ps.name = name;
    ps.family_str = family_str;
    ps.timeout = timeout;
    return IptablesFirewall::build_ipset_create_line(ps);
  }

  static std::string build_ipt_script(bool ipv6,
                                      const std::vector<RuleDesc> &descs,
                                      FirewallGlobalPrefilter prefilter = {}) {
    std::vector<IptablesFirewall::PendingRule> rules;
    rules.reserve(descs.size());
    for (const auto &d : descs) {
      IptablesFirewall::PendingRule pr;
      pr.set_name = d.set_name;
      pr.ipv6 = d.ipv6;
      pr.direct = d.direct;
      if (d.action == RuleDesc::Mark) {
        pr.action = IptablesFirewall::PendingRule::Mark;
      } else if (d.action == RuleDesc::Drop) {
        pr.action = IptablesFirewall::PendingRule::Drop;
      } else {
        pr.action = IptablesFirewall::PendingRule::Pass;
      }
      pr.fwmark = d.fwmark;
      pr.filter = d.filter;
      rules.push_back(std::move(pr));
    }
    return IptablesFirewall::build_ipt_script(ipv6, rules, prefilter);
  }

  static std::string build_proto_port_fragment(const std::string &proto,
                                               const std::string &src_port,
                                               const std::string &dst_port,
                                               bool negate_src = false,
                                               bool negate_dst = false) {
    return IptablesFirewall::build_proto_port_fragment(
        proto, src_port, dst_port, negate_src, negate_dst);
  }
};

} // namespace keen_pbr3

using namespace keen_pbr3;
using T = IptablesBuilderTest;
using Rule = IptablesBuilderTest::RuleDesc;

namespace {

Config parse_valid_config(const std::string& json) {
  Config cfg = parse_config(json);
  if (!cfg.dns.has_value()) {
    cfg.dns = DnsConfig{};
  }
  if (!cfg.dns->servers.has_value()) {
    DnsServer fallback_server;
    fallback_server.tag = "default_dns";
    fallback_server.address = "127.0.0.1";
    cfg.dns->servers = std::vector<DnsServer>{fallback_server};
  }
  if (!cfg.dns->fallback.has_value()) {
    cfg.dns->fallback = std::vector<std::string>{"default_dns"};
  }
  if (!cfg.dns->system_resolver.has_value()) {
    api::SystemResolver resolver;
    resolver.type = DnsSystemResolverType::DNSMASQ_NFTSET;
    resolver.address = "127.0.0.1";
    cfg.dns->system_resolver = resolver;
  }
  validate_config(cfg);
  return cfg;
}

} // namespace

static Rule mark_rule(const std::string &set_name, bool ipv6, uint32_t fwmark,
                      ProtoPortFilter filter = {}) {
  Rule r;
  r.set_name = set_name;
  r.ipv6 = ipv6;
  r.direct = false;
  r.action = Rule::Mark;
  r.fwmark = fwmark;
  r.filter = filter;
  return r;
}

static Rule drop_rule(const std::string &set_name, bool ipv6,
                      ProtoPortFilter filter = {}) {
  Rule r;
  r.set_name = set_name;
  r.ipv6 = ipv6;
  r.direct = false;
  r.action = Rule::Drop;
  r.fwmark = 0;
  r.filter = filter;
  return r;
}

static Rule pass_rule(const std::string &set_name, bool ipv6,
                      ProtoPortFilter filter = {}) {
  Rule r;
  r.set_name = set_name;
  r.ipv6 = ipv6;
  r.direct = false;
  r.action = Rule::Pass;
  r.fwmark = 0;
  r.filter = filter;
  return r;
}

static FirewallGlobalPrefilter prefilter_with_interfaces(
    std::vector<std::string> interfaces,
    bool skip_established_or_dnat = true) {
  FirewallGlobalPrefilter prefilter;
  prefilter.skip_established_or_dnat = skip_established_or_dnat;
  prefilter.inbound_interfaces = std::move(interfaces);
  return prefilter;
}

// =============================================================================
// IpsetRestoreVisitor::on_entry tests
// =============================================================================

TEST_CASE("IpsetRestoreVisitor: IP entry without timeout") {
  std::ostringstream buf;
  IpsetRestoreVisitor v(buf, "myset");
  v.on_entry(EntryType::Ip, "10.0.0.1");
  CHECK(buf.str() == "add myset 10.0.0.1\n");
  CHECK(v.count() == 1);
}

TEST_CASE("IpsetRestoreVisitor: CIDR entry") {
  std::ostringstream buf;
  IpsetRestoreVisitor v(buf, "myset");
  v.on_entry(EntryType::Cidr, "192.168.0.0/24");
  CHECK(buf.str() == "add myset 192.168.0.0/24\n");
  CHECK(v.count() == 1);
}

TEST_CASE("IpsetRestoreVisitor: Domain entry is ignored") {
  std::ostringstream buf;
  IpsetRestoreVisitor v(buf, "myset");
  v.on_entry(EntryType::Domain, "example.com");
  CHECK(buf.str().empty());
  CHECK(v.count() == 0);
}

TEST_CASE("IpsetRestoreVisitor: count increments only for IP/CIDR") {
  std::ostringstream buf;
  IpsetRestoreVisitor v(buf, "myset");
  v.on_entry(EntryType::Ip, "1.2.3.4");
  v.on_entry(EntryType::Domain, "example.com");
  v.on_entry(EntryType::Cidr, "10.0.0.0/8");
  CHECK(v.count() == 2);
}

// =============================================================================
// build_ipset_create_line tests
// =============================================================================

TEST_CASE("build_ipset_create_line: IPv4 without timeout") {
  auto line = T::build_ipset_create_line("myset", "inet", 0);
  CHECK(line == "create myset hash:net family inet -exist\n");
}

TEST_CASE("build_ipset_create_line: IPv4 with timeout 60") {
  auto line = T::build_ipset_create_line("myset", "inet", 60);
  CHECK(line == "create myset hash:net family inet timeout 60 -exist\n");
}

TEST_CASE("build_ipset_create_line: IPv6 without timeout") {
  auto line = T::build_ipset_create_line("myset", "inet6", 0);
  CHECK(line == "create myset hash:net family inet6 -exist\n");
}

// =============================================================================
// build_ipt_script tests
// =============================================================================

TEST_CASE("build_ipt_script: IPv4 mark rule") {
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100)});
  CHECK(s.find("*mangle") != std::string::npos);
  CHECK(s.find(":KeenPbrTable") != std::string::npos);
  CHECK(s.find("-A PREROUTING -j KeenPbrTable") != std::string::npos);
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -j MARK "
               "--set-mark 0x100") != std::string::npos);
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -j RETURN") !=
        std::string::npos);
  CHECK(s.size() >= 7);
  CHECK(s.substr(s.size() - 7) == "COMMIT\n");
}

TEST_CASE("build_ipt_script: IPv4 drop rule") {
  auto s = T::build_ipt_script(false, {drop_rule("blacklist", false)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set blacklist dst -j DROP") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: IPv4 pass rule") {
  auto s = T::build_ipt_script(false, {pass_rule("allowlist", false)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set allowlist dst -j RETURN") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: IPv6 mark rule") {
  auto s = T::build_ipt_script(true, {mark_rule("v6set", true, 0x200)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set v6set dst -j MARK "
               "--set-mark 0x200") != std::string::npos);
  CHECK(s.find("-A KeenPbrTable -m set --match-set v6set dst -j RETURN") !=
        std::string::npos);
  CHECK(s.substr(s.size() - 7) == "COMMIT\n");
}

TEST_CASE("build_ipt_script: ipv6=false filters out IPv6 rules") {
  auto s = T::build_ipt_script(false, {mark_rule("v4set", false, 0x100),
                                       mark_rule("v6set", true, 0x200)});
  CHECK(s.find("v4set") != std::string::npos);
  CHECK(s.find("v6set") == std::string::npos);
}

TEST_CASE("build_ipt_script: ipv6=true filters out IPv4 rules") {
  auto s = T::build_ipt_script(true, {mark_rule("v4set", false, 0x100),
                                      mark_rule("v6set", true, 0x200)});
  CHECK(s.find("v6set") != std::string::npos);
  CHECK(s.find("v4set") == std::string::npos);
}

TEST_CASE("build_ipt_script: zero fwmark") {
  auto s = T::build_ipt_script(false, {mark_rule("zeroset", false, 0)});
  CHECK(s.find("--set-mark 0x0") != std::string::npos);
}

TEST_CASE("build_ipt_script: multiple rules appear in order") {
  auto s = T::build_ipt_script(
      false, {mark_rule("first", false, 0x1), drop_rule("second", false)});
  auto pos_first = s.find("first");
  auto pos_second = s.find("second");
  CHECK(pos_first != std::string::npos);
  CHECK(pos_second != std::string::npos);
  CHECK(pos_first < pos_second);
}

TEST_CASE("build_ipt_script: empty rules still build KeenPbrTable scaffold") {
  auto s = T::build_ipt_script(false, {});
  CHECK(s.find("*mangle\n") != std::string::npos);
  CHECK(s.find(":KeenPbrTable - [0:0]\n") != std::string::npos);
  CHECK(s.find("-A PREROUTING -j KeenPbrTable\n") != std::string::npos);
  CHECK(s.find("-A KeenPbrTable ") == std::string::npos);
  CHECK(s == "*mangle\n:KeenPbrTable - [0:0]\n-A PREROUTING -j KeenPbrTable\nCOMMIT\n");
}

TEST_CASE("build_ipt_script: global prefilter RETURN lines are emitted before route rules") {
  auto s = T::build_ipt_script(
      false,
      {mark_rule("myset", false, 0x100)},
      prefilter_with_interfaces({"br0"}));

  const std::string dnat =
      "-A KeenPbrTable -m conntrack --ctstate DNAT -j RETURN\n";
  const std::string iface =
      "-A KeenPbrTable ! -i br0 -j RETURN\n";
  const std::string mark =
      "-A KeenPbrTable -m set --match-set myset dst -j MARK --set-mark 0x100\n";

  const auto dnat_pos = s.find(dnat);
  const auto iface_pos = s.find(iface);
  const auto mark_pos = s.find(mark);
  REQUIRE(dnat_pos != std::string::npos);
  REQUIRE(iface_pos != std::string::npos);
  REQUIRE(mark_pos != std::string::npos);
  CHECK(s.find("--ctstate RELATED,ESTABLISHED") == std::string::npos);
  CHECK(dnat_pos < iface_pos);
  CHECK(iface_pos < mark_pos);
}

TEST_CASE("build_ipt_script: multi-interface prefilter expands route rules with -i matches") {
  auto s = T::build_ipt_script(
      false,
      {pass_rule("allowlist", false)},
      prefilter_with_interfaces({"br0", "wg0"}, false));

  CHECK(s.find("-A KeenPbrTable -m set --match-set allowlist dst -i br0 -j RETURN\n") !=
        std::string::npos);
  CHECK(s.find("-A KeenPbrTable -m set --match-set allowlist dst -i wg0 -j RETURN\n") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: config-derived prefilter keeps route rule body unchanged") {
  auto cfg = parse_valid_config(R"({
    "outbounds":[
      {"tag":"wan","type":"interface","interface":"eth0","gateway":"192.0.2.1"}
    ],
    "lists":{
      "local":{"ip_cidrs":["192.168.0.0/16"]}
    },
    "route":{
      "inbound_interfaces":["br0"],
      "rules":[
        {"list":["local"],"outbound":"wan"}
      ]
    }
  })");

  const auto prefilter = build_firewall_global_prefilter(cfg);
  auto s = T::build_ipt_script(false, {mark_rule("kpbr4_local", false, 0x100)}, prefilter);

  const std::string iface = "-A KeenPbrTable ! -i br0 -j RETURN\n";
  const std::string mark =
      "-A KeenPbrTable -m set --match-set kpbr4_local dst -j MARK --set-mark 0x100\n";
  const auto iface_pos = s.find(iface);
  const auto mark_pos = s.find(mark);
  REQUIRE(iface_pos != std::string::npos);
  REQUIRE(mark_pos != std::string::npos);
  CHECK(iface_pos < mark_pos);
}

TEST_CASE("build_ipt_script: config-derived prefilter omits interface guard when inbound list is empty") {
  auto cfg = parse_valid_config(R"({
    "outbounds":[
      {"tag":"wan","type":"interface","interface":"eth0","gateway":"192.0.2.1"}
    ],
    "lists":{
      "local":{"ip_cidrs":["192.168.0.0/16"]}
    },
    "route":{
      "inbound_interfaces":[],
      "rules":[
        {"list":["local"],"outbound":"wan"}
      ]
    }
  })");

  const auto prefilter = build_firewall_global_prefilter(cfg);
  auto s = T::build_ipt_script(false, {mark_rule("kpbr4_local", false, 0x100)}, prefilter);

  CHECK(s.find("! -i ") == std::string::npos);
  CHECK(s.find("-A KeenPbrTable -m set --match-set kpbr4_local dst -j MARK --set-mark 0x100\n") !=
        std::string::npos);
}

// =============================================================================
// build_proto_port_fragment tests
// =============================================================================

TEST_CASE("build_proto_port_fragment: empty filter → empty string") {
  CHECK(T::build_proto_port_fragment("", "", "") == "");
}

TEST_CASE("build_proto_port_fragment: tcp + single dest_port") {
  auto frag = T::build_proto_port_fragment("tcp", "", "443");
  CHECK(frag == " -p tcp --dport 443");
}

TEST_CASE("build_proto_port_fragment: udp + port range") {
  auto frag = T::build_proto_port_fragment("udp", "", "8000-9000");
  CHECK(frag == " -p udp --dport 8000:9000");
}

TEST_CASE("build_proto_port_fragment: tcp + port list → multiport") {
  auto frag = T::build_proto_port_fragment("tcp", "", "80,443");
  CHECK(frag == " -p tcp -m multiport --dports 80,443");
}

TEST_CASE("build_proto_port_fragment: src_port + dest_port → multiport") {
  auto frag = T::build_proto_port_fragment("tcp", "1024-65535", "80");
  CHECK(frag == " -p tcp -m multiport --sports 1024:65535 --dports 80");
}

TEST_CASE("build_proto_port_fragment: proto only, no ports") {
  auto frag = T::build_proto_port_fragment("udp", "", "");
  CHECK(frag == " -p udp");
}

// =============================================================================
// build_ipt_script with proto/port filter tests
// =============================================================================

TEST_CASE("build_ipt_script: tcp + single dest_port in rule") {
  ProtoPortFilter f;
  f.proto = "tcp";
  f.dst_port = "443";
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -p tcp --dport "
               "443 -j MARK --set-mark 0x100") != std::string::npos);
}

TEST_CASE("build_ipt_script: udp + port range in rule") {
  ProtoPortFilter f;
  f.proto = "udp";
  f.dst_port = "8000-9000";
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set bl dst -p udp --dport "
               "8000:9000 -j DROP") != std::string::npos);
}

TEST_CASE("build_ipt_script: tcp/udp + port list → two rules") {
  ProtoPortFilter f;
  f.proto = "tcp/udp";
  f.dst_port = "80,443";
  // create_mark_rule expands tcp/udp, so we simulate by passing two rules
  // already expanded
  ProtoPortFilter ftcp;
  ftcp.proto = "tcp";
  ftcp.dst_port = "80,443";
  ProtoPortFilter fudp;
  fudp.proto = "udp";
  fudp.dst_port = "80,443";
  auto s = T::build_ipt_script(false, {mark_rule("s", false, 0x10, ftcp),
                                       mark_rule("s", false, 0x10, fudp)});
  CHECK(s.find("-p tcp -m multiport --dports 80,443") != std::string::npos);
  CHECK(s.find("-p udp -m multiport --dports 80,443") != std::string::npos);
}

TEST_CASE(
    "build_ipt_script: no proto, no ports → no extra flags (regression)") {
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -j MARK "
               "--set-mark 0x100") != std::string::npos);
  CHECK(s.find("-p ") == std::string::npos);
  CHECK(s.find("--dport") == std::string::npos);
}

// =============================================================================
// build_ipt_script with src_addr / dest_addr tests
// =============================================================================

TEST_CASE("build_ipt_script: single src_addr → -s flag") {
  ProtoPortFilter f;
  f.src_addr = {"192.168.10.0/24"};
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -s "
               "192.168.10.0/24 -j MARK --set-mark 0x100") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: single dest_addr → -d flag") {
  ProtoPortFilter f;
  f.dst_addr = {"10.0.0.0/8"};
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -d 10.0.0.0/8 -j "
               "MARK --set-mark 0x100") != std::string::npos);
}

TEST_CASE("build_ipt_script: src_addr + dest_addr → both flags") {
  ProtoPortFilter f;
  f.src_addr = {"192.168.1.0/24"};
  f.dst_addr = {"8.8.8.0/24"};
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -s 192.168.1.0/24 "
               "-d 8.8.8.0/24 -j MARK --set-mark 0x100") != std::string::npos);
}

TEST_CASE("build_ipt_script: src_addr + tcp/udp + dest_port → addr and proto "
          "present") {
  ProtoPortFilter f;
  f.src_addr = {"192.168.1.0/24"};
  f.proto = "tcp";
  f.dst_port = "443";
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -s 192.168.1.0/24 "
               "-p tcp --dport 443 -j MARK --set-mark 0x100") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: drop rule with src_addr → -s flag on DROP") {
  ProtoPortFilter f;
  f.src_addr = {"10.10.0.0/16"};
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set bl dst -s 10.10.0.0/16 -j "
               "DROP") != std::string::npos);
}

// =============================================================================
// build_proto_port_fragment negation tests
// =============================================================================

TEST_CASE(
    "build_proto_port_fragment: negated dest_port (single) → ! --dport 443") {
  auto frag = T::build_proto_port_fragment("tcp", "", "443", false, true);
  CHECK(frag == " -p tcp ! --dport 443");
}

TEST_CASE(
    "build_proto_port_fragment: negated port range → ! --dport 8000:9000") {
  auto frag = T::build_proto_port_fragment("udp", "", "8000-9000", false, true);
  CHECK(frag == " -p udp ! --dport 8000:9000");
}

TEST_CASE("build_proto_port_fragment: negated multiport list → -m multiport ! "
          "--dports 80,443") {
  auto frag = T::build_proto_port_fragment("tcp", "", "80,443", false, true);
  CHECK(frag == " -p tcp -m multiport ! --dports 80,443");
}

TEST_CASE("build_proto_port_fragment: negated src_port only → ! --sport 1024") {
  auto frag = T::build_proto_port_fragment("tcp", "1024", "", true, false);
  CHECK(frag == " -p tcp ! --sport 1024");
}

TEST_CASE(
    "build_proto_port_fragment: both ports negated → single multiport clause") {
  auto frag =
      T::build_proto_port_fragment("tcp", "1024-65535", "80", true, true);
  CHECK(frag == " -p tcp -m multiport ! --sports 1024:65535 ! --dports 80");
}

TEST_CASE(
    "build_proto_port_fragment: mixed negation → separate multiport clauses") {
  auto frag = T::build_proto_port_fragment("tcp", "1024", "443", true, false);
  CHECK(frag.find("-m multiport ! --sports 1024") != std::string::npos);
  CHECK(frag.find("-m multiport --dports 443") != std::string::npos);
}

// =============================================================================
// build_ipt_script negation tests
// =============================================================================

TEST_CASE("build_ipt_script: negated src_addr → ! -s flag") {
  ProtoPortFilter f;
  f.src_addr = {"192.168.1.0/24"};
  f.negate_src_addr = true;
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst ! -s "
               "192.168.1.0/24 -j MARK --set-mark 0x100") != std::string::npos);
}

TEST_CASE("build_ipt_script: negated dest_addr → ! -d flag") {
  ProtoPortFilter f;
  f.dst_addr = {"10.0.0.0/8"};
  f.negate_dst_addr = true;
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst ! -d 10.0.0.0/8 "
               "-j MARK --set-mark 0x100") != std::string::npos);
}

TEST_CASE("build_ipt_script: negated dest_port in full rule") {
  ProtoPortFilter f;
  f.proto = "tcp";
  f.dst_port = "443";
  f.negate_dst_port = true;
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -p tcp ! --dport "
               "443 -j MARK --set-mark 0x100") != std::string::npos);
}

TEST_CASE("build_ipt_script: combined negated src_addr + negated dest_port") {
  ProtoPortFilter f;
  f.src_addr = {"192.168.1.0/24"};
  f.negate_src_addr = true;
  f.proto = "tcp";
  f.dst_port = "443";
  f.negate_dst_port = true;
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(
      s.find("-A KeenPbrTable -m set --match-set myset dst ! -s 192.168.1.0/24 "
             "-p tcp ! --dport 443 -j MARK --set-mark 0x100") !=
      std::string::npos);
}

TEST_CASE("build_ipt_script: drop rule with negated src_addr") {
  ProtoPortFilter f;
  f.src_addr = {"10.10.0.0/16"};
  f.negate_src_addr = true;
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set bl dst ! -s 10.10.0.0/16 -j "
               "DROP") != std::string::npos);
}

// =============================================================================
// Multiple CIDR / port negation tests
// (expand_and_push emits one rule per CIDR; each carries the shared negate
// flag)
// =============================================================================

TEST_CASE(
    "build_ipt_script: two negated src_addrs → two rules each with ! -s") {
  // Simulate what expand_and_push produces for negate_src_addr + two CIDRs
  ProtoPortFilter f1;
  f1.src_addr = {"192.168.1.0/24"};
  f1.negate_src_addr = true;
  ProtoPortFilter f2;
  f2.src_addr = {"10.0.0.0/8"};
  f2.negate_src_addr = true;
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f1),
                                       mark_rule("myset", false, 0x100, f2)});
  CHECK(s.find("! -s 192.168.1.0/24") != std::string::npos);
  CHECK(s.find("! -s 10.0.0.0/8") != std::string::npos);
  // Both are mark rules
  CHECK(s.find("! -s 192.168.1.0/24 -j MARK") != std::string::npos);
  CHECK(s.find("! -s 10.0.0.0/8 -j MARK") != std::string::npos);
}

TEST_CASE(
    "build_ipt_script: two negated dst_addrs → two rules each with ! -d") {
  ProtoPortFilter f1;
  f1.dst_addr = {"8.8.8.0/24"};
  f1.negate_dst_addr = true;
  ProtoPortFilter f2;
  f2.dst_addr = {"1.1.1.0/24"};
  f2.negate_dst_addr = true;
  auto s = T::build_ipt_script(
      false, {drop_rule("bl", false, f1), drop_rule("bl", false, f2)});
  CHECK(s.find("! -d 8.8.8.0/24") != std::string::npos);
  CHECK(s.find("! -d 1.1.1.0/24") != std::string::npos);
}

TEST_CASE("build_proto_port_fragment: negated src port list → -m multiport ! "
          "--sports 80,8080") {
  auto frag = T::build_proto_port_fragment("tcp", "80,8080", "", true, false);
  CHECK(frag == " -p tcp -m multiport ! --sports 80,8080");
}

TEST_CASE(
    "build_proto_port_fragment: negated src port range → ! --sport 8000:9000") {
  auto frag = T::build_proto_port_fragment("tcp", "8000-9000", "", true, false);
  CHECK(frag == " -p tcp ! --sport 8000:9000");
}

// =============================================================================
// Mixed negation documentation tests
// (current design: negation is per-list, determined by the first element)
// =============================================================================

TEST_CASE(
    "build_ipt_script: non-negated and negated src_addrs in separate rules") {
  // A non-negated CIDR and a negated CIDR produce independent rules — each can
  // match different traffic, so there is no contradiction.
  ProtoPortFilter fpos;
  fpos.src_addr = {"172.16.0.0/12"};
  ProtoPortFilter fneg;
  fneg.src_addr = {"10.0.0.0/8"};
  fneg.negate_src_addr = true;
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, fpos),
                                       mark_rule("myset", false, 0x100, fneg)});
  CHECK(s.find(" -s 172.16.0.0/12") != std::string::npos);
  CHECK(s.find("! -s 10.0.0.0/8") != std::string::npos);
}

// =============================================================================
// Static / dynamic set split tests
// =============================================================================

TEST_CASE("static set naming: kpbr4_ prefix, no timeout") {
  auto line = T::build_ipset_create_line("kpbr4_mylist", "inet", 0);
  CHECK(line == "create kpbr4_mylist hash:net family inet -exist\n");
}

TEST_CASE("dynamic set naming: kpbr4d_ prefix, no timeout when ttl_ms=0") {
  auto line = T::build_ipset_create_line("kpbr4d_mylist", "inet", 0);
  CHECK(line == "create kpbr4d_mylist hash:net family inet -exist\n");
}

TEST_CASE("dynamic set naming: kpbr4d_ prefix, with timeout when ttl_ms set") {
  auto line = T::build_ipset_create_line("kpbr4d_mylist", "inet", 3600);
  CHECK(line == "create kpbr4d_mylist hash:net family inet timeout 3600 -exist\n");
}

TEST_CASE("dynamic set naming: kpbr6d_ IPv6 with timeout") {
  auto line = T::build_ipset_create_line("kpbr6d_mylist", "inet6", 86400);
  CHECK(line == "create kpbr6d_mylist hash:net family inet6 timeout 86400 -exist\n");
}

TEST_CASE("dual-set mark rules: both static and dynamic sets get mark rules") {
  auto s = T::build_ipt_script(false, {mark_rule("kpbr4_mylist", false, 0x100),
                                       mark_rule("kpbr4d_mylist", false, 0x100)});
  CHECK(s.find("--match-set kpbr4_mylist dst -j MARK --set-mark 0x100") != std::string::npos);
  CHECK(s.find("--match-set kpbr4d_mylist dst -j MARK --set-mark 0x100") != std::string::npos);
}

TEST_CASE("dual-set drop rules: both static and dynamic sets get drop rules") {
  auto s = T::build_ipt_script(false, {drop_rule("kpbr4_mylist", false),
                                       drop_rule("kpbr4d_mylist", false)});
  CHECK(s.find("--match-set kpbr4_mylist dst -j DROP") != std::string::npos);
  CHECK(s.find("--match-set kpbr4d_mylist dst -j DROP") != std::string::npos);
}

TEST_CASE("dual-set IPv6 mark rules: kpbr6_ and kpbr6d_ both matched") {
  auto s = T::build_ipt_script(true, {mark_rule("kpbr6_mylist", true, 0x200),
                                      mark_rule("kpbr6d_mylist", true, 0x200)});
  CHECK(s.find("--match-set kpbr6_mylist dst -j MARK --set-mark 0x200") != std::string::npos);
  CHECK(s.find("--match-set kpbr6d_mylist dst -j MARK --set-mark 0x200") != std::string::npos);
}

// Helper for direct (no-set) mark rules
static Rule direct_mark_rule(bool ipv6, uint32_t fwmark, ProtoPortFilter filter = {}) {
  Rule r;
  r.set_name = "";
  r.ipv6 = ipv6;
  r.direct = true;
  r.action = Rule::Mark;
  r.fwmark = fwmark;
  r.filter = filter;
  return r;
}

// =============================================================================
// create_direct_mark_rule / build_ipt_script with direct=true tests
// =============================================================================

TEST_CASE("build_ipt_script: direct mark rule IPv4 UDP dst port 53") {
  ProtoPortFilter f;
  f.proto = "udp";
  f.dst_port = "53";
  f.dst_addr = {"10.8.0.1"};
  auto s = T::build_ipt_script(false, {direct_mark_rule(false, 0x10000, f)});
  // Must NOT contain --match-set
  CHECK(s.find("--match-set") == std::string::npos);
  // Must contain dst addr and port
  CHECK(s.find("-d 10.8.0.1") != std::string::npos);
  CHECK(s.find("--dport 53") != std::string::npos);
  CHECK(s.find("-j MARK --set-mark 0x10000") != std::string::npos);
}

TEST_CASE("build_ipt_script: direct mark rule IPv4 TCP dst port 53") {
  ProtoPortFilter f;
  f.proto = "tcp";
  f.dst_port = "53";
  f.dst_addr = {"10.8.0.1"};
  auto s = T::build_ipt_script(false, {direct_mark_rule(false, 0x10000, f)});
  CHECK(s.find("--match-set") == std::string::npos);
  CHECK(s.find("-d 10.8.0.1") != std::string::npos);
  CHECK(s.find("-p tcp --dport 53") != std::string::npos);
  CHECK(s.find("-j MARK --set-mark 0x10000") != std::string::npos);
}

TEST_CASE("build_ipt_script: direct mark rule has no set_name reference") {
  ProtoPortFilter f;
  f.proto = "udp";
  f.dst_addr = {"192.0.2.1"};
  auto s = T::build_ipt_script(false, {direct_mark_rule(false, 0x20000, f)});
  CHECK(s.find("--match-set") == std::string::npos);
  CHECK(s.find("-d 192.0.2.1") != std::string::npos);
}
