#include <doctest/doctest.h>

#include "../src/firewall/firewall_physical.hpp"
#include "../src/firewall/firewall_lowering.hpp"
#include "../src/firewall/firewall_plan.hpp"
#include "../src/firewall/nftables.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../src/firewall/iptables.hpp"
#include "firewall_fixtures.hpp"

namespace keen_pbr3 {
namespace {

// ---------------------------------------------------------------------------
// Fixture capture (manual).  Applies a representative plan through the real
// backend so that tests/firewall_it/scripts/capture_physical_fixtures.sh can
// dump the kernel state.  Does nothing unless KPBR_CAPTURE_SCENARIO is set;
// it must only ever run inside a throw-away network namespace.
// ---------------------------------------------------------------------------

TEST_CASE("physical fixture capture (manual, needs KPBR_CAPTURE_SCENARIO)") {
  const char *scenario = std::getenv("KPBR_CAPTURE_SCENARIO");
  if (scenario == nullptr) {
    return;
  }
  const std::string name = scenario;
  // *_intercept: the capture plan plus DNS hold and L7 sniff.  *_repair
  // re-applies in place (PreserveSets) so a drifted hook is repaired instead
  // of recreated; the plain scenarios are Destructive.
  const bool intercept = name == "nft_intercept" ||
                         name == "nft_intercept_repair" ||
                         name == "iptables_intercept" ||
                         name == "iptables_intercept_repair";
  const bool repair = name.size() > 7 &&
                      name.compare(name.size() - 7, 7, "_repair") == 0;
  const FirewallApplyMode apply_mode =
      repair ? FirewallApplyMode::PreserveSets : FirewallApplyMode::Destructive;
  if (name == "nftables" || name.rfind("nft_intercept", 0) == 0 ||
      name == "nft_plain_repair" || name == "nft_catchall" ||
      name == "nft_lan_output") {
    auto firewall = create_nftables_firewall();
    firewall->prepare_apply(apply_mode);
    firewall->apply(name == "nft_catchall"
                        ? capture_plan_catch_all()
                    : name == "nft_lan_output" ? capture_plan_lan_output()
                    : intercept ? capture_plan_with_intercept(true, true, true)
                                : capture_plan(true, true, true),
                    apply_mode);
    // The nft backend deletes its table on destruction; the dump needs it.
    (void)firewall.release();
    return;
  }
  RawPreroutingMode mode;
  mode.ipv4 = mode.ipv6 = name == "iptables_raw";
  IptablesFirewall firewall;
  // nft-backed iptables has no /proc/net/ip*_tables_*; force the capabilities
  // that legacy iptables reports.
  firewall.override_capabilities_for_fixtures(true, mode);
  firewall.prepare_apply(apply_mode);
  firewall.apply(
      name == "iptables_catchall" ? capture_plan_catch_all()
      : name == "iptables_lan_output" ? capture_plan_lan_output()
      : intercept ? capture_plan_with_intercept(false, false, true)
                : capture_plan(false, name != "iptables_mangle" &&
                                          name != "iptables_plain_repair",
                               true),
      apply_mode);
}


// ---------------------------------------------------------------------------
// Parser tests.  Fixtures under tests/firewall_it/fixtures/physical are REAL
// kernel dumps (see capture_physical_fixtures.sh and the README there).
// ---------------------------------------------------------------------------

using Role = PhysicalChainRole;
using Table = PhysicalTable;
using Fam = FirewallFamily;

PhysicalChainId ipt_id(Role role, Table table, Fam family,
                       const std::string &name = {}) {
  PhysicalChainId id;
  id.role = role;
  id.table = table;
  id.family = family;
  id.name = name;
  return id;
}

PhysicalChainId nft_id(Role role, uint32_t mark = 0) {
  PhysicalChainId id;
  id.role = role;
  id.table = Table::nft_inet;
  id.family = Fam::any;
  id.setter_mark = mark;
  return id;
}

PhysicalChainId setter(uint32_t mark) { return nft_id(Role::nft_setter, mark); }

PhysicalRule make_rule(Fam family, std::vector<PhysicalMatch> matches,
                       std::vector<PhysicalStatement> statements) {
  PhysicalRule rule;
  rule.family = family;
  rule.matches = std::move(matches);
  rule.statements = std::move(statements);
  return rule;
}

const PhysicalChain &require_chain(const PhysicalRuleset &set,
                                   const PhysicalChainId &id) {
  const PhysicalChain *chain = set.find(id);
  REQUIRE_MESSAGE(chain != nullptr, "chain missing: " << id.name);
  return *chain;
}

std::size_t count_lines_with_prefix(const std::string &text,
                                    const std::string &prefix) {
  std::size_t count = 0;
  std::size_t pos = 0;
  while (pos < text.size()) {
    const auto end = text.find('\n', pos);
    const auto line = text.substr(pos, end == std::string::npos
                                           ? std::string::npos
                                           : end - pos);
    if (line.rfind(prefix, 0) == 0) ++count;
    if (end == std::string::npos) break;
    pos = end + 1;
  }
  return count;
}

bool has_unknown(const PhysicalRule &rule) {
  for (const auto &match : rule.matches) {
    if (std::holds_alternative<UnknownMatch>(match)) return true;
  }
  for (const auto &statement : rule.statements) {
    if (std::holds_alternative<UnknownStmt>(statement)) return true;
  }
  return false;
}

constexpr uint32_t kMask = 0x00FF0000u;

TEST_CASE("physical: iptables mangle IPv4 dump is parsed rule for rule") {
  const std::string text = read_fixture("iptables_mangle_v4.save");
  const auto set = parse_iptables_save(text, Fam::ipv4);

  const auto a = ipt_id(Role::iptables_prerouting, Table::mangle, Fam::ipv4);
  const auto &chain_a = require_chain(set, a);
  CHECK(chain_a.rules.size() ==
        count_lines_with_prefix(text, "-A KeenPbrTable "));
  CHECK(chain_a.rules.size() == 28);
  for (const auto &rule : chain_a.rules) {
    CHECK_FALSE(has_unknown(rule));
    CHECK(rule.key.has_value());
  }

  SUBCASE("hook rules jump straight into the classification chains") {
    const auto output =
        ipt_id(Role::iptables_output, Table::mangle, Fam::ipv4);
    const auto &pre = require_chain(
        set, ipt_id(Role::system_prerouting, Table::mangle, Fam::ipv4));
    REQUIRE(pre.rules.size() == 1);
    CHECK(pre.rules[0] == make_rule(Fam::ipv4, {}, {JumpStmt{a, false}}));
    const auto &out = require_chain(
        set, ipt_id(Role::system_output, Table::mangle, Fam::ipv4));
    REQUIRE(out.rules.size() == 1);
    CHECK(out.rules[0] == make_rule(Fam::ipv4, {}, {JumpStmt{output, false}}));
    // No dispatcher rule and no A/B chains: the chains hold the rules.
    for (const auto &chain : set.chains) {
      CHECK_MESSAGE(chain.id.role != Role::other_owned,
                    "unexpected owned chain " << chain.id.name);
    }
  }

  SUBCASE("OUTPUT chain: DNS-detour rules only here, no inbound-interface guard") {
    const auto &out = require_chain(
        set, ipt_id(Role::iptables_output, Table::mangle, Fam::ipv4));
    CHECK(out.rules.size() ==
          count_lines_with_prefix(text, "-A KeenPbrOutput "));
    bool dynamic_set = false;
    for (const auto &rule : out.rules) {
      for (const auto &match : rule.matches) {
        CHECK(std::get_if<IifMatch>(&match) == nullptr);
        if (const auto *set_match = std::get_if<SetMatch>(&match)) {
          dynamic_set = dynamic_set || set_match->name == "kpbr4d_routed";
        }
      }
    }
    CHECK(dynamic_set);
    for (const auto &rule : chain_a.rules) {
      for (const auto &match : rule.matches) {
        if (const auto *set_match = std::get_if<SetMatch>(&match)) {
          CHECK(set_match->name != "kpbr4d_routed");
        }
      }
    }
  }

  SUBCASE("prefilter rules (mask omission, --mask spelled nfmask/ctmask)") {
    // `-m connmark ! --mark 0x0/0xff0000 ... -j CONNMARK --restore-mark
    //  --nfmask 0xff0000 --ctmask 0xff0000`
    CHECK(chain_a.rules[0] ==
          make_rule(Fam::ipv4,
                    {MarkMatch{PhysicalMarkKind::conntrack, kMask, true, {0}},
                     CtDirMatch{true}},
                    {CopyMarkStmt{false, kMask, kMask}}));
    CHECK(chain_a.rules[0].key->module_id ==
          "prefilter.restore_conntrack_mark");
    CHECK(chain_a.rules[1] ==
          make_rule(Fam::ipv4,
                    {MarkMatch{PhysicalMarkKind::packet, kMask, true, {0}},
                     CtDirMatch{true}},
                    {VerdictStmt{PhysicalVerdict::return_}}));
    // Forwarded replies are never classified: `-m conntrack --ctdir REPLY`.
    CHECK(chain_a.rules[2] ==
          make_rule(Fam::ipv4, {CtDirMatch{false}},
                    {VerdictStmt{PhysicalVerdict::return_}}));
    CHECK(chain_a.rules[2].key->module_id == "prefilter.skip_local_replies");
    CHECK(chain_a.rules[3] ==
          make_rule(Fam::ipv4, {CtStateMatch{ct_dnat, false}},
                    {VerdictStmt{PhysicalVerdict::return_}}));
    // `-m mark ! --mark 0x0`: iptables-save drops the /0xffffffff mask.
    CHECK(chain_a.rules[4] ==
          make_rule(Fam::ipv4,
                    {MarkMatch{PhysicalMarkKind::packet, 0xFFFFFFFFu, true, {0}}},
                    {VerdictStmt{PhysicalVerdict::accept}}));
    CHECK(chain_a.rules[5] ==
          make_rule(Fam::ipv4, {IifMatch{true, {"lan0"}}},
                    {VerdictStmt{PhysicalVerdict::return_}}));
  }

  SUBCASE("route rules: kernel match order is canonicalized") {
    // `-p tcp -m set --match-set kpbr4_hybrid dst -m tcp --dport 443`
    const PhysicalRule mark = make_rule(
        Fam::ipv4,
        {SetMatch{"kpbr4_hybrid", PhysicalDir::dst, false},
         ProtoMatch{L4Proto::Tcp},
         PortMatch{PhysicalTransport::tcp, PhysicalDir::dst, false, {{443, 443}}}},
        {SetMarkStmt{PhysicalMarkKind::packet, 0x10000u, kMask}});
    CHECK(chain_a.rules[6] == mark);
    CHECK(chain_a.rules[6].key->module_id == "route.mark");
    // The CONNMARK --save-mark and RETURN rules follow in order.
    CHECK(chain_a.rules[7].statements ==
          std::vector<PhysicalStatement>{CopyMarkStmt{true, kMask, kMask}});
    CHECK(chain_a.rules[8].statements ==
          std::vector<PhysicalStatement>{
              VerdictStmt{PhysicalVerdict::return_}});
    CHECK(chain_a.rules[7].matches == chain_a.rules[6].matches);
    // `! -s 10.0.0.0/8 ... -m udp --dport 53 -j DROP`
    CHECK(chain_a.rules[9] ==
          make_rule(Fam::ipv4,
                    {SetMatch{"kpbr4_hybrid", PhysicalDir::dst, false},
                     AddrMatch{PhysicalDir::src, true, {"10.0.0.0/8"}},
                     ProtoMatch{L4Proto::Udp},
                     PortMatch{PhysicalTransport::udp, PhysicalDir::dst, false,
                               {{53, 53}}}},
                    {VerdictStmt{PhysicalVerdict::drop}}));
    // `-d 8.8.8.8/32 -p udp -m udp --dport 53 -j RETURN`
    CHECK(chain_a.rules[10].matches[0] ==
          PhysicalMatch{AddrMatch{PhysicalDir::dst, false, {"8.8.8.8/32"}}});
  }

  SUBCASE("dscp is printed in hex, multiport lists keep their ranges") {
    const PhysicalRule dscp = make_rule(
        Fam::ipv4,
        {ProtoMatch{L4Proto::Tcp},
         PortMatch{PhysicalTransport::tcp, PhysicalDir::dst, false,
                   {{80, 80}, {443, 443}, {8000, 9000}}},
         DscpMatch{0x2e}},
        {SetMarkStmt{PhysicalMarkKind::packet, 0x10000u, kMask}});
    bool found = false;
    for (const auto &rule : chain_a.rules) found = found || rule == dscp;
    CHECK(found);
  }

  SUBCASE("sport range, negated dport, physical set names are kept") {
    bool negated_dport = false;
    for (const auto &rule : chain_a.rules) {
      for (const auto &match : rule.matches) {
        if (const auto *port = std::get_if<PortMatch>(&match)) {
          negated_dport = negated_dport || (port->negate && port->ranges.size() == 1 &&
                                            port->ranges[0].from == 443);
        }
      }
    }
    CHECK(negated_dport);
  }
}

TEST_CASE("physical: iptables IPv6 dump") {
  const auto set =
      parse_iptables_save(read_fixture("iptables_mangle_v6.save"), Fam::ipv6);
  const auto &chain = require_chain(
      set, ipt_id(Role::iptables_prerouting, Table::mangle, Fam::ipv6));
  bool found = false;
  for (const auto &rule : chain.rules) {
    CHECK(rule.family == Fam::ipv6);
    for (const auto &match : rule.matches) {
      if (const auto *addr = std::get_if<AddrMatch>(&match)) {
        found = found || addr->cidrs == std::vector<std::string>{
                                            "2001:db8:53::53/128"};
      }
    }
  }
  CHECK(found);
  // The chain of the other family does not exist in this ruleset.
  CHECK(set.find(ipt_id(Role::iptables_prerouting, Table::mangle, Fam::ipv4)) == nullptr);
}

TEST_CASE("physical: iptables raw PREROUTING layout (raw + mangle OUTPUT)") {
  const std::string text = read_fixture("iptables_raw_v4.save");
  const auto set = parse_iptables_save(text, Fam::ipv4);

  const auto raw_a =
      ipt_id(Role::iptables_prerouting, Table::raw, Fam::ipv4);
  const auto out_a =
      ipt_id(Role::iptables_output, Table::mangle, Fam::ipv4);

  const auto &raw_chain = require_chain(set, raw_a);
  CHECK(raw_chain.rules.size() ==
        count_lines_with_prefix(text, "-A KeenPbrRaw "));
  // Multi-interface inbound allowlist: one `-i` fragment per interface.
  bool lan0 = false;
  bool guest = false;
  for (const auto &rule : raw_chain.rules) {
    for (const auto &match : rule.matches) {
      if (const auto *iif = std::get_if<IifMatch>(&match)) {
        CHECK_FALSE(iif->negate);
        lan0 = lan0 || iif->names == std::vector<std::string>{"lan0"};
        guest = guest || iif->names == std::vector<std::string>{"br-guest"};
      }
    }
  }
  CHECK(lan0);
  CHECK(guest);
  // raw has no conntrack prefilter: only skip-marked comes before the routes.
  CHECK(raw_chain.rules[0].matches ==
        std::vector<PhysicalMatch>{
            MarkMatch{PhysicalMarkKind::packet, 0xFFFFFFFFu, true, {0}}});

  const auto &out_chain = require_chain(set, out_a);
  CHECK(out_chain.rules.size() ==
        count_lines_with_prefix(text, "-A KeenPbrOutput "));
  CHECK(out_chain.rules[0].statements ==
        std::vector<PhysicalStatement>{CopyMarkStmt{false, kMask, kMask}});

  // Hooks: raw PREROUTING and mangle OUTPUT.
  const auto &pre = require_chain(
      set, ipt_id(Role::system_prerouting, Table::raw, Fam::ipv4));
  CHECK(pre.rules ==
        std::vector<PhysicalRule>{
            make_rule(Fam::ipv4, {}, {JumpStmt{raw_a, false}})});
  const auto &out = require_chain(
      set, ipt_id(Role::system_output, Table::mangle, Fam::ipv4));
  CHECK(out.rules ==
        std::vector<PhysicalRule>{
            make_rule(Fam::ipv4, {}, {JumpStmt{out_a, false}})});
  // The OUTPUT chain does not carry the inbound-interface guard.
  for (const auto &rule : out_chain.rules) {
    for (const auto &match : rule.matches) {
      CHECK(std::get_if<IifMatch>(&match) == nullptr);
    }
  }
}

TEST_CASE("physical: iptables foreign rules, unknown matches, stray hooks") {
  const std::string text = read_fixture("iptables_mangle_v4_foreign.save");
  const auto set = parse_iptables_save(text, Fam::ipv4);
  const auto a = ipt_id(Role::iptables_prerouting, Table::mangle, Fam::ipv4);
  const auto &chain = require_chain(set, a);
  const std::size_t total = count_lines_with_prefix(text, "-A KeenPbrTable ");
  REQUIRE(chain.rules.size() == total);

  SUBCASE("foreign rule inside an owned chain is captured, without a key") {
    const PhysicalRule foreign = make_rule(
        Fam::ipv4, {AddrMatch{PhysicalDir::src, false, {"203.0.113.9/32"}}},
        {VerdictStmt{PhysicalVerdict::accept}});
    const auto it = std::find(chain.rules.begin(), chain.rules.end(), foreign);
    REQUIRE(it != chain.rules.end());
    CHECK_FALSE(it->key.has_value());
    // It stayed at its place: after the prefilters, before nothing of ours
    // being reordered (the dump appended it at the end of the chain).
    CHECK(&*it == &chain.rules.back() - 1);
  }

  SUBCASE("unknown extension match and unknown target are explicit") {
    // `-m limit --limit 1/sec` inserted at position 1
    REQUIRE(chain.rules.size() > 2);
    const PhysicalRule limit = chain.rules.front();
    REQUIRE(limit.matches.size() == 2);
    CHECK(std::get<UnknownMatch>(limit.matches[0]).text == "-m limit");
    CHECK(std::get<UnknownMatch>(limit.matches[1]).text == "--limit 1/sec");
    CHECK(limit.statements ==
          std::vector<PhysicalStatement>{VerdictStmt{PhysicalVerdict::return_}});
    // Unknown never compares equal, not even to itself.
    CHECK(limit != limit);

    const PhysicalRule &log = chain.rules.back();
    REQUIRE(log.statements.size() == 1);
    CHECK(std::get<UnknownStmt>(log.statements[0]).text == "-j LOG");
    CHECK(std::get<OifMatch>(log.matches.at(0)) == OifMatch{false, {"eth9"}});
    // Comment of a foreign rule is not ours -> no key (and no exception).
    CHECK_FALSE(log.key.has_value());
  }

  SUBCASE("only hook rules into keen-pbr chains are kept in system chains") {
    const auto &pre = require_chain(
        set, ipt_id(Role::system_prerouting, Table::mangle, Fam::ipv4));
    // `-A PREROUTING -j KeenPbrTable` (real hook) and a later
    // `-j KeenPbrOutput` (misplaced extra hook).  `-i eth0 -j ACCEPT` is
    // foreign and dropped.
    REQUIRE(pre.rules.size() == 2);
    CHECK(std::get<JumpStmt>(pre.rules[0].statements[0]).target.role ==
          Role::iptables_prerouting);
    CHECK(std::get<JumpStmt>(pre.rules[1].statements[0]).target.role ==
          Role::iptables_output);
  }
}

TEST_CASE("physical: `iptables -S` output parses to the same ruleset") {
  // `-S` has `-N`/`-P` instead of `:chain` lines and no `*table` header.
  const auto saved =
      parse_iptables_save(read_fixture("iptables_mangle_v4.save"), Fam::ipv4);
  const auto listed = parse_iptables_save(
      read_fixture("iptables_mangle_v4.rules"), Fam::ipv4, Table::mangle);
  CHECK(saved == listed);
  CHECK(saved.chains.size() == 4);
  // Same for a dump containing unknown rules (which never compare equal).
  const auto foreign_saved = parse_iptables_save(
      read_fixture("iptables_mangle_v4_foreign.save"), Fam::ipv4);
  const auto foreign_listed = parse_iptables_save(
      read_fixture("iptables_mangle_v4_foreign.rules"), Fam::ipv4);
  REQUIRE(foreign_saved.chains.size() == foreign_listed.chains.size());
  for (std::size_t c = 0; c < foreign_saved.chains.size(); ++c) {
    CHECK(foreign_saved.chains[c].id == foreign_listed.chains[c].id);
    CHECK(foreign_saved.chains[c].rules.size() ==
          foreign_listed.chains[c].rules.size());
  }
}

TEST_CASE("physical: iptables spelling zoo (real iptables-save output)") {
  const auto v4 =
      parse_iptables_save(read_fixture("iptables_misc_v4.save"), Fam::ipv4);
  const auto a = ipt_id(Role::iptables_prerouting, Table::mangle, Fam::ipv4);
  const auto &chain = require_chain(v4, a);
  const auto &r = chain.rules;
  REQUIRE(r.size() == 20);

  // `--set-mark 0x10` is printed as `--set-xmark 0x10/0xffffffff`.
  CHECK(r[0] == make_rule(Fam::ipv4, {},
                          {SetMarkStmt{PhysicalMarkKind::packet, 0x10, 0xFFFFFFFFu}}));
  CHECK(r[1] == make_rule(Fam::ipv4, {},
                          {SetMarkStmt{PhysicalMarkKind::packet, 0x10, 0xFF}}));
  // `--mark 0x5` (value only) means mask 0xffffffff.
  CHECK(r[3] == make_rule(Fam::ipv4,
                          {MarkMatch{PhysicalMarkKind::packet, 0xFFFFFFFFu, false, {5}}},
                          {VerdictStmt{PhysicalVerdict::accept}}));
  // `--dscp-class AF11` and `--dscp 10` are both printed `--dscp 0x0a`.
  CHECK(r[4] == r[5]);
  CHECK(r[4].matches ==
        std::vector<PhysicalMatch>{DscpMatch{10}});
  // `--dport 80:80` is printed `--dport 80`; implicit `-m tcp` adds nothing.
  CHECK(r[6].matches ==
        std::vector<PhysicalMatch>{
            ProtoMatch{L4Proto::Tcp},
            PortMatch{PhysicalTransport::tcp, PhysicalDir::dst, false, {{80, 80}}}});
  // negated multiport takes the transport from `-p udp`.
  CHECK(r[7].matches ==
        std::vector<PhysicalMatch>{
            ProtoMatch{L4Proto::Udp},
            PortMatch{PhysicalTransport::udp, PhysicalDir::dst, true,
                      {{53, 53}, {80, 90}}}});
  // `--mask` is printed `--nfmask X --ctmask X`; distinct masks stay distinct.
  CHECK(r[8].statements ==
        std::vector<PhysicalStatement>{CopyMarkStmt{false, kMask, kMask}});
  CHECK(r[9].statements ==
        std::vector<PhysicalStatement>{CopyMarkStmt{true, 0xFF, 0xF}});

  SUBCASE("comments: only well-formed v1 keys become keys") {
    CHECK_FALSE(r[10].key.has_value()); // "kpbr:v1:a.b:c d" (space)
    REQUIRE(r[11].key.has_value());
    CHECK(r[11].key->module_id == "route.mark");
    CHECK(r[11].key->instance_id == "abc");
    CHECK_FALSE(r[12].key.has_value()); // unknown version v9
    // Keyless / malformed rules are still owned and fully parsed.
    CHECK(r[10].statements ==
          std::vector<PhysicalStatement>{VerdictStmt{PhysicalVerdict::return_}});
    CHECK(r[10].matches == std::vector<PhysicalMatch>{ProtoMatch{L4Proto::Udp}});
    // Equality ignores the key.
    PhysicalRule keyless = r[11];
    keyless.key.reset();
    CHECK(keyless == r[11]);
  }

  SUBCASE("goto, `-f`, unknown module, address canonicalization") {
    const auto b = ipt_id(Role::iptables_output, Table::mangle, Fam::ipv4);
    CHECK(r[15] == make_rule(Fam::ipv4, {}, {JumpStmt{b, true}}));
    CHECK(has_unknown(r[16])); // -f
    CHECK(std::get<UnknownMatch>(r[16].matches[0]).text == "-f");
    // `! -d 10.1.2.3/8` -> `! -d 10.0.0.0/8`, `-s 1.2.3.4` -> /32.
    CHECK(r[17].matches ==
          std::vector<PhysicalMatch>{
              AddrMatch{PhysicalDir::src, false, {"1.2.3.4/32"}},
              AddrMatch{PhysicalDir::dst, true, {"10.0.0.0/8"}}});
  }

  SUBCASE("stray hooks in system chains, duplicates preserved") {
    const auto &pre = require_chain(
        v4, ipt_id(Role::system_prerouting, Table::mangle, Fam::ipv4));
    CHECK(pre.rules.size() == 2); // duplicate hook visible
    CHECK(pre.rules[0] == pre.rules[1]);
    const auto &input = require_chain(
        v4, [] {
          auto id = ipt_id(Role::system_other, Table::mangle, Fam::ipv4);
          id.name = "INPUT";
          return id;
        }());
    CHECK(input.rules.size() == 1);
  }

  SUBCASE("IPv6 prefixes are masked and compressed") {
    const auto v6 =
        parse_iptables_save(read_fixture("iptables_misc_v6.save"), Fam::ipv6);
    const auto &chain6 = require_chain(
        v6, ipt_id(Role::iptables_prerouting, Table::mangle, Fam::ipv6));
    const auto &last = chain6.rules.back();
    CHECK(last.matches[0] ==
          PhysicalMatch{AddrMatch{PhysicalDir::src, false, {"2001:db8::1/128"}}});
    CHECK(last.matches[1] ==
          PhysicalMatch{AddrMatch{PhysicalDir::dst, false, {"2001:db8::/64"}}});
  }
}

TEST_CASE("physical: iptables parser edge cases") {
  const std::string text =
      "# Generated by iptables-save\n"
      "*nat\n"
      ":KeenPbrTable - [0:0]\n"
      "-A KeenPbrTable -j ACCEPT\n"
      "COMMIT\n"
      "*mangle\n"
      ":PREROUTING ACCEPT [0:0]\n"
      ":KeenPbrTable - [0:0]\n"
      ":KeenPbrWeird - [0:0]\n"
      ":UserChain - [0:0]\n"
      "[5:300] -A KeenPbrTable -m comment --comment \"kpbr:v1:a:b\" -j DROP\n"
      "-A KeenPbrTable -j UserChain\n"
      "-A KeenPbrTable\n"
      "-A KeenPbrTable -j MARK --set-xmark 0x1/0x0\n"
      "-A KeenPbrTable -p icmp -j ACCEPT\n"
      "-A KeenPbrTable -m comment --comment \"say \\\"hi\\\"\" -j ACCEPT\n"
      "-A KeenPbrWeird -j ACCEPT\n"
      "-A UserChain -j KeenPbrTable\n"
      "-A PREROUTING -m comment --comment kpbr:v1:hook:1 -j KeenPbrTable\n"
      "COMMIT\n";
  const auto set = parse_iptables_save(text, Fam::ipv4);
  const auto a = ipt_id(Role::iptables_prerouting, Table::mangle, Fam::ipv4);
  const auto &chain = require_chain(set, a);
  REQUIRE(chain.rules.size() == 6);
  // `*nat` is not examined; counters prefix is accepted; the key is read.
  CHECK(chain.rules[0] == make_rule(Fam::ipv4, {}, {VerdictStmt{PhysicalVerdict::drop}}));
  REQUIRE(chain.rules[0].key.has_value());
  CHECK(chain.rules[0].key->module_id == "a");
  // A jump to a foreign chain is not a hook into ours -> explicit unknown.
  CHECK(std::get<UnknownStmt>(chain.rules[1].statements[0]).text == "-j UserChain");
  // A rule without a target (counting rule) is valid and empty.
  CHECK(chain.rules[2] == make_rule(Fam::ipv4, {}, {}));
  // xor of bits outside the mask is not an OR-style mark.
  CHECK(has_unknown(chain.rules[3]));
  CHECK(std::get<UnknownMatch>(chain.rules[4].matches[0]).text == "-p icmp");
  // Escaped quotes in a comment do not break tokenization.
  CHECK(chain.rules[5].statements ==
        std::vector<PhysicalStatement>{VerdictStmt{PhysicalVerdict::accept}});
  CHECK_FALSE(chain.rules[5].key.has_value());

  // Unrecognized KeenPbr* chain names are still owned; user chains are not.
  PhysicalChainId weird;
  weird.role = Role::other_owned;
  weird.table = Table::mangle;
  weird.family = Fam::ipv4;
  weird.name = "KeenPbrWeird";
  CHECK(require_chain(set, weird).rules.size() == 1);
  PhysicalChainId other = weird;
  other.name = "KeenPbrOther";
  CHECK(set.find(other) == nullptr);
  CHECK(set.chains.size() == 3); // A, Weird, PREROUTING hook
  CHECK(require_chain(set, ipt_id(Role::system_prerouting, Table::mangle,
                                  Fam::ipv4)).rules.size() == 1);

  // Empty owned chains are reported (declared but no rules).
  const auto empty = parse_iptables_save(
      "*mangle\n:KeenPbrTable - [0:0]\nCOMMIT\n", Fam::ipv4);
  REQUIRE(empty.chains.size() == 1);
  CHECK(empty.chains[0].rules.empty());
  CHECK(parse_iptables_save("", Fam::ipv4).chains.empty());
}

TEST_CASE("physical: nft balance dump (restore vmap, setter chains, numgen)") {
  const std::string text = read_fixture("nft_balance.json");
  const auto set = parse_nft_json(text);
  const auto pre = nft_id(Role::nft_prerouting);
  const auto out = nft_id(Role::nft_output);

  SUBCASE("base chains") {
    const auto &p = require_chain(set, pre);
    REQUIRE(p.base.has_value());
    CHECK(*p.base == PhysicalBaseChain{PhysicalBaseChain::Type::filter,
                                       PhysicalBaseChain::Hook::prerouting,
                                       -150, true});
    const auto &o = require_chain(set, out);
    REQUIRE(o.base.has_value());
    CHECK(*o.base == PhysicalBaseChain{PhysicalBaseChain::Type::route,
                                       PhysicalBaseChain::Hook::output, -150,
                                       true});
    CHECK(set.chains.size() == 5);
  }

  SUBCASE("setter chains: libnftables rewrites the mark expression") {
    // Emitted: (mark & ~0xff0000) | 0x10000.  Printed back:
    // (mark & 0xff01ffff) | 0x10000.  Both mean "replace the mask bits".
    for (const uint32_t mark : {0x10000u, 0x20000u, 0x40000u}) {
      const auto &chain = require_chain(set, setter(mark));
      REQUIRE(chain.rules.size() == 1);
      CHECK_FALSE(chain.base.has_value());
      CHECK(chain.rules[0] ==
            make_rule(Fam::any, {},
                      {SetMarkStmt{PhysicalMarkKind::packet, mark, kMask},
                       SetMarkStmt{PhysicalMarkKind::conntrack, mark, kMask},
                       VerdictStmt{PhysicalVerdict::accept}}));
    }
  }

  const auto &p = require_chain(set, pre);
  REQUIRE(p.rules.size() == 19);
  for (const auto &rule : p.rules) {
    CHECK_FALSE(has_unknown(rule));
    CHECK(rule.key.has_value());
  }

  SUBCASE("ct restore: vmap by ct mark, late re-check, order preserved") {
    VmapStmt vmap;
    vmap.key = PhysicalVmapKey::conntrack_mark_and;
    vmap.param = kMask;
    vmap.entries = {{0x10000u, setter(0x10000u)},
                    {0x20000u, setter(0x20000u)},
                    {0x40000u, setter(0x40000u)}};
    CHECK(p.rules[0] ==
          make_rule(Fam::any,
                    {MarkMatch{PhysicalMarkKind::conntrack, kMask, true, {0}},
                     CtDirMatch{true}},
                    {vmap,
                     LateMatchStmt{MarkMatch{PhysicalMarkKind::conntrack, kMask,
                                             false,
                                             {0x10000u, 0x20000u, 0x40000u}}},
                     VerdictStmt{PhysicalVerdict::accept}}));
    CHECK(p.rules[0].key->module_id == "prefilter.restore_conntrack_mark");
  }

  SUBCASE("prefilters") {
    // `ct status dnat` (op "in"), `meta mark != 0`, counters dropped.
    CHECK(p.rules[2] ==
          make_rule(Fam::any, {CtStateMatch{ct_dnat, false}},
                    {VerdictStmt{PhysicalVerdict::accept}}));
    CHECK(p.rules[3] ==
          make_rule(Fam::any,
                    {MarkMatch{PhysicalMarkKind::packet, 0xFFFFFFFFu, true, {0}}},
                    {VerdictStmt{PhysicalVerdict::accept}}));
    // iifname set is listed sorted although emitted as lan0, br-guest.
    CHECK(p.rules[4] ==
          make_rule(Fam::any, {IifMatch{true, {"br-guest", "lan0"}}},
                    {VerdictStmt{PhysicalVerdict::accept}}));
  }

  SUBCASE("route rules") {
    // family comes from the `ip` / `ip6` payload protocol; nft drops the
    // redundant `meta l4proto tcp`, the parser adds it back canonically.
    CHECK(p.rules[5] ==
          make_rule(Fam::ipv4,
                    {SetMatch{"kpbr4_hybrid", PhysicalDir::dst, false},
                     ProtoMatch{L4Proto::Tcp},
                     PortMatch{PhysicalTransport::tcp, PhysicalDir::dst, false,
                               {{443, 443}}}},
                    {JumpStmt{setter(0x10000u), false}}));
    CHECK(p.rules[6].family == Fam::ipv6);
    // single address is a bare string (no /len): canonical /128.
    CHECK(p.rules[11].matches[0] ==
          PhysicalMatch{AddrMatch{PhysicalDir::dst, false, {"2001:db8:53::53/128"}}});
    // `!= 10.0.0.0/8` prefix object.
    CHECK(p.rules[7].matches[1] ==
          PhysicalMatch{AddrMatch{PhysicalDir::src, true, {"10.0.0.0/8"}}});
    // address set; both entries are single addresses.
    CHECK(p.rules[8].matches[0] ==
          PhysicalMatch{AddrMatch{PhysicalDir::dst, false,
                                  {"8.8.8.8/32", "9.9.9.9/32"}}});
    // `th sport 1111`: no protocol, so no ProtoMatch.
    CHECK(p.rules[9] ==
          make_rule(Fam::any,
                    {PortMatch{PhysicalTransport::any, PhysicalDir::src, false,
                               {{1111, 1111}}}},
                    {JumpStmt{setter(0x20000u), false}}));
    // `tcp dport != 443`
    CHECK(p.rules[10].matches[2] ==
          PhysicalMatch{PortMatch{PhysicalTransport::tcp, PhysicalDir::dst, true,
                                  {{443, 443}}}});
    // `ip dscp ef` -> 46 and port set with range.
    CHECK(p.rules[12] ==
          make_rule(Fam::ipv4,
                    {ProtoMatch{L4Proto::Tcp},
                     PortMatch{PhysicalTransport::tcp, PhysicalDir::dst, false,
                               {{80, 80}, {443, 443}, {8000, 9000}}},
                     DscpMatch{46}},
                    {JumpStmt{setter(0x10000u), false}}));
    // `ip dscp af11` -> 10
    CHECK(std::get<DscpMatch>(p.rules[13].matches[1]).value == 10);
    // sport range
    CHECK(std::get<PortMatch>(p.rules[14].matches[1]).ranges ==
          std::vector<PortRange>{{1024, 65535}});
    // default-gateway bypass is a negated address list, sorted
    CHECK(p.rules[15].matches ==
          std::vector<PhysicalMatch>{
              AddrMatch{PhysicalDir::dst, true, {"10.0.0.0/8", "192.168.0.0/16"}}});
    CHECK(p.rules[16].family == Fam::ipv6);
  }

  SUBCASE("balance: numgen inc vmap after a mark guard") {
    VmapStmt vmap;
    vmap.key = PhysicalVmapKey::numgen_inc;
    vmap.param = 2;
    vmap.entries = {{0, setter(0x10000u)}, {1, setter(0x20000u)}};
    CHECK(p.rules[17] ==
          make_rule(Fam::ipv4,
                    {SetMatch{"kpbr4_hybrid", PhysicalDir::dst, false},
                     ProtoMatch{L4Proto::Udp},
                     MarkMatch{PhysicalMarkKind::packet, kMask, false, {0}}},
                    {vmap, VerdictStmt{PhysicalVerdict::accept}}));
    // `meta nfproto ipv6` is the family guard, not a match.
    const auto &v6 = p.rules[18];
    CHECK(v6.family == Fam::ipv6);
    CHECK(v6.matches ==
          std::vector<PhysicalMatch>{
              ProtoMatch{L4Proto::Tcp},
              PortMatch{PhysicalTransport::tcp, PhysicalDir::dst, false,
                        {{8443, 8443}}},
              MarkMatch{PhysicalMarkKind::packet, kMask, false, {0}}});
    const auto &v6vmap = std::get<VmapStmt>(v6.statements[0]);
    CHECK(v6vmap.param == 3);
    CHECK(v6vmap.entries.size() == 3);
    CHECK(v6vmap.entries[2].second == setter(0x40000u));
  }

  SUBCASE("output chain mirrors prerouting except inbound filter and output-only rules") {
    const auto &p = require_chain(set, pre);
    const auto &o = require_chain(set, out);
    REQUIRE(p.rules.size() == 19);
    REQUIRE(o.rules.size() == 20);
    // Prerouting rule 4 is the inbound-interface filter, which never applies
    // to router-originated traffic; output rules 14 and 15 come from plan
    // rules with hook=output.  The reply skip (rule 1) is in both chains.
    std::vector<PhysicalRule> shared;
    for (std::size_t i = 0; i < p.rules.size(); ++i) {
      if (i != 4) shared.push_back(p.rules[i]);
    }
    std::vector<PhysicalRule> output_shared;
    for (std::size_t i = 0; i < o.rules.size(); ++i) {
      if (i != 14 && i != 15) output_shared.push_back(o.rules[i]);
    }
    CHECK(output_shared == shared);
    CHECK(std::holds_alternative<IifMatch>(p.rules[4].matches.front()));
    CHECK(std::get<SetMatch>(o.rules[14].matches.front()).name == "kpbr4d_routed");
    CHECK(std::get<SetMatch>(o.rules[15].matches.front()).name == "kpbr4_hybrid");
  }
}

TEST_CASE("physical: nft foreign and unknown expressions") {
  const auto set = parse_nft_json(read_fixture("nft_foreign.json"));
  const auto &p = require_chain(set, nft_id(Role::nft_prerouting));
  REQUIRE(p.rules.size() == 22);
  // The three rules appended with the stock nft tool sit at the end.
  const auto &foreign = p.rules[19];
  CHECK(foreign == make_rule(Fam::ipv4,
                             {AddrMatch{PhysicalDir::src, false, {"203.0.113.9/32"}}},
                             {VerdictStmt{PhysicalVerdict::accept}}));
  CHECK_FALSE(foreign.key.has_value());
  // `limit rate 1/second` is not understood: explicit unknown statement.
  REQUIRE(p.rules[20].statements.size() == 2);
  CHECK(std::holds_alternative<UnknownStmt>(p.rules[20].statements[0]));
  CHECK(p.rules[20] != p.rules[20]);
  // Malformed comment -> keyless, still parsed.
  CHECK(p.rules[21].family == Fam::ipv4);
  CHECK_FALSE(p.rules[21].key.has_value());
  CHECK(p.rules[21].statements ==
        std::vector<PhysicalStatement>{VerdictStmt{PhysicalVerdict::drop}});
}

TEST_CASE("physical: nft spelling zoo (real nft -j output)") {
  const auto set = parse_nft_json(read_fixture("nft_misc.json"));
  const auto &p = require_chain(set, nft_id(Role::nft_prerouting));
  const auto &r = p.rules;
  REQUIRE(r.size() == 22);
  // Adjacent/overlapping ranges are merged and sorted by the kernel.
  CHECK(r[0].matches ==
        std::vector<PhysicalMatch>{
            ProtoMatch{L4Proto::Tcp},
            PortMatch{PhysicalTransport::tcp, PhysicalDir::dst, false,
                      {{22, 25}, {80, 81}, {443, 443}, {8000, 9500}}}});
  // Address sets are sorted and covered prefixes (10.1/16 in 10/8) merged.
  CHECK(r[1].matches ==
        std::vector<PhysicalMatch>{AddrMatch{
            PhysicalDir::dst, false, {"8.8.8.8/32", "9.9.9.9/32", "10.0.0.0/8"}}});
  CHECK(r[2].matches ==
        std::vector<PhysicalMatch>{AddrMatch{PhysicalDir::dst, false, {"1.2.3.4/32"}}});
  // `ip saddr 10.1.2.3/8` is stored as 10.0.0.0/8.
  CHECK(r[3].matches ==
        std::vector<PhysicalMatch>{AddrMatch{PhysicalDir::src, false, {"10.0.0.0/8"}}});
  CHECK(r[4].matches ==
        std::vector<PhysicalMatch>{AddrMatch{PhysicalDir::dst, false, {"2001:db8::/64"}}});
  CHECK(r[4].family == Fam::ipv6);
  // dscp: names (ef, cs1) and numbers (0x2e typed in hex, 7).
  CHECK(r[5].matches == std::vector<PhysicalMatch>{DscpMatch{46}});
  CHECK(r[6].matches == std::vector<PhysicalMatch>{DscpMatch{7}});
  CHECK(r[7].matches == std::vector<PhysicalMatch>{DscpMatch{8}});
  // `meta mark set 5` (plain) and the masked form.
  CHECK(r[8].statements[0] ==
        PhysicalStatement{SetMarkStmt{PhysicalMarkKind::packet, 5, 0xFFFFFFFFu}});
  CHECK(r[9].statements[0] ==
        PhysicalStatement{SetMarkStmt{PhysicalMarkKind::packet, 0x10000u, kMask}});
  // Interface set listed sorted and deduplicated.
  CHECK(r[10].matches ==
        std::vector<PhysicalMatch>{IifMatch{false, {"br0", "lan0"}}});
  CHECK(r[11].matches == std::vector<PhysicalMatch>{CtStateMatch{
                             static_cast<uint8_t>(ct_established | ct_related), false}});
  CHECK(r[12].matches == std::vector<PhysicalMatch>{CtStateMatch{ct_dnat, false}});
  CHECK(r[14].matches ==
        std::vector<PhysicalMatch>{
            MarkMatch{PhysicalMarkKind::packet, 0xFF, false, {0x10}}});
  CHECK(r[15].matches ==
        std::vector<PhysicalMatch>{
            ProtoMatch{L4Proto::Tcp},
            PortMatch{PhysicalTransport::tcp, PhysicalDir::dst, true,
                      {{80, 80}, {443, 443}}}});
  // Unknown shapes are explicit: l4proto set, concatenation, limit.
  CHECK(has_unknown(r[16]));
  CHECK(has_unknown(r[17]));
  CHECK(has_unknown(r[18]));
  // jump / goto / return.
  CHECK(r[19].matches ==
        std::vector<PhysicalMatch>{SetMatch{"kpbr4_x", PhysicalDir::dst, false}});
  CHECK(r[19].statements ==
        std::vector<PhysicalStatement>{JumpStmt{setter(0x10), false}});
  CHECK(r[20].statements ==
        std::vector<PhysicalStatement>{JumpStmt{setter(0x10), true}});
  CHECK(r[21].statements ==
        std::vector<PhysicalStatement>{VerdictStmt{PhysicalVerdict::return_}});
  // Empty owned chain is still reported.
  CHECK(require_chain(set, setter(0x10)).rules.empty());
}

TEST_CASE("physical: nft parser robustness") {
  CHECK_THROWS_AS(parse_nft_json("not json"), FirewallError);
  CHECK_THROWS_AS(parse_nft_json("{}"), FirewallError);
  // Absent table (or other tables only) yields an empty ruleset.
  CHECK(parse_nft_json(R"({"nftables":[{"metainfo":{}}]})").chains.empty());
  CHECK(parse_nft_json(
            R"({"nftables":[{"chain":{"family":"inet","table":"other","name":"prerouting"}},
                {"rule":{"family":"inet","table":"other","chain":"prerouting","expr":[{"accept":null}]}}]})")
            .chains.empty());
  // A rule in a foreign table is not ours, but one in ours is, even without
  // comment; set element arrays are skipped without being stored.
  const auto set = parse_nft_json(R"({"nftables":[
      {"set":{"family":"inet","table":"KeenPbrTable","name":"s","elem":[1,2,3]}},
      {"chain":{"family":"inet","table":"KeenPbrTable","name":"prerouting"}},
      {"rule":{"family":"inet","table":"KeenPbrTable","chain":"prerouting",
               "expr":[{"counter":{"packets":1,"bytes":2}},{"accept":null}]}},
      {"rule":{"family":"inet","table":"KeenPbrTable","chain":"mystery",
               "comment":"kpbr:v1:x:y","expr":[{"frobnicate":{}},{"drop":null}]}}
    ]})");
  REQUIRE(set.chains.size() == 2);
  CHECK(set.chains[0].rules[0] ==
        make_rule(Fam::any, {}, {VerdictStmt{PhysicalVerdict::accept}}));
  CHECK_FALSE(set.chains[0].rules[0].key.has_value());
  PhysicalChainId mystery = nft_id(Role::other_owned);
  mystery.name = "mystery";
  const auto &m = require_chain(set, mystery);
  REQUIRE(m.rules.size() == 1);
  CHECK(has_unknown(m.rules[0]));
  CHECK(m.rules[0].key.has_value());
  // Conflicting L3 families are flagged, never merged.
  const auto conflict = parse_nft_json(R"({"nftables":[
      {"rule":{"family":"inet","table":"KeenPbrTable","chain":"prerouting","expr":[
        {"match":{"op":"==","left":{"meta":{"key":"nfproto"}},"right":"ipv6"}},
        {"match":{"op":"==","left":{"payload":{"protocol":"ip","field":"daddr"}},"right":"1.2.3.4"}},
        {"accept":null}]}}]})");
  CHECK(has_unknown(conflict.chains[0].rules[0]));
}

