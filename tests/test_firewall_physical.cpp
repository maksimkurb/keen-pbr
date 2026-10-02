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
  if (name == "nftables") {
    auto firewall = create_nftables_firewall();
    firewall->prepare_apply(FirewallApplyMode::Destructive);
    firewall->apply(capture_plan(true, true, true),
                    FirewallApplyMode::Destructive);
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
  firewall.prepare_apply(FirewallApplyMode::Destructive);
  firewall.apply(capture_plan(false, name != "iptables_mangle", true),
                 FirewallApplyMode::Destructive);
}


// ---------------------------------------------------------------------------
// Parser tests.  Fixtures under tests/firewall_it/fixtures/physical are REAL
// kernel dumps (see capture_physical_fixtures.sh and the README there).
// ---------------------------------------------------------------------------

using Role = PhysicalChainRole;
using Gen = PhysicalGeneration;
using Table = PhysicalTable;
using Fam = FirewallFamily;

PhysicalChainId ipt_id(Role role, Table table, Fam family,
                       Gen generation = Gen::none) {
  PhysicalChainId id;
  id.role = role;
  id.table = table;
  id.family = family;
  id.generation = generation;
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

  const auto a = ipt_id(Role::prerouting_generation, Table::mangle, Fam::ipv4,
                        Gen::a);
  const auto &chain_a = require_chain(set, a);
  CHECK(chain_a.rules.size() ==
        count_lines_with_prefix(text, "-A KeenPbrTable_A "));
  CHECK(chain_a.rules.size() == 31);
  for (const auto &rule : chain_a.rules) {
    CHECK_FALSE(has_unknown(rule));
    CHECK(rule.key.has_value());
  }

  SUBCASE("dispatchers and hook rules") {
    const auto dispatcher =
        ipt_id(Role::prerouting_dispatcher, Table::mangle, Fam::ipv4);
    const auto output =
        ipt_id(Role::output_dispatcher, Table::mangle, Fam::ipv4);
    const auto &d = require_chain(set, dispatcher);
    REQUIRE(d.rules.size() == 1);
    CHECK(d.rules[0] == make_rule(Fam::ipv4, {}, {JumpStmt{a, false}}));
    const auto &o = require_chain(set, output);
    REQUIRE(o.rules.size() == 1);
    CHECK(o.rules[0] == make_rule(Fam::ipv4, {}, {JumpStmt{a, false}}));

    const auto &pre = require_chain(
        set, ipt_id(Role::system_prerouting, Table::mangle, Fam::ipv4));
    REQUIRE(pre.rules.size() == 1);
    CHECK(pre.rules[0] ==
          make_rule(Fam::ipv4, {}, {JumpStmt{dispatcher, false}}));
    const auto &out = require_chain(
        set, ipt_id(Role::system_output, Table::mangle, Fam::ipv4));
    REQUIRE(out.rules.size() == 1);
    CHECK(out.rules[0] == make_rule(Fam::ipv4, {}, {JumpStmt{output, false}}));
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
    CHECK(chain_a.rules[2] ==
          make_rule(Fam::ipv4, {CtStateMatch{ct_dnat, false}},
                    {VerdictStmt{PhysicalVerdict::return_}}));
    // `-m mark ! --mark 0x0`: iptables-save drops the /0xffffffff mask.
    CHECK(chain_a.rules[3] ==
          make_rule(Fam::ipv4,
                    {MarkMatch{PhysicalMarkKind::packet, 0xFFFFFFFFu, true, {0}}},
                    {VerdictStmt{PhysicalVerdict::accept}}));
    CHECK(chain_a.rules[4] ==
          make_rule(Fam::ipv4, {IifMatch{true, {"lan0"}}},
                    {VerdictStmt{PhysicalVerdict::return_}}));
  }

  SUBCASE("route rules: kernel match order is canonicalized") {
    // `-p tcp -m set --match-set kpbr4s_hybrid dst -m tcp --dport 443`
    const PhysicalRule mark = make_rule(
        Fam::ipv4,
        {SetMatch{"kpbr4s_hybrid", PhysicalDir::dst, false},
         ProtoMatch{L4Proto::Tcp},
         PortMatch{PhysicalTransport::tcp, PhysicalDir::dst, false, {{443, 443}}}},
        {SetMarkStmt{PhysicalMarkKind::packet, 0x10000u, kMask}});
    CHECK(chain_a.rules[5] == mark);
    CHECK(chain_a.rules[5].key->module_id == "route.mark");
    // The CONNMARK --save-mark and RETURN rules follow in order.
    CHECK(chain_a.rules[6].statements ==
          std::vector<PhysicalStatement>{CopyMarkStmt{true, kMask, kMask}});
    CHECK(chain_a.rules[7].statements ==
          std::vector<PhysicalStatement>{
              VerdictStmt{PhysicalVerdict::return_}});
    CHECK(chain_a.rules[6].matches == chain_a.rules[5].matches);
    // `! -s 10.0.0.0/8 ... -m udp --dport 53 -j DROP`
    CHECK(chain_a.rules[8] ==
          make_rule(Fam::ipv4,
                    {SetMatch{"kpbr4s_hybrid", PhysicalDir::dst, false},
                     AddrMatch{PhysicalDir::src, true, {"10.0.0.0/8"}},
                     ProtoMatch{L4Proto::Udp},
                     PortMatch{PhysicalTransport::udp, PhysicalDir::dst, false,
                               {{53, 53}}}},
                    {VerdictStmt{PhysicalVerdict::drop}}));
    // `-d 8.8.8.8/32 -p udp -m udp --dport 53 -j RETURN`
    CHECK(chain_a.rules[9].matches[0] ==
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
    bool dynamic_set = false;
    for (const auto &rule : chain_a.rules) {
      for (const auto &match : rule.matches) {
        if (const auto *port = std::get_if<PortMatch>(&match)) {
          negated_dport = negated_dport || (port->negate && port->ranges.size() == 1 &&
                                            port->ranges[0].from == 443);
        }
        if (const auto *set_match = std::get_if<SetMatch>(&match)) {
          dynamic_set = dynamic_set || set_match->name == "kpbr4d_routed";
        }
      }
    }
    CHECK(negated_dport);
    CHECK(dynamic_set);
  }
}

TEST_CASE("physical: iptables IPv6 dump") {
  const auto set =
      parse_iptables_save(read_fixture("iptables_mangle_v6.save"), Fam::ipv6);
  const auto &chain = require_chain(
      set, ipt_id(Role::prerouting_generation, Table::mangle, Fam::ipv6, Gen::a));
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
  CHECK(set.find(ipt_id(Role::prerouting_generation, Table::mangle, Fam::ipv4,
                        Gen::a)) == nullptr);
}

TEST_CASE("physical: iptables raw PREROUTING layout (raw + mangle OUTPUT)") {
  const std::string text = read_fixture("iptables_raw_v4.save");
  const auto set = parse_iptables_save(text, Fam::ipv4);

  const auto raw_a =
      ipt_id(Role::prerouting_generation, Table::raw, Fam::ipv4, Gen::a);
  const auto raw_dispatcher =
      ipt_id(Role::prerouting_dispatcher, Table::raw, Fam::ipv4);
  const auto out_a =
      ipt_id(Role::output_generation, Table::mangle, Fam::ipv4, Gen::a);
  const auto out_dispatcher =
      ipt_id(Role::output_dispatcher, Table::mangle, Fam::ipv4);

  const auto &raw_chain = require_chain(set, raw_a);
  CHECK(raw_chain.rules.size() ==
        count_lines_with_prefix(text, "-A KeenPbrRaw_A "));
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
        count_lines_with_prefix(text, "-A KeenPbrOutput_A "));
  CHECK(out_chain.rules[0].statements ==
        std::vector<PhysicalStatement>{CopyMarkStmt{false, kMask, kMask}});

  // Hooks: raw PREROUTING and mangle OUTPUT.
  const auto &pre = require_chain(
      set, ipt_id(Role::system_prerouting, Table::raw, Fam::ipv4));
  CHECK(pre.rules ==
        std::vector<PhysicalRule>{
            make_rule(Fam::ipv4, {}, {JumpStmt{raw_dispatcher, false}})});
  const auto &out = require_chain(
      set, ipt_id(Role::system_output, Table::mangle, Fam::ipv4));
  CHECK(out.rules ==
        std::vector<PhysicalRule>{
            make_rule(Fam::ipv4, {}, {JumpStmt{out_dispatcher, false}})});
  CHECK(require_chain(set, raw_dispatcher).rules[0] ==
        make_rule(Fam::ipv4, {}, {JumpStmt{raw_a, false}}));
}

