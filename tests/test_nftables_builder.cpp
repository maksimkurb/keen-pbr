#include <doctest/doctest.h>

#include "../src/config/config.hpp"
#include "../src/config/routing_state.hpp"
#include "../src/firewall/nft_batch_pipe.hpp"
#include "../src/firewall/nftables.hpp"

#include <nlohmann/json.hpp>
#include <sys/socket.h>

// NftablesBuilderTest must be in the same namespace as the friend declaration
// (keen_pbr3::NftablesBuilderTest matches the friend class NftablesBuilderTest
// declared inside namespace keen_pbr3).
namespace keen_pbr3 {

// Friend class with test access to NftablesFirewall private methods.
// Helper methods take plain parameters and build the private structs
// internally, because friend access applies inside member function bodies, not
// at class scope.
class NftablesBuilderTest {
public:
  // set helpers
  static nlohmann::json build_table_json() {
    return NftablesFirewall::build_table_json();
  }

  static nlohmann::json build_set_json(const std::string &name,
                                       const std::string &type,
                                       uint32_t timeout) {
    NftablesFirewall::PendingSet ps;
    ps.name = name;
    ps.type = type;
    ps.timeout = timeout;
    return NftablesFirewall::build_set_json(ps);
  }

  static nlohmann::json build_chain_json() {
    return NftablesFirewall::build_chain_json();
  }

  struct RuleDesc {
    std::string set_name;
    int family;
    bool direct = false;
    enum Action { Mark, Drop, Pass } action;
    uint32_t fwmark;
    ProtoPortFilter filter;
  };

  static nlohmann::json build_rule_add_commands(
      FirewallGlobalPrefilter prefilter,
      const std::vector<RuleDesc> &descs) {
    std::vector<NftablesFirewall::PendingRule> rules;
    rules.reserve(descs.size());
    for (const auto &d : descs) {
      NftablesFirewall::PendingRule pr;
      pr.set_name = d.set_name;
      pr.family = d.family;
      pr.direct = d.direct;
      if (d.action == RuleDesc::Mark) {
        pr.action = NftablesFirewall::PendingRule::Mark;
      } else if (d.action == RuleDesc::Drop) {
        pr.action = NftablesFirewall::PendingRule::Drop;
      } else {
        pr.action = NftablesFirewall::PendingRule::Pass;
      }
      pr.fwmark = d.fwmark;
      pr.filter = d.filter;
      rules.push_back(std::move(pr));
    }
    return NftablesFirewall::build_rule_add_commands(prefilter, rules);
  }

  static nlohmann::json build_mark_rule_json(const std::string &set_name,
                                             int family, uint32_t fwmark,
                                             ProtoPortFilter filter = {},
                                             bool direct = false) {
    NftablesFirewall::PendingRule pr;
    pr.set_name = set_name;
    pr.family = family;
    pr.direct = direct;
    pr.action = NftablesFirewall::PendingRule::Mark;
    pr.fwmark = fwmark;
    pr.filter = filter;
    return NftablesFirewall::build_mark_rule_json(pr);
  }

  static nlohmann::json build_drop_rule_json(const std::string &set_name,
                                             int family,
                                             ProtoPortFilter filter = {}) {
    NftablesFirewall::PendingRule pr;
    pr.set_name = set_name;
    pr.family = family;
    pr.action = NftablesFirewall::PendingRule::Drop;
    pr.fwmark = 0;
    pr.filter = filter;
    return NftablesFirewall::build_drop_rule_json(pr);
  }

  static nlohmann::json build_pass_rule_json(const std::string &set_name,
                                             int family,
                                             ProtoPortFilter filter = {}) {
    NftablesFirewall::PendingRule pr;
    pr.set_name = set_name;
    pr.family = family;
    pr.action = NftablesFirewall::PendingRule::Pass;
    pr.fwmark = 0;
    pr.filter = filter;
    return NftablesFirewall::build_pass_rule_json(pr);
  }

  static nlohmann::json build_port_match_exprs(const std::string &proto,
                                               const std::string &src_port,
                                               const std::string &dst_port,
                                               bool negate_src = false,
                                               bool negate_dst = false) {
    return NftablesFirewall::build_port_match_exprs(proto, src_port, dst_port,
                                                    negate_src, negate_dst);
  }

  static nlohmann::json
  build_addr_match_exprs(const std::string &ip_proto,
                         const std::vector<std::string> &src_addr,
                         const std::vector<std::string> &dst_addr,
                         bool negate_src = false, bool negate_dst = false) {
    return NftablesFirewall::build_addr_match_exprs(
        ip_proto, src_addr, dst_addr, negate_src, negate_dst);
  }

  static nlohmann::json build_elements_json(const std::string &set_name,
                                            const nlohmann::json &elems) {
    return NftablesFirewall::build_elements_json(set_name, elems);
  }
};

} // namespace keen_pbr3

using namespace keen_pbr3;
using T = NftablesBuilderTest;
using Rule = NftablesBuilderTest::RuleDesc;

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