// ---------------------------------------------------------------------------
// Interception constructs: NFQUEUE / NFLOG / connbytes / ctdir, pinned hooks.
// ---------------------------------------------------------------------------

namespace {

// Rules of one owned iptables chain parsed from `-A` lines.
std::vector<PhysicalRule> parse_sniff_rules(const std::string &lines) {
  const auto set = parse_iptables_save(
      "*mangle\n:KeenPbrSniff - [0:0]\n" + lines + "COMMIT\n", Fam::ipv4);
  return require_chain(set, ipt_id(Role::iptables_sniff, Table::mangle, Fam::ipv4))
      .rules;
}

PhysicalRule parse_sniff_rule(const std::string &rule) {
  const auto rules = parse_sniff_rules("-A KeenPbrSniff " + rule + "\n");
  REQUIRE(rules.size() == 1);
  return rules.front();
}

// The rule of a one-rule nft document.
PhysicalRule parse_nft_expr(const std::string &exprs,
                            const std::string &chain = "sniff_fwd") {
  const auto set = parse_nft_json(
      R"({"nftables":[{"rule":{"family":"inet","table":"KeenPbrTable","chain":")" +
      chain + R"(","expr":)" + exprs + "}}]}");
  REQUIRE(set.chains.size() == 1);
  REQUIRE(set.chains[0].rules.size() == 1);
  return set.chains[0].rules[0];
}

} // namespace