TEST_CASE("physical: iptables foreign rules, unknown matches, stray hooks") {
  const std::string text = read_fixture("iptables_mangle_v4_foreign.save");
  const auto set = parse_iptables_save(text, Fam::ipv4);
  const auto a = ipt_id(Role::prerouting_generation, Table::mangle, Fam::ipv4,
                        Gen::a);
  const auto &chain = require_chain(set, a);
  const std::size_t total = count_lines_with_prefix(text, "-A KeenPbrTable_A ");
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
    CHECK(std::get<UnknownMatch>(log.matches.at(0)).text == "-o eth9");
    // Comment of a foreign rule is not ours -> no key (and no exception).
    CHECK_FALSE(log.key.has_value());
  }

  SUBCASE("only hook rules into keen-pbr chains are kept in system chains") {
    const auto &pre = require_chain(
        set, ipt_id(Role::system_prerouting, Table::mangle, Fam::ipv4));
    // `-A PREROUTING -j KeenPbrTable` (real hook) and a later
    // `-j KeenPbrTable_A` (misplaced extra hook).  `-i eth0 -j ACCEPT` is
    // foreign and dropped.
    REQUIRE(pre.rules.size() == 2);
    CHECK(std::get<JumpStmt>(pre.rules[0].statements[0]).target.role ==
          Role::prerouting_dispatcher);
    CHECK(std::get<JumpStmt>(pre.rules[1].statements[0]).target.role ==
          Role::prerouting_generation);
  }
}