static Rule mark_rule(const std::string &set_name, int family, uint32_t fwmark,
                      ProtoPortFilter filter = {}) {
  Rule r;
  r.set_name = set_name;
  r.family = family;
  r.direct = false;
  r.action = Rule::Mark;
  r.fwmark = fwmark;
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
// build_set_json tests
// =============================================================================

TEST_CASE("build_set_json: IPv4 without timeout") {
  auto j = T::build_set_json("myset", "ipv4_addr", 0);
  const auto &set = j["add"]["set"];
  CHECK(set["type"] == "ipv4_addr");
  CHECK(set["flags"].is_array());
  CHECK(set["flags"].size() == 1);
  CHECK(set["flags"][0] == "interval");
  CHECK(set["auto-merge"] == true);
  CHECK_FALSE(set.contains("timeout"));
}

TEST_CASE("build_set_json: IPv4 with timeout") {
  auto j = T::build_set_json("timeset", "ipv4_addr", 60);
  const auto &set = j["add"]["set"];
  CHECK(set["flags"].is_array());
  CHECK(set["flags"].size() == 2);
  CHECK(set["flags"][0] == "interval");
  CHECK(set["flags"][1] == "timeout");
  CHECK(set["auto-merge"] == true);
  CHECK(set.contains("timeout"));
  CHECK(set["timeout"] == 60);
}

TEST_CASE("build_set_json: IPv6 set") {
  auto j = T::build_set_json("v6set", "ipv6_addr", 0);
  CHECK(j["add"]["set"]["type"] == "ipv6_addr");
}

// =============================================================================
// build_chain_json tests
// =============================================================================

TEST_CASE("build_chain_json: correct fields") {
  auto j = T::build_chain_json();
  const auto &chain = j["add"]["chain"];
  CHECK(chain["family"] == "inet");
  CHECK(chain["table"] == "KeenPbrTable");
  CHECK(chain["name"] == "prerouting");
  CHECK(chain["hook"] == "prerouting");
  CHECK(chain["prio"] == -150);
  CHECK(chain["policy"] == "accept");
}

TEST_CASE("build_rule_add_commands: prefilter rules lead the prerouting chain") {
  auto cmds = T::build_rule_add_commands(
      prefilter_with_interfaces({"br0", "wg0"}),
      {mark_rule("myset", AF_INET, 256)});

  REQUIRE(cmds.is_array());
  REQUIRE(cmds.size() == 3);

  const auto &dnat_expr = cmds[0]["add"]["rule"]["expr"];
  CHECK(dnat_expr[0]["match"]["left"]["ct"]["key"] == "status");
  CHECK(dnat_expr[0]["match"]["right"] == "dnat");
  CHECK(dnat_expr[2].contains("accept"));

  const auto &iface_expr = cmds[1]["add"]["rule"]["expr"];
  CHECK(iface_expr[0]["match"]["op"] == "!=");
  CHECK(iface_expr[0]["match"]["left"]["meta"]["key"] == "iifname");
  CHECK(iface_expr[0]["match"]["right"]["set"][0] == "br0");
  CHECK(iface_expr[0]["match"]["right"]["set"][1] == "wg0");
  CHECK(iface_expr[2].contains("accept"));

  CHECK(cmds.dump().find("\"state\"") == std::string::npos);

  const auto &mark_expr = cmds[2]["add"]["rule"]["expr"];
  CHECK(mark_expr[0]["match"]["left"]["payload"]["protocol"] == "ip");
  CHECK(mark_expr[0]["match"]["right"] == "@myset");
  CHECK(mark_expr[2]["mangle"]["value"] == 256);
}

TEST_CASE("build_rule_add_commands: config-derived prefilter omits interface guard when inbound list is empty") {
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

  const auto cmds = T::build_rule_add_commands(
      build_firewall_global_prefilter(cfg),
      {mark_rule("myset", AF_INET, 256)});

  REQUIRE(cmds.is_array());
  REQUIRE(cmds.size() == 3);
  CHECK(cmds[0]["add"]["rule"]["expr"][0]["match"]["left"]["ct"]["key"] == "status");
  CHECK(cmds[1]["add"]["rule"]["expr"][0]["match"]["left"]["ct"]["key"] == "state");
  CHECK(cmds[2]["add"]["rule"]["expr"][0]["match"]["right"] == "@myset");
}

TEST_CASE("build_rule_add_commands: config-derived prefilter inserts interface guard before route rule") {
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

  const auto cmds = T::build_rule_add_commands(
      build_firewall_global_prefilter(cfg),
      {mark_rule("myset", AF_INET, 256)});

  REQUIRE(cmds.is_array());
  REQUIRE(cmds.size() == 4);
  CHECK(cmds[2]["add"]["rule"]["expr"][0]["match"]["left"]["meta"]["key"] == "iifname");
  CHECK(cmds[2]["add"]["rule"]["expr"][0]["match"]["right"] == "br0");
  CHECK(cmds[3]["add"]["rule"]["expr"][0]["match"]["right"] == "@myset");
}

// =============================================================================
// build_mark_rule_json tests
// =============================================================================

TEST_CASE("build_mark_rule_json: IPv4 mark rule") {
  auto j = T::build_mark_rule_json("myset", AF_INET, 256);
  const auto &expr = j["add"]["rule"]["expr"];

  CHECK(expr[0]["match"]["left"]["payload"]["protocol"] == "ip");
  CHECK(expr[0]["match"]["left"]["payload"]["field"] == "daddr");
  CHECK(expr[0]["match"]["right"] == "@myset");
  CHECK(expr[1].contains("counter"));
  CHECK(expr[2]["mangle"]["key"]["meta"]["key"] == "mark");
  CHECK(expr[2]["mangle"]["value"] == 256);
}

TEST_CASE("build_mark_rule_json: IPv6 mark rule") {
  auto j = T::build_mark_rule_json("v6set", AF_INET6, 512);
  CHECK(j["add"]["rule"]["expr"][0]["match"]["left"]["payload"]["protocol"] ==
        "ip6");
}

TEST_CASE("build_mark_rule_json: zero fwmark is valid") {
  auto j = T::build_mark_rule_json("zeroset", AF_INET, 0);
  CHECK(j["add"]["rule"]["expr"][2]["mangle"]["value"] == 0);
}

// =============================================================================
// build_drop_rule_json tests
// =============================================================================

TEST_CASE("build_drop_rule_json: IPv4 drop rule") {
  auto j = T::build_drop_rule_json("blacklist", AF_INET);
  const auto &expr = j["add"]["rule"]["expr"];

  CHECK(expr[0]["match"]["left"]["payload"]["protocol"] == "ip");

  bool has_drop = false, has_mangle = false;
  for (const auto &e : expr) {
    if (e.contains("drop"))
      has_drop = true;
    if (e.contains("mangle"))
      has_mangle = true;
  }
  CHECK(has_drop);
  CHECK_FALSE(has_mangle);
}

TEST_CASE("build_drop_rule_json: IPv6 drop rule") {
  auto j = T::build_drop_rule_json("v6bl", AF_INET6);
  CHECK(j["add"]["rule"]["expr"][0]["match"]["left"]["payload"]["protocol"] ==
        "ip6");

  bool has_drop = false;
  for (const auto &e : j["add"]["rule"]["expr"]) {
    if (e.contains("drop"))
      has_drop = true;
  }
  CHECK(has_drop);
}

TEST_CASE("build_pass_rule_json: IPv4 pass rule") {
  auto j = T::build_pass_rule_json("allowlist", AF_INET);
  const auto &expr = j["add"]["rule"]["expr"];

  bool has_accept = false, has_drop = false, has_mangle = false;
  for (const auto &e : expr) {
    if (e.contains("accept"))
      has_accept = true;
    if (e.contains("drop"))
      has_drop = true;
    if (e.contains("mangle"))
      has_mangle = true;
  }
  CHECK(has_accept);
  CHECK_FALSE(has_drop);
  CHECK_FALSE(has_mangle);
}

// =============================================================================
// build_elements_json tests
// =============================================================================

TEST_CASE("build_elements_json: correct set name, table, and elements") {
  nlohmann::json elems = nlohmann::json::array({
      "10.0.0.1",
      "10.0.0.2",
  });
  auto j = T::build_elements_json("myset", elems);
  const auto &elem = j["add"]["element"];
  CHECK(elem["family"] == "inet");
  CHECK(elem["table"] == "KeenPbrTable");
  CHECK(elem["name"] == "myset");
  CHECK(elem["elem"] == elems);
}

TEST_CASE("build_elements_json: IPv6 element is emitted as a plain string") {
  nlohmann::json elems = nlohmann::json::array({
      "2a00:1450:4010:c1e::8a",
  });

  auto j = T::build_elements_json("myset6", elems);
  const auto &elem = j["add"]["element"]["elem"][0];

  CHECK(elem == "2a00:1450:4010:c1e::8a");
}

TEST_CASE("NftBatchVisitor: CIDR element is emitted as nft prefix JSON") {
  nlohmann::json elems = nlohmann::json::array();
  NftBatchVisitor v(elems, "myset");

  v.on_entry(EntryType::Cidr, "10.0.0.0/24");

  REQUIRE(elems.size() == 1);
  REQUIRE(elems[0].contains("prefix"));
  CHECK(elems[0]["prefix"]["addr"] == "10.0.0.0");
  CHECK(elems[0]["prefix"]["len"] == 24);
}

TEST_CASE("NftBatchVisitor: IPv6 CIDR element is emitted as nft prefix JSON") {
  nlohmann::json elems = nlohmann::json::array();
  NftBatchVisitor v(elems, "myset6");

  v.on_entry(EntryType::Cidr, "2001:db8::/32");

  REQUIRE(elems.size() == 1);
  REQUIRE(elems[0].contains("prefix"));
  CHECK(elems[0]["prefix"]["addr"] == "2001:db8::");
  CHECK(elems[0]["prefix"]["len"] == 32);
}

TEST_CASE("NftBatchVisitor: plain IP element remains a string") {
  nlohmann::json elems = nlohmann::json::array();
  NftBatchVisitor v(elems, "myset");

  v.on_entry(EntryType::Ip, "10.0.1.3");

  REQUIRE(elems.size() == 1);
  CHECK(elems[0] == "10.0.1.3");
}

// =============================================================================
// Schema validity tests
// =============================================================================

TEST_CASE("full document structure: top-level key is nftables array") {
  nlohmann::json doc;
  auto &arr = doc["nftables"];
  arr = nlohmann::json::array();
  arr.push_back({{"metainfo", {{"json_schema_version", 1}}}});
  arr.push_back(T::build_table_json());
  arr.push_back(
      {{"flush",
        {{"table", {{"family", "inet"}, {"name", "KeenPbrTable"}}}}}});
  arr.push_back(T::build_chain_json());

  CHECK(doc.contains("nftables"));
  CHECK(doc["nftables"].is_array());
}

TEST_CASE("each command object has expected top-level key") {
  nlohmann::json meta = {{"metainfo", {{"json_schema_version", 1}}}};
  CHECK(meta.contains("metainfo"));

  auto add_table = T::build_table_json();
  CHECK(add_table.contains("add"));
  CHECK(add_table["add"].contains("table"));

  nlohmann::json flush = {
      {"flush", {{"table", {{"family", "inet"}, {"name", "KeenPbrTable"}}}}}};
  CHECK(flush.contains("flush"));
  CHECK(flush["flush"].contains("table"));

  auto chain = T::build_chain_json();
  CHECK(chain.contains("add"));
  CHECK(chain["add"].contains("chain"));

  auto set_j = T::build_set_json("s", "ipv4_addr", 0);
  CHECK(set_j.contains("add"));
  CHECK(set_j["add"].contains("set"));

  auto rule_j = T::build_mark_rule_json("s", AF_INET, 1);
  CHECK(rule_j.contains("add"));
  CHECK(rule_j["add"].contains("rule"));

  auto elem_j = T::build_elements_json("s", nlohmann::json::array({"1.2.3.4"}));
  CHECK(elem_j.contains("add"));
  CHECK(elem_j["add"].contains("element"));
}

// =============================================================================
// build_port_match_exprs tests
// =============================================================================

TEST_CASE("build_port_match_exprs: empty filter → empty array") {
  auto exprs = T::build_port_match_exprs("", "", "");
  CHECK(exprs.is_array());
  CHECK(exprs.empty());
}

TEST_CASE("build_port_match_exprs: proto only → l4proto match") {
  auto exprs = T::build_port_match_exprs("tcp", "", "");
  REQUIRE(exprs.size() == 1);
  CHECK(exprs[0]["match"]["left"]["meta"]["key"] == "l4proto");
  CHECK(exprs[0]["match"]["right"] == "tcp");
}

TEST_CASE("build_port_match_exprs: tcp + single dest_port → port integer") {
  auto exprs = T::build_port_match_exprs("tcp", "", "443");
  REQUIRE(exprs.size() == 2);
  CHECK(exprs[0]["match"]["left"]["meta"]["key"] == "l4proto");
  CHECK(exprs[1]["match"]["left"]["payload"]["field"] == "dport");
  CHECK(exprs[1]["match"]["right"] == 443);
}

TEST_CASE("build_port_match_exprs: udp + port range → range JSON") {
  auto exprs = T::build_port_match_exprs("udp", "", "8000-9000");
  REQUIRE(exprs.size() == 2);
  const auto &rhs = exprs[1]["match"]["right"];
  CHECK(rhs.contains("range"));
  CHECK(rhs["range"][0] == 8000);
  CHECK(rhs["range"][1] == 9000);
}

TEST_CASE("build_port_match_exprs: port list → set JSON") {
  auto exprs = T::build_port_match_exprs("tcp", "", "80,443");
  REQUIRE(exprs.size() == 2);
  const auto &rhs = exprs[1]["match"]["right"];
  CHECK(rhs.contains("set"));
  CHECK(rhs["set"][0] == 80);
  CHECK(rhs["set"][1] == 443);
}

TEST_CASE("build_port_match_exprs: src_port + dest_port → two port exprs") {
  auto exprs = T::build_port_match_exprs("tcp", "1024", "443");
  // l4proto + sport + dport
  REQUIRE(exprs.size() == 3);
  CHECK(exprs[1]["match"]["left"]["payload"]["field"] == "sport");
  CHECK(exprs[2]["match"]["left"]["payload"]["field"] == "dport");
}

// =============================================================================
// build_mark_rule_json with proto/port filter tests
// =============================================================================

TEST_CASE(
    "build_mark_rule_json: tcp + dest_port=443 → port match expr present") {
  ProtoPortFilter f;
  f.proto = "tcp";
  f.dst_port = "443";
  auto j = T::build_mark_rule_json("myset", AF_INET, 0x100, f);
  const auto &expr = j["add"]["rule"]["expr"];
  // expr[0]=daddr match, expr[1]=l4proto, expr[2]=dport, expr[3]=counter,
  // expr[4]=mangle
  bool has_dport = false;
  for (const auto &e : expr) {
    if (e.contains("match") && e["match"]["left"].contains("payload") &&
        e["match"]["left"]["payload"]["field"] == "dport") {
      has_dport = true;
    }
  }
  CHECK(has_dport);
}

TEST_CASE("build_mark_rule_json: no filter → no port exprs (regression)") {
  auto j = T::build_mark_rule_json("myset", AF_INET, 0x100);
  const auto &expr = j["add"]["rule"]["expr"];
  // Should be exactly 3: daddr match, counter, mangle
  CHECK(expr.size() == 3);
}

TEST_CASE("build_drop_rule_json: no filter → no port exprs (regression)") {
  auto j = T::build_drop_rule_json("bl", AF_INET);
  const auto &expr = j["add"]["rule"]["expr"];
  CHECK(expr.size() == 3);
}

// =============================================================================
// build_addr_match_exprs tests
// =============================================================================

TEST_CASE("build_addr_match_exprs: both empty → empty array") {
  auto exprs = T::build_addr_match_exprs("ip", {}, {});
  CHECK(exprs.is_array());
  CHECK(exprs.empty());
}

TEST_CASE("build_addr_match_exprs: single src_addr → saddr match with string") {
  auto exprs = T::build_addr_match_exprs("ip", {"192.168.10.0/24"}, {});
  REQUIRE(exprs.size() == 1);
  CHECK(exprs[0]["match"]["left"]["payload"]["field"] == "saddr");
  CHECK(exprs[0]["match"]["right"] == "192.168.10.0/24");
}

TEST_CASE("build_addr_match_exprs: multiple src_addr → saddr match with set") {
  auto exprs =
      T::build_addr_match_exprs("ip", {"192.168.1.0/24", "10.0.0.0/8"}, {});
  REQUIRE(exprs.size() == 1);
  CHECK(exprs[0]["match"]["left"]["payload"]["field"] == "saddr");
  CHECK(exprs[0]["match"]["right"].contains("set"));
  CHECK(exprs[0]["match"]["right"]["set"][0] == "192.168.1.0/24");
  CHECK(exprs[0]["match"]["right"]["set"][1] == "10.0.0.0/8");
}

TEST_CASE("build_addr_match_exprs: single dst_addr → daddr match with string") {
  auto exprs = T::build_addr_match_exprs("ip", {}, {"10.0.0.0/8"});
  REQUIRE(exprs.size() == 1);
  CHECK(exprs[0]["match"]["left"]["payload"]["field"] == "daddr");
  CHECK(exprs[0]["match"]["right"] == "10.0.0.0/8");
}

TEST_CASE("build_addr_match_exprs: src_addr + dst_addr → two match exprs") {
  auto exprs =
      T::build_addr_match_exprs("ip", {"192.168.1.0/24"}, {"8.8.8.0/24"});
  REQUIRE(exprs.size() == 2);
  CHECK(exprs[0]["match"]["left"]["payload"]["field"] == "saddr");
  CHECK(exprs[1]["match"]["left"]["payload"]["field"] == "daddr");
}

TEST_CASE("build_addr_match_exprs: ip6 family → ip6 protocol in payload") {
  auto exprs = T::build_addr_match_exprs("ip6", {"fd00::/8"}, {});
  REQUIRE(exprs.size() == 1);
  CHECK(exprs[0]["match"]["left"]["payload"]["protocol"] == "ip6");
}

// =============================================================================
// build_mark_rule_json with src_addr / dest_addr filter tests
// =============================================================================

TEST_CASE("build_mark_rule_json: src_addr → saddr expr present") {
  ProtoPortFilter f;
  f.src_addr = {"192.168.10.0/24"};
  auto j = T::build_mark_rule_json("myset", AF_INET, 0x100, f);
  const auto &expr = j["add"]["rule"]["expr"];
  // daddr @set, saddr match, counter, mangle = 4
  CHECK(expr.size() == 4);
  bool has_saddr = false;
  for (const auto &e : expr) {
    if (e.contains("match") && e["match"]["left"].contains("payload") &&
        e["match"]["left"]["payload"]["field"] == "saddr") {
      has_saddr = true;
      CHECK(e["match"]["right"] == "192.168.10.0/24");
    }
  }
  CHECK(has_saddr);
}

TEST_CASE(
    "build_mark_rule_json: multiple src_addr → set literal in saddr expr") {
  ProtoPortFilter f;
  f.src_addr = {"192.168.1.0/24", "10.0.0.0/8"};
  auto j = T::build_mark_rule_json("myset", AF_INET, 0x100, f);
  const auto &expr = j["add"]["rule"]["expr"];
  bool has_set_saddr = false;
  for (const auto &e : expr) {
    if (e.contains("match") && e["match"]["left"].contains("payload") &&
        e["match"]["left"]["payload"]["field"] == "saddr") {
      has_set_saddr = e["match"]["right"].contains("set");
    }
  }
  CHECK(has_set_saddr);
}

TEST_CASE(
    "build_mark_rule_json: no filter → still exactly 3 exprs (regression)") {
  auto j = T::build_mark_rule_json("myset", AF_INET, 0x100);
  const auto &expr = j["add"]["rule"]["expr"];
  CHECK(expr.size() == 3);
}

// =============================================================================
// build_port_match_exprs negation tests
// =============================================================================

TEST_CASE("build_port_match_exprs: negated dest_port → op !=") {
  auto exprs = T::build_port_match_exprs("tcp", "", "443", false, true);
  REQUIRE(exprs.size() == 2);
  CHECK(exprs[0]["match"]["op"] == "=="); // l4proto never negated
  CHECK(exprs[1]["match"]["op"] == "!=");
  CHECK(exprs[1]["match"]["left"]["payload"]["field"] == "dport");
}

TEST_CASE("build_port_match_exprs: negated src_port → op !=") {
  auto exprs = T::build_port_match_exprs("udp", "1024", "", true, false);
  REQUIRE(exprs.size() == 2);
  CHECK(exprs[1]["match"]["op"] == "!=");
  CHECK(exprs[1]["match"]["left"]["payload"]["field"] == "sport");
}

TEST_CASE("build_port_match_exprs: both negated → both !=") {
  auto exprs = T::build_port_match_exprs("tcp", "1024", "443", true, true);
  REQUIRE(exprs.size() == 3);
  CHECK(exprs[0]["match"]["op"] == "=="); // l4proto
  CHECK(exprs[1]["match"]["op"] == "!="); // sport
  CHECK(exprs[2]["match"]["op"] == "!="); // dport
}

TEST_CASE(
    "build_port_match_exprs: negated dst port range → range JSON with !=") {
  auto exprs = T::build_port_match_exprs("udp", "", "8000-9000", false, true);
  REQUIRE(exprs.size() == 2);
  CHECK(exprs[1]["match"]["op"] == "!=");
  const auto &rhs = exprs[1]["match"]["right"];
  REQUIRE(rhs.contains("range"));
  CHECK(rhs["range"][0] == 8000);
  CHECK(rhs["range"][1] == 9000);
}

TEST_CASE("build_port_match_exprs: negated dst port list → set JSON with !=") {
  auto exprs = T::build_port_match_exprs("tcp", "", "80,443", false, true);
  REQUIRE(exprs.size() == 2);
  CHECK(exprs[1]["match"]["op"] == "!=");
  const auto &rhs = exprs[1]["match"]["right"];
  REQUIRE(rhs.contains("set"));
  CHECK(rhs["set"][0] == 80);
  CHECK(rhs["set"][1] == 443);
}

TEST_CASE("build_port_match_exprs: negated src port list → set JSON with !=") {
  auto exprs = T::build_port_match_exprs("tcp", "80,8080", "", true, false);
  REQUIRE(exprs.size() == 2);
  CHECK(exprs[1]["match"]["op"] == "!=");
  CHECK(exprs[1]["match"]["left"]["payload"]["field"] == "sport");
  const auto &rhs = exprs[1]["match"]["right"];
  REQUIRE(rhs.contains("set"));
  CHECK(rhs["set"][0] == 80);
  CHECK(rhs["set"][1] == 8080);
}

TEST_CASE("build_port_match_exprs: no negation → both == (regression)") {
  auto exprs = T::build_port_match_exprs("tcp", "1024", "443");
  REQUIRE(exprs.size() == 3);
  for (const auto &e : exprs) {
    CHECK(e["match"]["op"] == "==");
  }
}

// =============================================================================
// build_addr_match_exprs negation tests
// =============================================================================

TEST_CASE("build_addr_match_exprs: negated single src_addr → op !=") {
  auto exprs =
      T::build_addr_match_exprs("ip", {"192.168.1.0/24"}, {}, true, false);
  REQUIRE(exprs.size() == 1);
  CHECK(exprs[0]["match"]["op"] == "!=");
  CHECK(exprs[0]["match"]["left"]["payload"]["field"] == "saddr");
  CHECK(exprs[0]["match"]["right"] == "192.168.1.0/24");
}

TEST_CASE("build_addr_match_exprs: negated dst_addr → op !=") {
  auto exprs = T::build_addr_match_exprs("ip", {}, {"10.0.0.0/8"}, false, true);
  REQUIRE(exprs.size() == 1);
  CHECK(exprs[0]["match"]["op"] == "!=");
  CHECK(exprs[0]["match"]["left"]["payload"]["field"] == "daddr");
}

TEST_CASE("build_addr_match_exprs: negated multiple src_addr → set literal "
          "with op != and correct CIDRs") {
  auto exprs = T::build_addr_match_exprs("ip", {"192.168.1.0/24", "10.0.0.0/8"},
                                         {}, true, false);
  REQUIRE(exprs.size() == 1);
  CHECK(exprs[0]["match"]["op"] == "!=");
  const auto &rhs = exprs[0]["match"]["right"];
  REQUIRE(rhs.contains("set"));
  CHECK(rhs["set"][0] == "192.168.1.0/24");
  CHECK(rhs["set"][1] == "10.0.0.0/8");
}

TEST_CASE("build_addr_match_exprs: negated multiple dst_addr → set literal "
          "with op !=") {
  auto exprs = T::build_addr_match_exprs("ip", {}, {"8.8.8.0/24", "1.1.1.0/24"},
                                         false, true);
  REQUIRE(exprs.size() == 1);
  CHECK(exprs[0]["match"]["op"] == "!=");
  CHECK(exprs[0]["match"]["left"]["payload"]["field"] == "daddr");
  const auto &rhs = exprs[0]["match"]["right"];
  REQUIRE(rhs.contains("set"));
  CHECK(rhs["set"][0] == "8.8.8.0/24");
  CHECK(rhs["set"][1] == "1.1.1.0/24");
}

TEST_CASE("build_addr_match_exprs: non-negated multiple src_addr stays == "
          "(regression)") {
  auto exprs =
      T::build_addr_match_exprs("ip", {"192.168.1.0/24", "10.0.0.0/8"}, {});
  REQUIRE(exprs.size() == 1);
  CHECK(exprs[0]["match"]["op"] == "==");
  const auto &rhs = exprs[0]["match"]["right"];
  REQUIRE(rhs.contains("set"));
  CHECK(rhs["set"][0] == "192.168.1.0/24");
  CHECK(rhs["set"][1] == "10.0.0.0/8");
}

TEST_CASE("build_addr_match_exprs: non-negated stays == (regression)") {
  auto exprs =
      T::build_addr_match_exprs("ip", {"192.168.1.0/24"}, {"8.8.8.0/24"});
  REQUIRE(exprs.size() == 2);
  CHECK(exprs[0]["match"]["op"] == "==");
  CHECK(exprs[1]["match"]["op"] == "==");
}

// =============================================================================
// build_mark_rule_json / build_drop_rule_json negation integration tests
// =============================================================================

TEST_CASE("build_mark_rule_json: negated src_addr → != in saddr expr") {
  ProtoPortFilter f;
  f.src_addr = {"192.168.1.0/24"};
  f.negate_src_addr = true;
  auto j = T::build_mark_rule_json("myset", AF_INET, 0x100, f);
  const auto &expr = j["add"]["rule"]["expr"];
  bool found = false;
  for (const auto &e : expr) {
    if (e.contains("match") && e["match"]["left"].contains("payload") &&
        e["match"]["left"]["payload"]["field"] == "saddr") {
      CHECK(e["match"]["op"] == "!=");
      found = true;
    }
  }
  CHECK(found);
}

TEST_CASE("build_mark_rule_json: negated dest_port → != in dport expr") {
  ProtoPortFilter f;
  f.proto = "tcp";
  f.dst_port = "443";
  f.negate_dst_port = true;
  auto j = T::build_mark_rule_json("myset", AF_INET, 0x100, f);
  const auto &expr = j["add"]["rule"]["expr"];
  bool found = false;
  for (const auto &e : expr) {
    if (e.contains("match") && e["match"]["left"].contains("payload") &&
        e["match"]["left"]["payload"]["field"] == "dport") {
      CHECK(e["match"]["op"] == "!=");
      found = true;
    }
  }
  CHECK(found);
}

TEST_CASE("build_mark_rule_json: multiple negated src_addrs → != with set "
          "containing both CIDRs") {
  ProtoPortFilter f;
  f.src_addr = {"192.168.1.0/24", "10.0.0.0/8"};
  f.negate_src_addr = true;
  auto j = T::build_mark_rule_json("myset", AF_INET, 0x100, f);
  const auto &expr = j["add"]["rule"]["expr"];
  bool found = false;
  for (const auto &e : expr) {
    if (e.contains("match") && e["match"]["left"].contains("payload") &&
        e["match"]["left"]["payload"]["field"] == "saddr") {
      CHECK(e["match"]["op"] == "!=");
      const auto &rhs = e["match"]["right"];
      REQUIRE(rhs.contains("set"));
      CHECK(rhs["set"][0] == "192.168.1.0/24");
      CHECK(rhs["set"][1] == "10.0.0.0/8");
      found = true;
    }
  }
  CHECK(found);
}

TEST_CASE(
    "build_mark_rule_json: negated dst port list → != with set {80,443}") {
  ProtoPortFilter f;
  f.proto = "tcp";
  f.dst_port = "80,443";
  f.negate_dst_port = true;
  auto j = T::build_mark_rule_json("myset", AF_INET, 0x100, f);
  const auto &expr = j["add"]["rule"]["expr"];
  bool found = false;
  for (const auto &e : expr) {
    if (e.contains("match") && e["match"]["left"].contains("payload") &&
        e["match"]["left"]["payload"]["field"] == "dport") {
      CHECK(e["match"]["op"] == "!=");
      const auto &rhs = e["match"]["right"];
      REQUIRE(rhs.contains("set"));
      CHECK(rhs["set"][0] == 80);
      CHECK(rhs["set"][1] == 443);
      found = true;
    }
  }
  CHECK(found);
}

TEST_CASE(
    "build_drop_rule_json: negated dst_addr → != in daddr constraint expr") {
  ProtoPortFilter f;
  f.dst_addr = {"10.0.0.0/8"};
  f.negate_dst_addr = true;
  auto j = T::build_drop_rule_json("bl", AF_INET, f);
  const auto &expr = j["add"]["rule"]["expr"];
  bool found = false;
  for (const auto &e : expr) {
    if (e.contains("match") && e["match"]["left"].contains("payload") &&
        e["match"]["left"]["payload"]["field"] == "daddr" &&
        e["match"]["right"] == "10.0.0.0/8") {
      CHECK(e["match"]["op"] == "!=");
      found = true;
    }
  }
  CHECK(found);
}

// =============================================================================
// Static / dynamic set split tests
// =============================================================================

TEST_CASE("nft static set naming: kpbr4_ prefix, no timeout") {
  auto j = T::build_set_json("kpbr4_mylist", "ipv4_addr", 0);
  const auto &set = j["add"]["set"];
  CHECK(set["name"] == "kpbr4_mylist");
  CHECK(set["flags"].is_array());
  CHECK(set["flags"][0] == "interval");
  CHECK_FALSE(set.contains("timeout"));
}

TEST_CASE("nft dynamic set naming: kpbr4d_ prefix, no timeout when ttl_ms=0") {
  auto j = T::build_set_json("kpbr4d_mylist", "ipv4_addr", 0);
  const auto &set = j["add"]["set"];
  CHECK(set["name"] == "kpbr4d_mylist");
  CHECK(set["flags"].is_array());
  CHECK(set["flags"][0] == "interval");
  CHECK_FALSE(set.contains("timeout"));
}

TEST_CASE("nft dynamic set naming: kpbr4d_ prefix, with timeout when ttl_ms set") {
  auto j = T::build_set_json("kpbr4d_mylist", "ipv4_addr", 3600);
  const auto &set = j["add"]["set"];
  CHECK(set["name"] == "kpbr4d_mylist");
  CHECK(set["flags"].is_array());
  CHECK(set["flags"][0] == "interval");
  CHECK(set["flags"][1] == "timeout");
  CHECK(set.contains("timeout"));
  CHECK(set["timeout"] == 3600);
}

TEST_CASE("nft dynamic set naming: kpbr6d_ IPv6 with timeout") {
  auto j = T::build_set_json("kpbr6d_mylist", "ipv6_addr", 86400);
  const auto &set = j["add"]["set"];
  CHECK(set["name"] == "kpbr6d_mylist");
  CHECK(set["flags"].is_array());
  CHECK(set["flags"][0] == "interval");
  CHECK(set["flags"][1] == "timeout");
  CHECK(set["timeout"] == 86400);
}

TEST_CASE("nft dual-set mark rules: both static and dynamic sets get mark rules") {
  auto j_static = T::build_mark_rule_json("kpbr4_mylist", AF_INET, 0x100);
  auto j_dynamic = T::build_mark_rule_json("kpbr4d_mylist", AF_INET, 0x100);
  CHECK(j_static["add"]["rule"]["expr"][0]["match"]["right"] == "@kpbr4_mylist");
  CHECK(j_dynamic["add"]["rule"]["expr"][0]["match"]["right"] == "@kpbr4d_mylist");
}

TEST_CASE("nft dual-set drop rules: both static and dynamic sets get drop rules") {
  auto j_static = T::build_drop_rule_json("kpbr4_mylist", AF_INET);
  auto j_dynamic = T::build_drop_rule_json("kpbr4d_mylist", AF_INET);
  CHECK(j_static["add"]["rule"]["expr"][0]["match"]["right"] == "@kpbr4_mylist");
  CHECK(j_dynamic["add"]["rule"]["expr"][0]["match"]["right"] == "@kpbr4d_mylist");
}

TEST_CASE("nft dual-set IPv6: kpbr6_ and kpbr6d_ both produce ip6 rules") {
  auto j_static = T::build_mark_rule_json("kpbr6_mylist", AF_INET6, 0x200);
  auto j_dynamic = T::build_mark_rule_json("kpbr6d_mylist", AF_INET6, 0x200);
  CHECK(j_static["add"]["rule"]["expr"][0]["match"]["left"]["payload"]["protocol"] == "ip6");
  CHECK(j_dynamic["add"]["rule"]["expr"][0]["match"]["left"]["payload"]["protocol"] == "ip6");
  CHECK(j_static["add"]["rule"]["expr"][0]["match"]["right"] == "@kpbr6_mylist");
  CHECK(j_dynamic["add"]["rule"]["expr"][0]["match"]["right"] == "@kpbr6d_mylist");
}

// =============================================================================
// build_mark_rule_json with direct=true (no named set match)
// =============================================================================

TEST_CASE("build_mark_rule_json: direct=true → no @set match in expr") {
  ProtoPortFilter f;
  f.proto = "udp";
  f.dst_port = "53";
  f.dst_addr = {"10.8.0.1"};
  auto j = T::build_mark_rule_json("", AF_INET, 0x10000, f, /*direct=*/true);
  const auto &expr = j["add"]["rule"]["expr"];
  // No expression should reference "@..."
  for (const auto &e : expr) {
    if (e.contains("match") && e["match"]["right"].is_string()) {
      CHECK(e["match"]["right"].get<std::string>().rfind('@', 0) != 0);
    }
  }
}

TEST_CASE("build_mark_rule_json: direct=true IPv4 UDP port 53 → daddr, l4proto, dport, counter, mangle") {
  ProtoPortFilter f;
  f.proto = "udp";
  f.dst_port = "53";
  f.dst_addr = {"10.8.0.1"};
  auto j = T::build_mark_rule_json("", AF_INET, 0x10000, f, /*direct=*/true);
  const auto &expr = j["add"]["rule"]["expr"];
  bool has_daddr = false, has_l4proto = false, has_dport = false, has_mangle = false;
  for (const auto &e : expr) {
    if (e.contains("match")) {
      const auto &left = e["match"]["left"];
      if (left.contains("payload") && left["payload"]["field"] == "daddr")
        has_daddr = true;
      if (left.contains("meta") && left["meta"]["key"] == "l4proto")
        has_l4proto = true;
      if (left.contains("payload") && left["payload"]["field"] == "dport")
        has_dport = true;
    }
    if (e.contains("mangle"))
      has_mangle = true;
  }
  CHECK(has_daddr);
  CHECK(has_l4proto);
  CHECK(has_dport);
  CHECK(has_mangle);
}

TEST_CASE("build_mark_rule_json: direct=true → daddr matches server IP") {
  ProtoPortFilter f;
  f.proto = "tcp";
  f.dst_port = "53";
  f.dst_addr = {"10.8.0.1"};
  auto j = T::build_mark_rule_json("", AF_INET, 0x10000, f, /*direct=*/true);
  const auto &expr = j["add"]["rule"]["expr"];
  bool found_ip = false;
  for (const auto &e : expr) {
    if (e.contains("match") && e["match"]["left"].contains("payload") &&
        e["match"]["left"]["payload"]["field"] == "daddr") {
      CHECK(e["match"]["right"] == "10.8.0.1");
      found_ip = true;
    }
  }
  CHECK(found_ip);
}

TEST_CASE("build_mark_rule_json: direct=false → first expr is @set match (regression)") {
  auto j = T::build_mark_rule_json("myset", AF_INET, 0x100);
  const auto &expr = j["add"]["rule"]["expr"];
  REQUIRE(!expr.empty());
  CHECK(expr[0]["match"]["right"] == "@myset");
}