TEST_CASE("physical: iptables connbytes, NFQUEUE, NFLOG and ctdir") {
  SUBCASE("the exact kernel spelling of the emitted rules") {
    const auto rule = parse_sniff_rule(
        "-p tcp -m multiport --dports 80,443 -m connbytes --connbytes 1:6 "
        "--connbytes-mode packets --connbytes-dir original -j NFLOG "
        "--nflog-group 9054 --nflog-size 2048");
    CHECK(rule == make_rule(
                      Fam::ipv4,
                      {ProtoMatch{L4Proto::Tcp},
                       PortMatch{PhysicalTransport::tcp, PhysicalDir::dst, false,
                                 {{80, 80}, {443, 443}}},
                       ConnbytesMatch{ConnbytesDir::original,
                                      ConnbytesMode::packets, 1, 6}},
                      {LogStmt{9054, 2048, 1}}));
    // The port list is canonical whatever its spelling.
    CHECK(rule == parse_sniff_rule(
                      "-p tcp -m multiport --dports 443,80 -m connbytes "
                      "--connbytes 1:6 --connbytes-mode packets "
                      "--connbytes-dir original -j NFLOG --nflog-group 9054 "
                      "--nflog-size 2048"));
  }
  SUBCASE("direction and mode variants") {
    const auto both = parse_sniff_rule(
        "-m connbytes --connbytes 0:9 --connbytes-mode bytes "
        "--connbytes-dir both -j NFLOG --nflog-group 1");
    REQUIRE(both.matches.size() == 1);
    CHECK(std::get<ConnbytesMatch>(both.matches[0]) ==
          ConnbytesMatch{ConnbytesDir::both, ConnbytesMode::bytes, 0, 9});
    CHECK(std::get<LogStmt>(both.statements[0]) == LogStmt{1, 0, 1});
    const auto reply = parse_sniff_rule(
        "-m connbytes --connbytes 2:3 --connbytes-mode packets "
        "--connbytes-dir reply -j RETURN");
    CHECK(std::get<ConnbytesMatch>(reply.matches[0]).dir == ConnbytesDir::reply);
  }
  SUBCASE("a partial or malformed connbytes is never trusted") {
    for (const char *text : {
             "-m connbytes --connbytes 1:6 -j RETURN",
             "-m connbytes --connbytes 1:6 --connbytes-mode packets -j RETURN",
             "-m connbytes --connbytes 1:6 --connbytes-dir original -j RETURN",
             "-m connbytes --connbytes 6:1 --connbytes-mode packets "
             "--connbytes-dir original -j RETURN",
             "-m connbytes --connbytes 6 --connbytes-mode packets "
             "--connbytes-dir original -j RETURN",
             "-m connbytes --connbytes x:6 --connbytes-mode packets "
             "--connbytes-dir original -j RETURN",
             "-m connbytes --connbytes 1:6 --connbytes-mode avgpkt "
             "--connbytes-dir original -j RETURN",
             "-m connbytes --connbytes 1:6 --connbytes-mode packets "
             "--connbytes-dir sideways -j RETURN",
             "-m connbytes ! --connbytes 1:6 --connbytes-mode packets "
             "--connbytes-dir original -j RETURN",
             "-m connbytes --connbytes-dir original --connbytes-mode packets "
             "--connbytes 1:6 -j RETURN",
             "-m connbytes --connbytes-dir original -j RETURN"}) {
      CAPTURE(text);
      const auto rule = parse_sniff_rule(text);
      CHECK(has_unknown(rule));
      for (const auto &match : rule.matches) {
        CHECK_FALSE(std::holds_alternative<ConnbytesMatch>(match));
      }
    }
  }
  SUBCASE("a repeated --connbytes is reported") {
    CHECK(has_unknown(parse_sniff_rule(
        "-m connbytes --connbytes 1:6 --connbytes 2:3 --connbytes-mode "
        "packets --connbytes-dir original -j RETURN")));
  }
  SUBCASE("NFQUEUE") {
    const auto rule = parse_sniff_rule(
        "-p udp -m udp --sport 53 -m conntrack --ctstate ESTABLISHED --ctdir "
        "REPLY -j NFQUEUE --queue-num 9053 --queue-bypass");
    CHECK(rule == make_rule(Fam::ipv4,
                            {ProtoMatch{L4Proto::Udp},
                             PortMatch{PhysicalTransport::udp, PhysicalDir::src,
                                       false, {{53, 53}}},
                             CtStateMatch{ct_established, false},
                             CtDirMatch{false}},
                            {QueueStmt{9053, true}}));
    CHECK(parse_sniff_rule("-j NFQUEUE --queue-num 3").statements[0] ==
          PhysicalStatement{QueueStmt{3, false}});
    // The kernel prints the number even for the default queue.
    CHECK(parse_sniff_rule("-j NFQUEUE --queue-num 0 --queue-bypass")
              .statements[0] == PhysicalStatement{QueueStmt{0, true}});
    for (const char *text : {"-j NFQUEUE", "-j NFQUEUE --queue-balance 3:5",
                             "-j NFQUEUE --queue-num 70000",
                             "-j NFQUEUE --queue-num x",
                             "-j NFQUEUE --queue-num 3 --queue-cpu-fanout",
                             "-j NFQUEUE --queue-num",
                             "-g NFQUEUE"}) {
      CAPTURE(text);
      CHECK(std::holds_alternative<UnknownStmt>(
          parse_sniff_rule(text).statements.back()));
    }
  }
  SUBCASE("NFLOG") {
    CHECK(parse_sniff_rule("-j NFLOG --nflog-group 5 --nflog-size 100")
              .statements[0] == PhysicalStatement{LogStmt{5, 100, 1}});
    CHECK(parse_sniff_rule("-j NFLOG --nflog-group 5 --nflog-threshold 7")
              .statements[0] == PhysicalStatement{LogStmt{5, 0, 7}});
    // An explicit default threshold is the same rule (iptables-nft prints it).
    CHECK(parse_sniff_rule("-j NFLOG --nflog-group 5 --nflog-threshold 1")
              .statements[0] == PhysicalStatement{LogStmt{5, 0, 1}});
    // group 0 is the bare `-j NFLOG`.
    CHECK(parse_sniff_rule("-j NFLOG").statements[0] ==
          PhysicalStatement{LogStmt{0, 0, 1}});
    for (const char *text : {"-j NFLOG --nflog-group 5 --nflog-prefix hi",
                             "-j NFLOG --nflog-group 70000",
                             "-j NFLOG --nflog-group 5 --nflog-size 70000",
                             "-j NFLOG --nflog-group 5 --nflog-threshold 0",
                             "-j NFLOG --nflog-group",
                             "-j NFLOG --bogus 1"}) {
      CAPTURE(text);
      CHECK(std::holds_alternative<UnknownStmt>(
          parse_sniff_rule(text).statements.back()));
    }
  }
  SUBCASE("conntrack options keep their meaning") {
    const auto rule = parse_sniff_rule(
        "-m conntrack --ctdir REPLY --ctstate NEW,ESTABLISHED -j RETURN");
    CHECK(rule.matches == std::vector<PhysicalMatch>{
                              CtStateMatch{ct_new | ct_established, false},
                              CtDirMatch{false}});
    CHECK(has_unknown(parse_sniff_rule("-m conntrack ! --ctdir REPLY -j RETURN")));
    CHECK(has_unknown(parse_sniff_rule("-m conntrack --ctdir SIDEWAYS -j RETURN")));
  }
}