TEST_CASE("physical: `iptables -S` output parses to the same ruleset") {
  // `-S` has `-N`/`-P` instead of `:chain` lines and no `*table` header.
  const auto saved =
      parse_iptables_save(read_fixture("iptables_mangle_v4.save"), Fam::ipv4);
  const auto listed = parse_iptables_save(
      read_fixture("iptables_mangle_v4.rules"), Fam::ipv4, Table::mangle);
  CHECK(saved == listed);
  CHECK(saved.chains.size() == 5);
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
  const auto a = ipt_id(Role::prerouting_generation, Table::mangle, Fam::ipv4,
                        Gen::a);
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
    const auto b = ipt_id(Role::prerouting_generation, Table::mangle, Fam::ipv4,
                          Gen::b);
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
        v6, ipt_id(Role::prerouting_generation, Table::mangle, Fam::ipv6, Gen::a));
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
      ":KeenPbrTable_A - [0:0]\n"
      "-A KeenPbrTable_A -j ACCEPT\n"
      "COMMIT\n"
      "*mangle\n"
      ":PREROUTING ACCEPT [0:0]\n"
      ":KeenPbrTable_A - [0:0]\n"
      ":KeenPbrWeird - [0:0]\n"
      ":UserChain - [0:0]\n"
      "[5:300] -A KeenPbrTable_A -m comment --comment \"kpbr:v1:a:b\" -j DROP\n"
      "-A KeenPbrTable_A -j UserChain\n"
      "-A KeenPbrTable_A\n"
      "-A KeenPbrTable_A -j MARK --set-xmark 0x1/0x0\n"
      "-A KeenPbrTable_A -p icmp -j ACCEPT\n"
      "-A KeenPbrTable_A -m comment --comment \"say \\\"hi\\\"\" -j ACCEPT\n"
      "-A KeenPbrWeird -j ACCEPT\n"
      "-A UserChain -j KeenPbrTable_A\n"
      "-A PREROUTING -m comment --comment kpbr:v1:hook:1 -j KeenPbrTable_A\n"
      "COMMIT\n";
  const auto set = parse_iptables_save(text, Fam::ipv4);
  const auto a = ipt_id(Role::prerouting_generation, Table::mangle, Fam::ipv4,
                        Gen::a);
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
  REQUIRE(p.rules.size() == 18);
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
    CHECK(p.rules[1] ==
          make_rule(Fam::any, {CtStateMatch{ct_dnat, false}},
                    {VerdictStmt{PhysicalVerdict::accept}}));
    CHECK(p.rules[2] ==
          make_rule(Fam::any,
                    {MarkMatch{PhysicalMarkKind::packet, 0xFFFFFFFFu, true, {0}}},
                    {VerdictStmt{PhysicalVerdict::accept}}));
    // iifname set is listed sorted although emitted as lan0, br-guest.
    CHECK(p.rules[3] ==
          make_rule(Fam::any, {IifMatch{true, {"br-guest", "lan0"}}},
                    {VerdictStmt{PhysicalVerdict::accept}}));
  }

  SUBCASE("route rules") {
    // family comes from the `ip` / `ip6` payload protocol; nft drops the
    // redundant `meta l4proto tcp`, the parser adds it back canonically.
    CHECK(p.rules[4] ==
          make_rule(Fam::ipv4,
                    {SetMatch{"kpbr4_hybrid", PhysicalDir::dst, false},
                     ProtoMatch{L4Proto::Tcp},
                     PortMatch{PhysicalTransport::tcp, PhysicalDir::dst, false,
                               {{443, 443}}}},
                    {JumpStmt{setter(0x10000u), false}}));
    CHECK(p.rules[5].family == Fam::ipv6);
    // single address is a bare string (no /len): canonical /128.
    CHECK(p.rules[10].matches[0] ==
          PhysicalMatch{AddrMatch{PhysicalDir::dst, false, {"2001:db8:53::53/128"}}});
    // `!= 10.0.0.0/8` prefix object.
    CHECK(p.rules[6].matches[1] ==
          PhysicalMatch{AddrMatch{PhysicalDir::src, true, {"10.0.0.0/8"}}});
    // address set; both entries are single addresses.
    CHECK(p.rules[7].matches[0] ==
          PhysicalMatch{AddrMatch{PhysicalDir::dst, false,
                                  {"8.8.8.8/32", "9.9.9.9/32"}}});
    // `th sport 1111`: no protocol, so no ProtoMatch.
    CHECK(p.rules[8] ==
          make_rule(Fam::any,
                    {PortMatch{PhysicalTransport::any, PhysicalDir::src, false,
                               {{1111, 1111}}}},
                    {JumpStmt{setter(0x20000u), false}}));
    // `tcp dport != 443`
    CHECK(p.rules[9].matches[2] ==
          PhysicalMatch{PortMatch{PhysicalTransport::tcp, PhysicalDir::dst, true,
                                  {{443, 443}}}});
    // `ip dscp ef` -> 46 and port set with range.
    CHECK(p.rules[11] ==
          make_rule(Fam::ipv4,
                    {ProtoMatch{L4Proto::Tcp},
                     PortMatch{PhysicalTransport::tcp, PhysicalDir::dst, false,
                               {{80, 80}, {443, 443}, {8000, 9000}}},
                     DscpMatch{46}},
                    {JumpStmt{setter(0x10000u), false}}));
    // `ip dscp af11` -> 10
    CHECK(std::get<DscpMatch>(p.rules[12].matches[1]).value == 10);
    // sport range
    CHECK(std::get<PortMatch>(p.rules[13].matches[1]).ranges ==
          std::vector<PortRange>{{1024, 65535}});
    // default-gateway bypass is a negated address list, sorted
    CHECK(p.rules[14].matches ==
          std::vector<PhysicalMatch>{
              AddrMatch{PhysicalDir::dst, true, {"10.0.0.0/8", "192.168.0.0/16"}}});
    CHECK(p.rules[15].family == Fam::ipv6);
  }

  SUBCASE("balance: numgen inc vmap after a mark guard") {
    VmapStmt vmap;
    vmap.key = PhysicalVmapKey::numgen_inc;
    vmap.param = 2;
    vmap.entries = {{0, setter(0x10000u)}, {1, setter(0x20000u)}};
    CHECK(p.rules[16] ==
          make_rule(Fam::ipv4,
                    {SetMatch{"kpbr4_hybrid", PhysicalDir::dst, false},
                     ProtoMatch{L4Proto::Udp},
                     MarkMatch{PhysicalMarkKind::packet, kMask, false, {0}}},
                    {vmap, VerdictStmt{PhysicalVerdict::accept}}));
    // `meta nfproto ipv6` is the family guard, not a match.
    const auto &v6 = p.rules[17];
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

  SUBCASE("output chain keeps companion and OUTPUT-only rules") {
    const auto &o = require_chain(set, out);
    REQUIRE(o.rules.size() == 5);
    CHECK(o.rules[3].matches ==
          std::vector<PhysicalMatch>{
              SetMatch{"kpbr4_hybrid", PhysicalDir::dst, false}});
    CHECK(o.rules[3].statements ==
          std::vector<PhysicalStatement>{VerdictStmt{PhysicalVerdict::drop}});
  }
}