TEST_CASE("physical: iptables hook position among all builtin rules") {
  const auto parse = [](const std::string &mangle) {
    return parse_iptables_save("*mangle\n" + mangle + "COMMIT\n", Fam::ipv4);
  };
  const auto forward_id = ipt_id(Role::system_other, Table::mangle, Fam::ipv4,
                                 "FORWARD");
  const auto position = [](const PhysicalChain &chain, std::size_t index) {
    return chain.rules.at(index).hook_position;
  };
  SUBCASE("first rule, foreign rules before and after") {
    const auto set = parse(
        "-A FORWARD -j KeenPbrSniff\n-A FORWARD -p icmp -j ACCEPT\n");
    const auto &chain = require_chain(set, forward_id);
    REQUIRE(chain.rules.size() == 1);
    CHECK(position(chain, 0) == std::optional<uint32_t>{0});
    CHECK(chain.rules[0] ==
          [&] {
            auto rule = make_rule(
                Fam::ipv4, {},
                {JumpStmt{ipt_id(Role::iptables_sniff, Table::mangle, Fam::ipv4,
                                 "KeenPbrSniff"),
                          false}});
            rule.hook_position = 0;
            return rule;
          }());
  }
  SUBCASE("foreign rules in front move the position") {
    const auto set = parse(
        "-A FORWARD -p icmp -j ACCEPT\n-A FORWARD -j DROP\n"
        "-A FORWARD -j KeenPbrSniff\n");
    const auto &chain = require_chain(set, forward_id);
    REQUIRE(chain.rules.size() == 1);
    CHECK(position(chain, 0) == std::optional<uint32_t>{2});
  }
  SUBCASE("a duplicate jump is kept with both positions") {
    const auto set = parse(
        "-A FORWARD -j KeenPbrSniff\n-A FORWARD -j ACCEPT\n"
        "-A FORWARD -j KeenPbrSniff\n");
    const auto &chain = require_chain(set, forward_id);
    REQUIRE(chain.rules.size() == 2);
    CHECK(position(chain, 0) == std::optional<uint32_t>{0});
    CHECK(position(chain, 1) == std::optional<uint32_t>{2});
  }
  SUBCASE("only pinned jumps carry a position; chains are counted apart") {
    const auto set = parse(
        "-A PREROUTING -p tcp -j ACCEPT\n-A PREROUTING -j KeenPbrTable\n"
        "-A OUTPUT -o eth9 -j ACCEPT\n-A OUTPUT -j KeenPbrSniff\n"
        "-A OUTPUT -j KeenPbrOutput\n-A POSTROUTING -j KeenPbrDnsHold\n");
    const auto &pre = require_chain(
        set, ipt_id(Role::system_prerouting, Table::mangle, Fam::ipv4));
    CHECK_FALSE(position(pre, 0).has_value());
    const auto &out = require_chain(
        set, ipt_id(Role::system_output, Table::mangle, Fam::ipv4));
    REQUIRE(out.rules.size() == 2);
    CHECK(position(out, 0) == std::optional<uint32_t>{1});
    CHECK_FALSE(position(out, 1).has_value());
    CHECK(position(require_chain(set, ipt_id(Role::system_other, Table::mangle,
                                             Fam::ipv4, "POSTROUTING")),
                   0) == std::optional<uint32_t>{0});
  }
  SUBCASE("the iptables -S spelling counts the same way") {
    const auto set = parse_iptables_save(
        "-P FORWARD ACCEPT\n-N KeenPbrSniff\n-A FORWARD -p icmp -j ACCEPT\n"
        "-A FORWARD -j KeenPbrSniff\n",
        Fam::ipv4);
    CHECK(position(require_chain(set, forward_id), 0) ==
          std::optional<uint32_t>{1});
  }
  SUBCASE("the real dump with foreign rules in front") {
    const auto set = parse_iptables_save(
        read_fixture("iptables_intercept_mangle_v4_foreign.rules"), Fam::ipv4);
    CHECK(position(require_chain(set, forward_id), 0) ==
          std::optional<uint32_t>{1});
    const auto &out = require_chain(
        set, ipt_id(Role::system_output, Table::mangle, Fam::ipv4));
    REQUIRE(out.rules.size() == 2);
    CHECK(position(out, 0) == std::optional<uint32_t>{1});
    const auto &post = require_chain(
        set, ipt_id(Role::system_other, Table::mangle, Fam::ipv4, "POSTROUTING"));
    REQUIRE(post.rules.size() == 1);
    CHECK(position(post, 0) == std::optional<uint32_t>{1});
  }
}

TEST_CASE("physical: nft queue, log, ct counters and new base chain hooks") {
  using nlohmann::json;
  const std::string port =
      R"({"match":{"op":"==","left":{"payload":{"protocol":"tcp","field":"dport"}},"right":443}})";
  SUBCASE("queue") {
    const auto rule = parse_nft_expr(
        R"([{"counter":{"packets":0,"bytes":0}},{"queue":{"num":9053,"flags":["bypass"]}}])",
        "dns_hold");
    CHECK(rule == make_rule(Fam::any, {}, {QueueStmt{9053, true}}));
    CHECK(parse_nft_expr(R"([{"queue":{"num":4}}])").statements[0] ==
          PhysicalStatement{QueueStmt{4, false}});
    for (const char *text :
         {R"([{"queue":{"num":{"range":[3,5]}}}])",
          R"([{"queue":{"num":3,"flags":["bypass","fanout"]}}])",
          R"([{"queue":{"num":3,"flags":["fanout"]}}])",
          R"([{"queue":{"num":3,"flags":"bypass"}}])",
          R"([{"queue":{"num":70000}}])", R"([{"queue":{}}])",
          R"([{"queue":{"flags":["bypass"]}}])",
          R"([{"queue":{"num":3,"extra":1}}])", R"([{"queue":5}])"}) {
      CAPTURE(text);
      CHECK(std::holds_alternative<UnknownStmt>(
          parse_nft_expr(text).statements.back()));
    }
  }
  SUBCASE("log") {
    CHECK(parse_nft_expr(R"([{"log":{"group":9054,"snaplen":2048}}])")
              .statements[0] == PhysicalStatement{LogStmt{9054, 2048, 1}});
    CHECK(parse_nft_expr(R"([{"log":{"group":1}}])").statements[0] ==
          PhysicalStatement{LogStmt{1, 0, 1}});
    CHECK(parse_nft_expr(
              R"([{"log":{"group":1,"snaplen":10,"queue-threshold":7}}])")
              .statements[0] == PhysicalStatement{LogStmt{1, 10, 7}});
    for (const char *text :
         {R"([{"log":{"prefix":"x","group":1}}])", R"([{"log":null}])",
          R"([{"log":{"level":"warn"}}])", R"([{"log":{"snaplen":10}}])",
          R"([{"log":{"group":70000}}])", R"([{"log":{"group":1,"snaplen":70000}}])",
          R"([{"log":{"group":1,"queue-threshold":0}}])",
          R"([{"log":{"group":1,"flags":["all"]}}])",
          R"([{"log":{"group":"x"}}])"}) {
      CAPTURE(text);
      CHECK(std::holds_alternative<UnknownStmt>(
          parse_nft_expr(text).statements.back()));
    }
  }
  SUBCASE("ct packets / bytes ranges") {
    const auto ct = [&](const std::string &left, const std::string &right,
                        const char *op = "==") {
      return parse_nft_expr(
          "[" + port + R"(,{"match":{"op":")" + op + R"(","left":)" + left +
          R"(,"right":)" + right + R"(}},{"accept":null}])");
    };
    const auto last_match = [](const PhysicalRule &rule) {
      return rule.matches.back();
    };
    const auto original =
        R"({"ct":{"key":"packets","dir":"original"}})";
    CHECK(last_match(ct(original, R"({"range":[1,6]})")) ==
          PhysicalMatch{ConnbytesMatch{ConnbytesDir::original,
                                       ConnbytesMode::packets, 1, 6}});
    CHECK(last_match(ct(original, "3")) ==
          PhysicalMatch{ConnbytesMatch{ConnbytesDir::original,
                                       ConnbytesMode::packets, 3, 3}});
    CHECK(last_match(ct(R"({"ct":{"key":"packets"}})", "3")) ==
          PhysicalMatch{ConnbytesMatch{ConnbytesDir::both,
                                       ConnbytesMode::packets, 3, 3}});
    CHECK(last_match(ct(R"({"ct":{"key":"bytes","dir":"reply"}})",
                        R"({"range":[10,20]})")) ==
          PhysicalMatch{ConnbytesMatch{ConnbytesDir::reply,
                                       ConnbytesMode::bytes, 10, 20}});
    // Ranges above 32 bits and the order of bounds are kept exact.
    CHECK(last_match(ct(original, R"({"range":[1,5000000000]})")) ==
          PhysicalMatch{ConnbytesMatch{ConnbytesDir::original,
                                       ConnbytesMode::packets, 1, 5000000000ULL}});
    for (const auto &entry :
         std::vector<std::tuple<std::string, std::string, const char *>>{
             {original, R"({"range":[1,6]})", "!="},
             {original, "4", ">"},
             {original, "4", ">="},
             {original, R"({"range":[6,1]})", "=="},
             {original, R"({"range":[1,6,7]})", "=="},
             {original, R"("x")", "=="},
             {original, "-1", "=="},
             {R"({"ct":{"key":"packets","dir":"sideways"}})", "4", "=="},
             {R"({"ct":{"key":"packets","dir":"original","family":"ip"}})", "4",
              "=="},
             {R"({"ct":{"key":"avgpkt"}})", "4", "=="}}) {
      const std::string &left = std::get<0>(entry);
      const std::string &right = std::get<1>(entry);
      const char *op = std::get<2>(entry);
      CAPTURE(left);
      CAPTURE(right);
      CAPTURE(op);
      const auto rule = ct(left, right, op);
      CHECK(has_unknown(rule));
      for (const auto &match : rule.matches) {
        CHECK_FALSE(std::holds_alternative<ConnbytesMatch>(match));
      }
    }
  }
  SUBCASE("forward and postrouting base chains") {
    const auto set = parse_nft_json(R"({"nftables":[
      {"chain":{"family":"inet","table":"KeenPbrTable","name":"dns_hold","type":"filter","hook":"postrouting","prio":-150,"policy":"accept"}},
      {"chain":{"family":"inet","table":"KeenPbrTable","name":"sniff_fwd","type":"filter","hook":"forward","prio":-150,"policy":"accept"}},
      {"chain":{"family":"inet","table":"KeenPbrTable","name":"sniff_out","type":"filter","hook":"output","prio":-150,"policy":"accept"}},
      {"chain":{"family":"inet","table":"KeenPbrTable","name":"sniff_in","type":"filter","hook":"input","prio":0,"policy":"accept"}}
    ]})");
    const auto base = [&](Role role) {
      return require_chain(set, nft_id(role)).base;
    };
    CHECK(base(Role::nft_dns_hold)->hook == PhysicalBaseChain::Hook::postrouting);
    CHECK(base(Role::nft_sniff_forward)->hook == PhysicalBaseChain::Hook::forward);
    CHECK(base(Role::nft_sniff_output)->hook == PhysicalBaseChain::Hook::output);
    CHECK(base(Role::nft_dns_hold)->priority == -150);
    PhysicalChainId unknown = nft_id(Role::other_owned);
    unknown.name = "sniff_in";
    CHECK(require_chain(set, unknown).base->hook == PhysicalBaseChain::Hook::other);
  }
}