TEST_CASE("physical: nft foreign and unknown expressions") {
  const auto set = parse_nft_json(read_fixture("nft_foreign.json"));
  const auto &p = require_chain(set, nft_id(Role::nft_prerouting));
  REQUIRE(p.rules.size() == 21);
  // The three rules appended with the stock nft tool sit at the end.
  const auto &foreign = p.rules[18];
  CHECK(foreign == make_rule(Fam::ipv4,
                             {AddrMatch{PhysicalDir::src, false, {"203.0.113.9/32"}}},
                             {VerdictStmt{PhysicalVerdict::accept}}));
  CHECK_FALSE(foreign.key.has_value());
  // `limit rate 1/second` is not understood: explicit unknown statement.
  REQUIRE(p.rules[19].statements.size() == 2);
  CHECK(std::holds_alternative<UnknownStmt>(p.rules[19].statements[0]));
  CHECK(p.rules[19] != p.rules[19]);
  // Malformed comment -> keyless, still parsed.
  CHECK(p.rules[20].family == Fam::ipv4);
  CHECK_FALSE(p.rules[20].key.has_value());
  CHECK(p.rules[20].statements ==
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
         SetMatch{"kpbr4s_x", PhysicalDir::dst, false}},
        {VerdictStmt{PhysicalVerdict::drop}, VerdictStmt{PhysicalVerdict::accept}});
    canonicalize_physical_rule(rule);
    CHECK(rule.matches ==
          std::vector<PhysicalMatch>{
              SetMatch{"kpbr4s_x", PhysicalDir::dst, false},
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
    PhysicalChainId a = ipt_id(Role::prerouting_generation, Table::mangle,
                               Fam::ipv4, Gen::a);
    PhysicalChainId b = a;
    a.name = "KeenPbrTable_A";
    b.name = "renamed";
    CHECK(a == b);
    b.generation = Gen::b;
    CHECK(a != b);
    b = a;
    b.table = Table::raw;
    CHECK(a != b);
  }
}


// ---------------------------------------------------------------------------
// Round trip: lowering the plans that produced the committed kernel dumps
// must give exactly what the parsers read back from those dumps.
// ---------------------------------------------------------------------------

bool is_lowered_role(Role role) {
  return role == Role::prerouting_generation ||
         role == Role::output_generation || role == Role::nft_prerouting ||
         role == Role::nft_output || role == Role::nft_setter;
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
  // The A generation names static sets kpbr4s_* / kpbr6s_*.
  context.physical_set_name = [backend](const std::string &name) {
    if (backend == FirewallBackend::iptables &&
        (name.rfind("kpbr4_", 0) == 0 || name.rfind("kpbr6_", 0) == 0)) {
      return name.substr(0, 5) + "s" + name.substr(5);
    }
    return name;
  };
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

} // namespace
} // namespace keen_pbr3