TEST_CASE("physical: canonical helpers") {
  SUBCASE("cidr") {
    CHECK(canonical_cidr("1.2.3.4") == "1.2.3.4/32");
    CHECK(canonical_cidr("10.1.2.3/8") == "10.0.0.0/8");
    CHECK(canonical_cidr("2001:DB8:0:0::5/64") == "2001:db8::/64");
    CHECK(canonical_cidr("::1") == "::1/128");
    CHECK_FALSE(canonical_cidr("bogus").has_value());
    CHECK_FALSE(canonical_cidr("1.2.3.4/33").has_value());
    std::vector<std::string> list{"10.1.0.0/16", "9.9.9.9", "10.0.0.0/8",
                                  "9.9.9.9/32", "zzz", "::1"};
    canonicalize_cidr_list(list);
    CHECK(list == std::vector<std::string>{"9.9.9.9/32", "10.0.0.0/8", "::1/128",
                                           "zzz"});
  }
  SUBCASE("ports") {
    std::vector<PortRange> ranges{{443, 443}, {80, 80}, {81, 90}, {85, 100},
                                  {80, 80}, {65535, 65535}};
    canonicalize_port_ranges(ranges);
    CHECK(ranges == std::vector<PortRange>{{80, 100}, {443, 443}, {65535, 65535}});
  }
  SUBCASE("rule canonicalization is idempotent and keeps statement order") {
    PhysicalRule rule = make_rule(
        Fam::ipv4,
        {PortMatch{PhysicalTransport::udp, PhysicalDir::dst, false, {{53, 53}}},
         AddrMatch{PhysicalDir::src, false, {"10.0.0.1"}},
         SetMatch{"kpbr4_x", PhysicalDir::dst, false}},
        {VerdictStmt{PhysicalVerdict::drop}, VerdictStmt{PhysicalVerdict::accept}});
    canonicalize_physical_rule(rule);
    CHECK(rule.matches ==
          std::vector<PhysicalMatch>{
              SetMatch{"kpbr4_x", PhysicalDir::dst, false},
              AddrMatch{PhysicalDir::src, false, {"10.0.0.1/32"}},
              ProtoMatch{L4Proto::Udp},
              PortMatch{PhysicalTransport::udp, PhysicalDir::dst, false,
                        {{53, 53}}}});
    CHECK(rule.statements[0] ==
          PhysicalStatement{VerdictStmt{PhysicalVerdict::drop}});
    const PhysicalRule again = [&] {
      PhysicalRule copy = rule;
      canonicalize_physical_rule(copy);
      return copy;
    }();
    CHECK(again == rule);
  }
  SUBCASE("chain identity ignores the diagnostic name") {
    PhysicalChainId a = ipt_id(Role::iptables_prerouting, Table::mangle,
                               Fam::ipv4);
    PhysicalChainId b = a;
    a.name = "KeenPbrTable";
    b.name = "renamed";
    CHECK(a == b);
    b = a;
    b.table = Table::raw;
    CHECK(a != b);
  }
}

TEST_CASE("physical: parse IPv4 and IPv6 CIDR from iptables dump") {
  const std::string text =
      "*mangle\n"
      ":PREROUTING ACCEPT [0:0]\n"
      ":KeenPbrTable - [0:0]\n"
      "-A KeenPbrTable -d 10.0.0.0/8 -j ACCEPT\n"
      "-A KeenPbrTable -d 2001:db8::/32 -j ACCEPT\n"
      "COMMIT\n";
  const auto set = parse_iptables_save(text, Fam::ipv4);
  const auto &chain = require_chain(
      set, ipt_id(Role::iptables_prerouting, Table::mangle, Fam::ipv4));
  REQUIRE(chain.rules.size() == 2);
  CHECK(chain.rules[0].matches ==
        std::vector<PhysicalMatch>{
            AddrMatch{PhysicalDir::dst, false, {"10.0.0.0/8"}}});
  // IPv6 CIDR is parsed and normalized.
  CHECK(chain.rules[1].matches ==
        std::vector<PhysicalMatch>{
            AddrMatch{PhysicalDir::dst, false, {"2001:db8::/32"}}});
}

// ---------------------------------------------------------------------------
// Round trip: lowering the plans that produced the committed kernel dumps
// must give exactly what the parsers read back from those dumps.
// ---------------------------------------------------------------------------

bool is_lowered_role(Role role) {
  return role == Role::iptables_prerouting ||
         role == Role::iptables_output || role == Role::iptables_dns_hold ||
         role == Role::iptables_sniff || role == Role::nft_prerouting ||
         role == Role::nft_output || role == Role::nft_dns_hold ||
         role == Role::nft_sniff_forward || role == Role::nft_sniff_output ||
         role == Role::nft_setter;
}

// Compares every owned chain rule for rule (dispatchers and system hooks are
// backend lifecycle, not lowering).
void check_lowered_equals_parsed(const PhysicalRuleset &lowered,
                                 const PhysicalRuleset &parsed) {
  std::size_t parsed_owned = 0;
  for (const auto &chain : parsed.chains) {
    if (is_lowered_role(chain.id.role)) ++parsed_owned;
  }
  CHECK(lowered.chains.size() == parsed_owned);
  for (const auto &chain : lowered.chains) {
    const PhysicalChain *kernel = parsed.find(chain.id);
    REQUIRE_MESSAGE(kernel != nullptr, "chain not in dump: " << chain.id.name);
    CHECK(kernel->base == chain.base);
    REQUIRE_MESSAGE(kernel->rules.size() == chain.rules.size(),
                    "rule count of " << chain.id.name);
    for (std::size_t index = 0; index < chain.rules.size(); ++index) {
      CHECK_MESSAGE(chain.rules[index] == kernel->rules[index],
                    chain.id.name << " rule " << index);
      CHECK_MESSAGE(chain.rules[index].key.has_value() ==
                        kernel->rules[index].key.has_value(),
                    chain.id.name << " rule " << index << " key");
      if (chain.rules[index].key && kernel->rules[index].key) {
        CHECK(*chain.rules[index].key == *kernel->rules[index].key);
      }
    }
  }
}

FirewallLoweringContext capture_context(FirewallBackend backend,
                                        RawPreroutingMode raw) {
  FirewallLoweringContext context;
  context.backend = backend;
  context.raw_prerouting = raw;
  context.fwmark_mask = kCaptureMask;
  context.physical_set_name = [](const std::string &name) { return name; };
  return context;
}

TEST_CASE("lowering round trip: iptables mangle layout equals the kernel dump") {
  const auto plan = capture_plan(false, false, true);
  const auto lowered = lower_firewall_plan(
      plan, capture_context(FirewallBackend::iptables, {}));
  PhysicalRuleset v4;
  PhysicalRuleset v6;
  for (const auto &chain : lowered.chains) {
    (chain.id.family == Fam::ipv4 ? v4 : v6).chains.push_back(chain);
  }
  check_lowered_equals_parsed(
      v4, parse_iptables_save(read_fixture("iptables_mangle_v4.save"), Fam::ipv4));
  check_lowered_equals_parsed(
      v6, parse_iptables_save(read_fixture("iptables_mangle_v6.save"), Fam::ipv6));
}

TEST_CASE("lowering round trip: iptables RAW PREROUTING layout equals the kernel dump") {
  const auto plan = capture_plan(false, true, true);
  const auto lowered = lower_firewall_plan(
      plan, capture_context(FirewallBackend::iptables,
                            RawPreroutingMode{true, true}));
  PhysicalRuleset v4;
  PhysicalRuleset v6;
  for (const auto &chain : lowered.chains) {
    (chain.id.family == Fam::ipv4 ? v4 : v6).chains.push_back(chain);
  }
  check_lowered_equals_parsed(
      v4, parse_iptables_save(read_fixture("iptables_raw_v4.save"), Fam::ipv4));
  check_lowered_equals_parsed(
      v6, parse_iptables_save(read_fixture("iptables_raw_v6.save"), Fam::ipv6));
}

TEST_CASE("lowering round trip: nftables layout equals the kernel dump") {
  const auto plan = capture_plan(true, true, true);
  const auto lowered =
      lower_firewall_plan(plan, capture_context(FirewallBackend::nftables, {}));
  check_lowered_equals_parsed(lowered,
                              parse_nft_json(read_fixture("nft_balance.json")));
}

TEST_CASE("lowering round trip: iptables interception equals the kernel dump") {
  const auto plan = capture_plan_with_intercept(false, false, true);
  const auto lowered = lower_firewall_plan(
      plan, capture_context(FirewallBackend::iptables, {}));
  PhysicalRuleset v4;
  PhysicalRuleset v6;
  for (const auto &chain : lowered.chains) {
    (chain.id.family == Fam::ipv4 ? v4 : v6).chains.push_back(chain);
  }
  const auto parsed4 = parse_iptables_save(
      read_fixture("iptables_intercept_mangle_v4.save"), Fam::ipv4);
  const auto parsed6 = parse_iptables_save(
      read_fixture("iptables_intercept_mangle_v6.save"), Fam::ipv6);
  check_lowered_equals_parsed(v4, parsed4);
  check_lowered_equals_parsed(v6, parsed6);
  // The `-S` spelling of the same state parses identically.
  const auto parsed_s = parse_iptables_save(
      read_fixture("iptables_intercept_mangle_v4.rules"), Fam::ipv4);
  check_lowered_equals_parsed(v4, parsed_s);

  // Pinned hooks: first rule of each builtin chain, in both families.
  for (const auto *parsed : {&parsed4, &parsed6}) {
    const Fam family = parsed == &parsed4 ? Fam::ipv4 : Fam::ipv6;
    const auto first = [&](Role role, const char *name) {
      return require_chain(*parsed, ipt_id(role, Table::mangle, family, name))
          .rules;
    };
    const auto post = first(Role::system_other, "POSTROUTING");
    const auto fwd = first(Role::system_other, "FORWARD");
    const auto out = first(Role::system_output, "OUTPUT");
    REQUIRE(post.size() == 1);
    REQUIRE(fwd.size() == 1);
    REQUIRE(out.size() == 2);
    CHECK(post[0].hook_position == std::optional<uint32_t>{0});
    CHECK(fwd[0].hook_position == std::optional<uint32_t>{0});
    CHECK(out[0].hook_position == std::optional<uint32_t>{0});
    CHECK_FALSE(out[1].hook_position.has_value());
    const auto *jump = std::get_if<JumpStmt>(&out[0].statements.at(0));
    REQUIRE(jump != nullptr);
    CHECK(jump->target.role == Role::iptables_sniff);
  }
}

TEST_CASE("lowering round trip: nftables interception equals the kernel dump") {
  const auto plan = capture_plan_with_intercept(true, true, true);
  const auto lowered =
      lower_firewall_plan(plan, capture_context(FirewallBackend::nftables, {}));
  const auto parsed = parse_nft_json(read_fixture("nft_intercept.json"));
  check_lowered_equals_parsed(lowered, parsed);
  for (const auto role : {Role::nft_dns_hold, Role::nft_sniff_forward,
                          Role::nft_sniff_output}) {
    CHECK(lowered.find(nft_id(role)) != nullptr);
  }
}

TEST_CASE("physical: catch-all address matches have one canonical form") {
  SUBCASE("cover detection") {
    CHECK(cidrs_cover_address_family({"0.0.0.0/0"}));
    CHECK(cidrs_cover_address_family({"::/0"}));
    CHECK(cidrs_cover_address_family({"0.0.0.0/1", "128.0.0.0/1"}));
    CHECK(cidrs_cover_address_family(
        {"128.0.0.0/2", "0.0.0.0/1", "192.0.0.0/2", "10.0.0.0/8"}));
    CHECK(cidrs_cover_address_family({"::/1", "8000::/1"}));
    CHECK_FALSE(cidrs_cover_address_family({"0.0.0.0/1"}));
    CHECK_FALSE(cidrs_cover_address_family({"0.0.0.0/1", "128.0.0.0/2"}));
    CHECK_FALSE(cidrs_cover_address_family({"10.0.0.0/8"}));
    CHECK_FALSE(cidrs_cover_address_family({"0.0.0.0/1", "::/1"}));
    CHECK_FALSE(cidrs_cover_address_family({}));
  }
  SUBCASE("dropped, family kept") {
    for (const auto &cidrs : std::vector<std::vector<std::string>>{
             {"0.0.0.0/0"}, {"0.0.0.0/1", "128.0.0.0/1"}}) {
      PhysicalRule rule = make_rule(
          Fam::any,
          {AddrMatch{PhysicalDir::dst, false, cidrs},
           ProtoMatch{L4Proto::Udp}},
          {VerdictStmt{PhysicalVerdict::accept}});
      canonicalize_physical_rule(rule);
      CHECK(rule.family == Fam::ipv4);
      CHECK(rule.matches == std::vector<PhysicalMatch>{ProtoMatch{L4Proto::Udp}});
    }
    PhysicalRule v6 = make_rule(
        Fam::any, {AddrMatch{PhysicalDir::src, false, {"::/0"}}},
        {VerdictStmt{PhysicalVerdict::accept}});
    canonicalize_physical_rule(v6);
    CHECK(v6.family == Fam::ipv6);
    CHECK(v6.matches.empty());
  }
  SUBCASE("a negated catch-all is kept (lowering rejects it)") {
    PhysicalRule rule = make_rule(
        Fam::ipv4, {AddrMatch{PhysicalDir::dst, true, {"0.0.0.0/0"}}}, {});
    canonicalize_physical_rule(rule);
    CHECK(rule.matches.size() == 1);
  }
}

TEST_CASE("lowering round trip: iptables catch-all equals the kernel dump") {
  const auto lowered = lower_firewall_plan(
      capture_plan_catch_all(), capture_context(FirewallBackend::iptables, {}));
  PhysicalRuleset v4;
  PhysicalRuleset v6;
  for (const auto &chain : lowered.chains) {
    (chain.id.family == Fam::ipv4 ? v4 : v6).chains.push_back(chain);
  }
  const auto kernel_v4 = parse_iptables_save(
      read_fixture("iptables_catchall_v4.save"), Fam::ipv4);
  const auto kernel_v6 = parse_iptables_save(
      read_fixture("iptables_catchall_v6.save"), Fam::ipv6);
  check_lowered_equals_parsed(v4, kernel_v4);
  check_lowered_equals_parsed(v6, kernel_v6);
  // The catch-all rules carry no address match at all.
  const auto &chain = require_chain(
      v4, ipt_id(Role::iptables_prerouting, Table::mangle, Fam::ipv4));
  std::size_t addr_free = 0;
  for (const auto &rule : chain.rules) {
    bool has_addr = false;
    for (const auto &match : rule.matches) {
      has_addr = has_addr || std::holds_alternative<AddrMatch>(match);
    }
    if (!has_addr) ++addr_free;
  }
  CHECK(addr_free >= 4); // restore pair, reply skip, catch-all rules
}

TEST_CASE("lowering round trip: nftables catch-all equals the kernel dump") {
  const auto lowered = lower_firewall_plan(
      capture_plan_catch_all(), capture_context(FirewallBackend::nftables, {}));
  const auto parsed = parse_nft_json(read_fixture("nft_catchall.json"));
  check_lowered_equals_parsed(lowered, parsed);
  // The family of a dropped catch-all survives as an nfproto guard.
  const auto &pre = require_chain(lowered, nft_id(Role::nft_prerouting));
  bool v4 = false;
  bool v6 = false;
  for (const auto &rule : pre.rules) {
    bool has_addr = false;
    for (const auto &match : rule.matches) {
      has_addr = has_addr || std::holds_alternative<AddrMatch>(match);
    }
    if (!has_addr && rule.family == Fam::ipv4) v4 = true;
    if (!has_addr && rule.family == Fam::ipv6) v6 = true;
  }
  CHECK(v4);
  CHECK(v6);
}

namespace {
bool has_oif_or_type(const PhysicalChain &chain) {
  std::size_t oif = 0;
  std::size_t type = 0;
  for (const auto &rule : chain.rules) {
    for (const auto &match : rule.matches) {
      oif += std::holds_alternative<OifMatch>(match) ? 1U : 0U;
      type += std::holds_alternative<AddrTypeMatch>(match) ? 1U : 0U;
    }
  }
  return oif == 1 && type == 2;
}
} // namespace

TEST_CASE("lowering round trip: iptables LAN-output skip equals the kernel dump") {
  const auto lowered = lower_firewall_plan(
      capture_plan_lan_output(), capture_context(FirewallBackend::iptables, {}));
  PhysicalRuleset v4;
  PhysicalRuleset v6;
  for (const auto &chain : lowered.chains) {
    (chain.id.family == Fam::ipv4 ? v4 : v6).chains.push_back(chain);
  }
  const auto kernel_v4 = parse_iptables_save(
      read_fixture("iptables_lan_output_v4.save"), Fam::ipv4);
  const auto kernel_v6 = parse_iptables_save(
      read_fixture("iptables_lan_output_v6.save"), Fam::ipv6);
  check_lowered_equals_parsed(v4, kernel_v4);
  check_lowered_equals_parsed(v6, kernel_v6);
  // OUTPUT: restore pair, reply skip, 2 oif rules, bcast, mcast (v4); no bcast for v6.
  const auto out4 = require_chain(
      kernel_v4, ipt_id(Role::iptables_output, Table::mangle, Fam::ipv4));
  const auto out6 = require_chain(
      kernel_v6, ipt_id(Role::iptables_output, Table::mangle, Fam::ipv6));
  CHECK(out4.rules.size() == 10); // + 3 catch-all mark rules
  CHECK(out6.rules.size() == 6);
  // PREROUTING never sees the new rules.
  const auto pre4 = require_chain(
      kernel_v4, ipt_id(Role::iptables_prerouting, Table::mangle, Fam::ipv4));
  for (const auto &rule : pre4.rules) {
    for (const auto &match : rule.matches) {
      CHECK_FALSE(std::holds_alternative<OifMatch>(match));
      CHECK_FALSE(std::holds_alternative<AddrTypeMatch>(match));
    }
  }
}

TEST_CASE("lowering round trip: nftables LAN-output skip equals the kernel dump") {
  const auto lowered = lower_firewall_plan(
      capture_plan_lan_output(), capture_context(FirewallBackend::nftables, {}));
  const auto parsed = parse_nft_json(read_fixture("nft_lan_output.json"));
  check_lowered_equals_parsed(lowered, parsed);
  const auto &out = require_chain(parsed, nft_id(Role::nft_output));
  CHECK(has_oif_or_type(out));
}

TEST_CASE("address lists compare as address sets") {
  using Strs = std::vector<std::string>;
  const auto canon = [](Strs list) {
    canonicalize_cidr_list(list);
    return list;
  };
  SUBCASE("overlap, containment, duplicates, unsorted input") {
    CHECK(canon({"10.0.1.0/24", "10.0.0.0/16", "10.0.1.5", "10.0.0.0/16"}) ==
          Strs{"10.0.0.0/16"});
    CHECK(canon({"10.0.0.128/25", "10.0.0.0/25"}) == Strs{"10.0.0.0/24"});
    CHECK(canon({"9.0.0.0/8", "8.0.0.0/8"}) == Strs{"8.0.0.0/7"});
  }
  SUBCASE("adjacency merges, a gap does not") {
    CHECK(canon({"10.0.0.0/24", "10.0.1.0/24", "10.0.2.0/23"}) ==
          Strs{"10.0.0.0/22"});
    CHECK(canon({"10.0.0.0/24", "10.0.2.0/24"}) ==
          Strs{"10.0.0.0/24", "10.0.2.0/24"});
    // Adjacent but not aligned to a single block.
    CHECK(canon({"10.0.1.0/24", "10.0.2.0/24"}) ==
          Strs{"10.0.1.0/24", "10.0.2.0/24"});
  }
  SUBCASE("/32 singles") {
    CHECK(canon({"77.74.65.226", "77.74.65.225"}) ==
          Strs{"77.74.65.225/32", "77.74.65.226/32"});
    CHECK(canon({"1.1.1.2", "1.1.1.3", "1.1.1.0/31"}) == Strs{"1.1.1.0/30"});
    CHECK(canon({"255.255.255.255", "255.255.255.254"}) ==
          Strs{"255.255.255.254/31"});
    CHECK(canon({"0.0.0.0", "0.0.0.1"}) == Strs{"0.0.0.0/31"});
  }
  SUBCASE("whole family and family boundaries") {
    CHECK(canon({"0.0.0.0/1", "128.0.0.0/1"}) == Strs{"0.0.0.0/0"});
    CHECK(canon({"::/1", "8000::/1"}) == Strs{"::/0"});
    CHECK(canon({"255.255.255.255", "0.0.0.0/0"}) == Strs{"0.0.0.0/0"});
  }
  SUBCASE("ipv6") {
    CHECK(canon({"2001:db8:8000::/33", "2001:db8::/33"}) ==
          Strs{"2001:db8::/32"});
    CHECK(canon({"2001:db8:1::2", "2001:db8:1::1"}) ==
          Strs{"2001:db8:1::1/128", "2001:db8:1::2/128"});
    CHECK(canon({"2001:db8::1", "2001:db8::/64", "2001:db8:0:1::/64"}) ==
          Strs{"2001:db8::/63"});
    CHECK(canon({"ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff",
                 "ffff:ffff:ffff:ffff:ffff:ffff:ffff:fffe"}) ==
          Strs{"ffff:ffff:ffff:ffff:ffff:ffff:ffff:fffe/127"});
    CHECK(canon({"::/65", "::8000:0:0:0/65"}) == Strs{"::/64"});
  }
  SUBCASE("families stay separate, v4 first") {
    CHECK(canon({"::1", "1.2.3.4"}) == Strs{"1.2.3.4/32", "::1/128"});
  }
  SUBCASE("range to CIDR decomposition") {
    CHECK(cidrs_from_range("192.168.40.0", "192.168.45.255") ==
          Strs{"192.168.40.0/22", "192.168.44.0/23"});
    CHECK(cidrs_from_range("77.74.65.225", "77.74.65.226") ==
          Strs{"77.74.65.225/32", "77.74.65.226/32"});
    CHECK(cidrs_from_range("10.0.0.0", "10.0.0.0") == Strs{"10.0.0.0/32"});
    CHECK(cidrs_from_range("0.0.0.0", "255.255.255.255") == Strs{"0.0.0.0/0"});
    CHECK(cidrs_from_range("0.0.0.1", "0.0.0.6") ==
          Strs{"0.0.0.1/32", "0.0.0.2/31", "0.0.0.4/31", "0.0.0.6/32"});
    CHECK(cidrs_from_range("2001:db8::", "2001:db8::ffff") ==
          Strs{"2001:db8::/112"});
    CHECK(cidrs_from_range("::", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff") ==
          Strs{"::/0"});
    CHECK(cidrs_from_range("10.0.0.5", "10.0.0.1").empty());
    CHECK(cidrs_from_range("10.0.0.1", "::1").empty());
    CHECK(cidrs_from_range("10.0.0.0/8", "10.0.0.9").empty());
  }
}

TEST_CASE("nft interval-set spellings parse to the same address set") {
  const auto parse_dst = [](const std::string &expr) {
    const auto set = parse_nft_json(
        R"({"nftables":[{"chain":{"family":"inet","table":"KeenPbrTable","name":"prerouting","handle":1}},
{"rule":{"family":"inet","table":"KeenPbrTable","chain":"prerouting","handle":2,"expr":[)" +
        expr + R"(,{"accept":null}]}}]})");
    REQUIRE(set.chains.size() == 1);
    REQUIRE(set.chains[0].rules.size() == 1);
    return set.chains[0].rules[0];
  };
  const auto match = [](const std::string &right, const char *op = "!=") {
    return std::string(R"({"match":{"op":")") + op +
           R"(","left":{"payload":{"protocol":"ip","field":"daddr"}},"right":)" +
           right + "}}";
  };
  const auto expected = [](bool negate, std::vector<std::string> cidrs) {
    PhysicalRule rule = make_rule(
        Fam::ipv4, {AddrMatch{PhysicalDir::dst, negate, std::move(cidrs)}},
        {VerdictStmt{PhysicalVerdict::accept}});
    canonicalize_physical_rule(rule);
    return rule;
  };
  SUBCASE("field case: prefixes and ranges versus the lowered CIDR list") {
    const PhysicalRule observed = parse_dst(match(
        R"({"set":[{"prefix":{"addr":"0.0.0.0","len":8}},
{"range":["77.74.65.225","77.74.65.226"]},
{"range":["192.168.40.0","192.168.45.255"]},
{"prefix":{"addr":"192.168.54.0","len":23}}]})"));
    const PhysicalRule want = expected(
        true, {"0.0.0.0/8", "77.74.65.225/32", "77.74.65.226/32",
               "192.168.40.0/23", "192.168.42.0/24", "192.168.43.0/24",
               "192.168.44.0/24", "192.168.45.0/24", "192.168.54.0/24",
               "192.168.55.0/24"});
    CHECK(observed == want);
    const auto *addr = std::get_if<AddrMatch>(&want.matches.at(0));
    REQUIRE(addr != nullptr);
    CHECK(addr->cidrs ==
          std::vector<std::string>{"0.0.0.0/8", "77.74.65.225/32",
                                   "77.74.65.226/32", "192.168.40.0/22",
                                   "192.168.44.0/23", "192.168.54.0/23"});
  }
  SUBCASE("single operands: string, prefix and range") {
    CHECK(parse_dst(match(R"("10.1.2.3")")) == expected(true, {"10.1.2.3"}));
    CHECK(parse_dst(match(R"({"prefix":{"addr":"10.0.0.0","len":8}})", "==")) ==
          expected(false, {"10.0.0.0/8"}));
    CHECK(parse_dst(match(R"({"range":["10.0.0.0","10.0.1.255"]})", "==")) ==
          expected(false, {"10.0.0.0/24", "10.0.1.0/24"}));
  }
  SUBCASE("a different address set still differs") {
    const PhysicalRule observed = parse_dst(match(
        R"({"set":[{"range":["192.168.40.0","192.168.45.255"]}]})"));
    CHECK_FALSE(observed == expected(true, {"192.168.40.0/22", "192.168.44.0/24"}));
    CHECK_FALSE(observed == expected(true, {"192.168.40.0/22", "192.168.44.0/23",
                                            "192.168.46.0/32"}));
    CHECK_FALSE(observed == expected(false, {"192.168.40.0/22", "192.168.44.0/23"}));
    CHECK(observed == expected(true, {"192.168.40.0/22", "192.168.44.0/23"}));
  }
  SUBCASE("malformed range is an unknown match") {
    const PhysicalRule observed =
        parse_dst(match(R"({"set":[{"range":["10.0.0.9","10.0.0.1"]}]})"));
    CHECK(std::holds_alternative<UnknownMatch>(observed.matches.at(0)));
  }
}

TEST_CASE("real nft interval dump equals the lowered address lists") {
  const auto set = parse_nft_json(read_fixture("nft_interval.json"));
  const auto &chain = set.chains.at(0);
  REQUIRE(chain.rules.size() == 3);
  const auto lowered = [](Fam family, PhysicalDir dir, bool negate,
                          std::vector<std::string> cidrs) {
    PhysicalRule rule = make_rule(family, {AddrMatch{dir, negate, std::move(cidrs)}},
                                  {VerdictStmt{PhysicalVerdict::accept}});
    canonicalize_physical_rule(rule);
    return rule;
  };
  CHECK(chain.rules[0] ==
        lowered(Fam::ipv4, PhysicalDir::dst, true,
                {"0.0.0.0/8", "10.0.0.0/24", "10.0.1.0/24", "10.0.2.0/23",
                 "77.74.65.225/32", "77.74.65.226/32", "192.168.40.0/23",
                 "192.168.42.0/24", "192.168.43.0/24", "192.168.44.0/24",
                 "192.168.45.0/24", "192.168.54.0/24", "192.168.55.0/24"}));
  CHECK(chain.rules[1] ==
        lowered(Fam::ipv4, PhysicalDir::src, false,
                {"198.51.100.0/25", "198.51.100.128/25", "203.0.113.9",
                 "203.0.113.10", "203.0.113.0/28"}));
  CHECK(chain.rules[2] ==
        lowered(Fam::ipv6, PhysicalDir::dst, true,
                {"2001:db8::/33", "2001:db8:8000::/33", "2001:db8:1::1",
                 "2001:db8:1::2"}));
  CHECK_FALSE(chain.rules[0] ==
              lowered(Fam::ipv4, PhysicalDir::dst, true,
                      {"0.0.0.0/8", "10.0.0.0/22", "77.74.65.225/32",
                       "192.168.40.0/22", "192.168.44.0/23", "192.168.54.0/23"}));
}

TEST_CASE("physical: output interface and destination address type matches") {
  SUBCASE("iptables -o and addrtype") {
    const auto oif = parse_sniff_rule("-o br-lan -j RETURN");
    REQUIRE(oif.matches.size() == 1);
    CHECK(oif.matches[0] == PhysicalMatch{OifMatch{false, {"br-lan"}}});
    const auto noif = parse_sniff_rule("! -o br-lan -j RETURN");
    CHECK(noif.matches[0] == PhysicalMatch{OifMatch{true, {"br-lan"}}});

    const auto bcast =
        parse_sniff_rule("-m addrtype --dst-type BROADCAST -j RETURN");
    REQUIRE(bcast.matches.size() == 1);
    CHECK(bcast.matches[0] == PhysicalMatch{AddrTypeMatch{addr_broadcast}});
    const auto mcast =
        parse_sniff_rule("-m addrtype --dst-type MULTICAST -j RETURN");
    CHECK(mcast.matches[0] == PhysicalMatch{AddrTypeMatch{addr_multicast}});
    // Unsupported address types stay explicit unknowns, never dropped.
    const auto local =
        parse_sniff_rule("-m addrtype --dst-type LOCAL -j RETURN");
    CHECK(std::holds_alternative<UnknownMatch>(local.matches.at(0)));
  }

  SUBCASE("nft oifname and fib daddr type") {
    const auto single = parse_nft_expr(
        R"([{"match":{"op":"==","left":{"meta":{"key":"oifname"}},"right":"br-lan"}},{"accept":null}])",
        "output");
    CHECK(single.matches.at(0) == PhysicalMatch{OifMatch{false, {"br-lan"}}});
    const auto many = parse_nft_expr(
        R"([{"match":{"op":"==","left":{"meta":{"key":"oifname"}},"right":{"set":["wg0","br-lan"]}}},{"accept":null}])",
        "output");
    PhysicalRule canonical = many;
    canonicalize_physical_rule(canonical);
    CHECK(canonical.matches.at(0) ==
          PhysicalMatch{OifMatch{false, {"br-lan", "wg0"}}});

    const auto both = parse_nft_expr(
        R"([{"match":{"op":"==","left":{"fib":{"result":"type","flags":["daddr"]}},"right":{"set":["broadcast","multicast"]}}},{"accept":null}])",
        "output");
    CHECK(both.matches.at(0) ==
          PhysicalMatch{AddrTypeMatch{addr_broadcast | addr_multicast}});
    const auto mcast = parse_nft_expr(
        R"([{"match":{"op":"==","left":{"fib":{"result":"type","flags":["daddr"]}},"right":"multicast"}},{"accept":null}])",
        "output");
    CHECK(mcast.matches.at(0) == PhysicalMatch{AddrTypeMatch{addr_multicast}});
    // Anything else about fib stays an explicit unknown.
    const auto saddr = parse_nft_expr(
        R"([{"match":{"op":"==","left":{"fib":{"result":"type","flags":["saddr"]}},"right":"multicast"}},{"accept":null}])",
        "output");
    CHECK(std::holds_alternative<UnknownMatch>(saddr.matches.at(0)));
  }
}

} // namespace
} // namespace keen_pbr3
