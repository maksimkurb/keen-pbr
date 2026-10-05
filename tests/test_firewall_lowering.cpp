#include <doctest/doctest.h>

#include "../src/firewall/firewall_lowering.hpp"
#include "../src/firewall/iptables.hpp"
#include "../src/firewall/nftables.hpp"
#include "firewall_fixtures.hpp"

#include <set>
#include <sstream>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace keen_pbr3 {
namespace {

using Fam = FirewallFamily;
using Role = PhysicalChainRole;

constexpr uint32_t kMask = 0x00FF0000u;
constexpr uint32_t kMark1 = 0x10000u;
constexpr uint32_t kMark2 = 0x20000u;
constexpr uint32_t kMark3 = 0x30000u;

FirewallRuleInstance make_rule(std::string module, std::string instance,
                               Fam family, FirewallRuleCriteria criteria,
                               FirewallRuleAction action,
                               FirewallHook hook = FirewallHook::prerouting) {
  FirewallRuleInstance rule;
  rule.key = {std::move(module), std::move(instance)};
  rule.family = family;
  rule.hook = hook;
  rule.criteria = std::move(criteria);
  rule.action = std::move(action);
  return rule;
}

FirewallRuleInstance mark_rule(std::string instance, Fam family,
                               FirewallRuleCriteria criteria,
                               uint32_t mark = kMark1) {
  return make_rule("route.mark", std::move(instance), family,
                   std::move(criteria), MarkAction{mark, kMask});
}

FirewallRuleCriteria for_set(const std::string &set) {
  FirewallRuleCriteria criteria;
  criteria.dst_set_name = set;
  return criteria;
}

FirewallPlan plan_of(std::vector<FirewallRuleInstance> rules) {
  FirewallPlan plan;
  plan.fwmark_mask = kMask;
  plan.rules = std::move(rules);
  return plan;
}

FirewallLoweringContext ipt_context() {
  FirewallLoweringContext context;
  context.backend = FirewallBackend::iptables;
  context.fwmark_mask = kMask;
  return context;
}

FirewallLoweringContext nft_context() {
  FirewallLoweringContext context;
  context.backend = FirewallBackend::nftables;
  context.fwmark_mask = kMask;
  return context;
}

PhysicalChainId ipt_chain(const std::string &name, PhysicalTable table,
                          Fam family) {
  return iptables_physical_chain_id(name, table, family);
}

const PhysicalChain &chain_of(const PhysicalRuleset &set,
                              const PhysicalChainId &id) {
  const PhysicalChain *chain = set.find(id);
  REQUIRE_MESSAGE(chain != nullptr, "missing chain " << id.name);
  return *chain;
}

// The PREROUTING classification chain of the mangle layout.
const PhysicalChain &gen_a(const PhysicalRuleset &set, Fam family) {
  return chain_of(set, ipt_chain("KeenPbrTable", PhysicalTable::mangle, family));
}

// The OUTPUT classification chain (mangle, both modes).
const PhysicalChain &out_chain(const PhysicalRuleset &set, Fam family) {
  return chain_of(set, ipt_chain("KeenPbrOutput", PhysicalTable::mangle, family));
}

const PhysicalChain &nft_pre(const PhysicalRuleset &set) {
  return chain_of(set, nft_physical_chain_id(Role::nft_prerouting));
}
const PhysicalChain &nft_out(const PhysicalRuleset &set) {
  return chain_of(set, nft_physical_chain_id(Role::nft_output));
}

PhysicalVerdict verdict_of(const PhysicalRule &rule) {
  return std::get<VerdictStmt>(rule.statements.back()).verdict;
}

bool has_match(const PhysicalRule &rule, const PhysicalMatch &match) {
  for (const auto &candidate : rule.matches) {
    if (candidate == match) return true;
  }
  return false;
}

template <typename T> const T *find_match(const PhysicalRule &rule) {
  for (const auto &match : rule.matches) {
    if (const auto *typed = std::get_if<T>(&match)) return typed;
  }
  return nullptr;
}

FirewallPlan prefilter_plan(bool restore, std::vector<std::string> inbound) {
  std::vector<FirewallRuleInstance> rules;
  if (restore) {
    rules.push_back(make_rule("prefilter.restore_conntrack_mark", "m", Fam::any,
                              {}, RestoreConntrackMarkAction{kMask}));
  }
  rules.push_back(make_rule("prefilter.skip_established_or_dnat", "d", Fam::any,
                            {}, SkipEstablishedOrDnatAction{}));
  rules.push_back(make_rule("prefilter.skip_marked_packets", "a", Fam::any, {},
                            SkipMarkedPacketsAction{}));
  if (!inbound.empty()) {
    rules.push_back(make_rule("prefilter.inbound_interface", "i", Fam::any, {},
                              InboundInterfaceFilterAction{std::move(inbound)}));
  }
  return plan_of(std::move(rules));
}

PhysicalRuleset with_route(FirewallPlan plan, FirewallRuleInstance route) {
  plan.rules.push_back(std::move(route));
  return lower_firewall_plan(plan, ipt_context());
}

} // namespace

// ===========================================================================
// iptables
// ===========================================================================

TEST_CASE("lowering iptables: family expansion") {
  SUBCASE("family any without a set lowers to both families") {
    const auto set = lower_firewall_plan(
        plan_of({mark_rule("a", Fam::any, {})}), ipt_context());
    REQUIRE(set.chains.size() == 4);
    CHECK(gen_a(set, Fam::ipv4).rules.size() == 2);
    CHECK(gen_a(set, Fam::ipv6).rules.size() == 2);
    CHECK(gen_a(set, Fam::ipv6).rules[0].family == Fam::ipv6);
  }
  SUBCASE("family any follows the declared family of its set") {
    auto plan = plan_of({mark_rule("a", Fam::any, for_set("kpbr6_x"))});
    plan.sets.push_back({"kpbr6_x", Fam::ipv6, 0});
    const auto set = lower_firewall_plan(plan, ipt_context());
    CHECK(gen_a(set, Fam::ipv4).rules.empty());
    CHECK(gen_a(set, Fam::ipv6).rules.size() == 2);
  }
  SUBCASE("explicit family and mixed address lists") {
    FirewallRuleCriteria criteria;
    criteria.dst_addr = {"192.0.2.1", "2001:db8::1"};
    const auto set = lower_firewall_plan(
        plan_of({mark_rule("a", Fam::any, criteria)}), ipt_context());
    const auto &v4 = gen_a(set, Fam::ipv4).rules.front();
    const auto &v6 = gen_a(set, Fam::ipv6).rules.front();
    CHECK(has_match(v4, AddrMatch{PhysicalDir::dst, false, {"192.0.2.1/32"}}));
    CHECK(has_match(v6, AddrMatch{PhysicalDir::dst, false, {"2001:db8::1/128"}}));
  }
  SUBCASE("multiple addresses become one rule each") {
    FirewallRuleCriteria criteria;
    criteria.dst_addr = {"192.0.2.1", "192.0.2.2"};
    const auto set = lower_firewall_plan(
        plan_of({mark_rule("a", Fam::ipv4, criteria)}), ipt_context());
    // MARK + RETURN per address.
    CHECK(gen_a(set, Fam::ipv4).rules.size() == 4);
  }
  SUBCASE("IPv6 disabled drops the family and its chain") {
    auto context = ipt_context();
    context.ipv6_enabled = false;
    const auto set = lower_firewall_plan(
        plan_of({mark_rule("a", Fam::any, {})}), context);
    REQUIRE(set.chains.size() == 2);
    for (const auto &chain : set.chains) {
      CHECK(chain.id.family == Fam::ipv4);
    }
  }
}

TEST_CASE("lowering iptables: protocol and port expansion") {
  SUBCASE("port without protocol expands to tcp and udp") {
    FirewallRuleCriteria criteria;
    criteria.dst_port = "53";
    const auto set = lower_firewall_plan(
        plan_of({mark_rule("a", Fam::ipv4, criteria)}), ipt_context());
    const auto &rules = gen_a(set, Fam::ipv4).rules;
    REQUIRE(rules.size() == 4);
    CHECK(has_match(rules[0], ProtoMatch{L4Proto::Tcp}));
    CHECK(has_match(rules[2], ProtoMatch{L4Proto::Udp}));
  }
  SUBCASE("a long list is split in chunks of 15 slots, negated stays one rule") {
    FirewallRuleCriteria criteria;
    criteria.proto = L4Proto::Tcp;
    for (int port = 1; port <= 32; port += 2) {
      criteria.dst_port.ranges.push_back(
          {static_cast<uint16_t>(port), static_cast<uint16_t>(port)});
    }
    auto positive = criteria;
    const auto set = lower_firewall_plan(
        plan_of({make_rule("route.drop", "a", Fam::ipv4, positive,
                           VerdictAction::drop)}),
        ipt_context());
    CHECK(gen_a(set, Fam::ipv4).rules.size() == 2);
    criteria.negate_dst_port = true;
    const auto negated = lower_firewall_plan(
        plan_of({make_rule("route.drop", "a", Fam::ipv4, criteria,
                           VerdictAction::drop)}),
        ipt_context());
    const auto &rules = gen_a(negated, Fam::ipv4).rules;
    REQUIRE(rules.size() == 1);
    std::size_t port_matches = 0;
    for (const auto &match : rules[0].matches) {
      port_matches += std::holds_alternative<PortMatch>(match) ? 1U : 0U;
    }
    CHECK(port_matches == 2);
  }
  SUBCASE("adjacent ports are merged before they are split") {
    FirewallRuleCriteria criteria;
    criteria.proto = L4Proto::Udp;
    criteria.dst_port = "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16";
    const auto set = lower_firewall_plan(
        plan_of({make_rule("route.pass", "a", Fam::ipv4, criteria,
                           VerdictAction::pass)}),
        ipt_context());
    const auto &rules = gen_a(set, Fam::ipv4).rules;
    REQUIRE(rules.size() == 1);
    CHECK(has_match(rules[0], PortMatch{PhysicalTransport::udp,
                                        PhysicalDir::dst, false, {{1, 16}}}));
  }
}

TEST_CASE("lowering iptables: mark, drop and pass shapes") {
  FirewallRuleCriteria criteria = for_set("kpbr4s_x");
  const auto set = lower_firewall_plan(
      plan_of({mark_rule("m", Fam::ipv4, criteria),
               make_rule("route.drop", "d", Fam::ipv4, criteria,
                         VerdictAction::drop),
               make_rule("route.pass", "p", Fam::ipv4, criteria,
                         VerdictAction::pass)}),
      ipt_context());
  const auto &rules = gen_a(set, Fam::ipv4).rules;
  REQUIRE(rules.size() == 4);
  CHECK(rules[0].statements ==
        std::vector<PhysicalStatement>{
            SetMarkStmt{PhysicalMarkKind::packet, kMark1, kMask}});
  CHECK(verdict_of(rules[1]) == PhysicalVerdict::return_);
  CHECK(verdict_of(rules[2]) == PhysicalVerdict::drop);
  CHECK(verdict_of(rules[3]) == PhysicalVerdict::return_);
  CHECK(rules[0].key->module_id == "route.mark");
  CHECK(rules[2].key->module_id == "route.drop");

  SUBCASE("a mark with bits outside its mask is stored inside it") {
    const auto wide = lower_firewall_plan(
        plan_of({mark_rule("w", Fam::ipv4, {}, 0x1000001u)}), ipt_context());
    CHECK(std::get<SetMarkStmt>(gen_a(wide, Fam::ipv4).rules[0].statements[0])
              .mask == (kMask | 0x1000001u));
  }
  SUBCASE("default gateway is nft only") {
    FirewallRuleCriteria gateway;
    gateway.default_gateway = DefaultGatewayFamily::Ipv4;
    CHECK_THROWS_AS(lower_firewall_plan(
                        plan_of({mark_rule("g", Fam::ipv4, gateway)}),
                        ipt_context()),
                    FirewallError);
  }
}

TEST_CASE("lowering iptables: prefilters") {
  SUBCASE("mangle chain: restore pair, dnat, skip marked, single inbound") {
    const auto set = lower_firewall_plan(prefilter_plan(true, {"lan0"}),
                                         ipt_context());
    const auto &rules = gen_a(set, Fam::ipv4).rules;
    REQUIRE(rules.size() == 5);
    CHECK(rules[0].statements ==
          std::vector<PhysicalStatement>{CopyMarkStmt{false, kMask, kMask}});
    CHECK(has_match(rules[0], CtDirMatch{true}));
    CHECK(verdict_of(rules[1]) == PhysicalVerdict::return_);
    CHECK(has_match(rules[2], CtStateMatch{ct_dnat, false}));
    CHECK(verdict_of(rules[3]) == PhysicalVerdict::accept);
    CHECK(has_match(rules[4], IifMatch{true, {"lan0"}}));
  }
  SUBCASE("raw PREROUTING has no conntrack prefilter, OUTPUT stays mangle") {
    auto context = ipt_context();
    context.raw_prerouting = RawPreroutingMode{true, false};
    context.ipv6_enabled = false;
    const auto set = lower_firewall_plan(prefilter_plan(true, {"lan0"}), context);
    REQUIRE(set.chains.size() == 2);
    const auto &raw = chain_of(
        set, ipt_chain("KeenPbrRaw", PhysicalTable::raw, Fam::ipv4));
    const auto &output = chain_of(
        set, ipt_chain("KeenPbrOutput", PhysicalTable::mangle, Fam::ipv4));
    CHECK(raw.rules.size() == 2); // skip marked, inbound
    CHECK(output.rules.size() == 4); // restore pair, dnat, skip marked (no inbound)
  }
  SUBCASE("only the family in raw mode moves") {
    auto context = ipt_context();
    context.raw_prerouting = RawPreroutingMode{false, true};
    const auto set = lower_firewall_plan(prefilter_plan(false, {}), context);
    CHECK(set.chains.size() == 4);
    CHECK(set.find(ipt_chain("KeenPbrTable", PhysicalTable::mangle,
                             Fam::ipv4)) != nullptr);
    CHECK(set.find(ipt_chain("KeenPbrRaw", PhysicalTable::raw, Fam::ipv6)) !=
          nullptr);
  }
  SUBCASE("conntrack save follows each mark in mangle but not in raw") {
    auto plan = prefilter_plan(true, {});
    plan.rules.push_back(mark_rule("a", Fam::ipv4, {}));
    const auto mangle = lower_firewall_plan(plan, ipt_context());
    const auto &rules = gen_a(mangle, Fam::ipv4).rules;
    REQUIRE(rules.size() == 7);
    CHECK(rules[5].statements ==
          std::vector<PhysicalStatement>{CopyMarkStmt{true, kMask, kMask}});
    auto context = ipt_context();
    context.raw_prerouting = RawPreroutingMode{true, true};
    const auto raw = lower_firewall_plan(plan, context);
    CHECK(chain_of(raw, ipt_chain("KeenPbrRaw", PhysicalTable::raw, Fam::ipv4))
              .rules.size() == 3);
  }
  SUBCASE("multiple interfaces become per-interface fragments") {
    const auto set = with_route(prefilter_plan(false, {"lan0", "br-guest"}),
                                mark_rule("a", Fam::ipv4, for_set("kpbr4s_x")));
    const auto &rules = gen_a(set, Fam::ipv4).rules;
    // dnat, skip marked, then (MARK, RETURN) per interface; no `! -i` rule.
    REQUIRE(rules.size() == 6);
    for (const std::size_t index : {2U, 3U}) {
      CHECK(has_match(rules[index], IifMatch{false, {"lan0"}}));
    }
    for (const std::size_t index : {4U, 5U}) {
      CHECK(has_match(rules[index], IifMatch{false, {"br-guest"}}));
    }
  }
}

TEST_CASE("lowering iptables: PREROUTING and OUTPUT placement") {
  const auto routes = [] {
    FirewallRuleCriteria detour = for_set("kpbr4s_dns");
    return plan_of({make_rule("route.mark", "route", Fam::ipv4,
                              for_set("kpbr4s_x"), MarkAction{kMark1, kMask}),
                    make_rule("dns.detour", "detour", Fam::ipv4, detour,
                              MarkAction{kMark2, kMask}, FirewallHook::output),
                    make_rule("route.drop", "last", Fam::ipv4,
                              for_set("kpbr4s_y"), VerdictAction::drop)});
  };
  const auto instance_ids = [](const PhysicalChain &chain) {
    std::vector<std::string> ids;
    for (const auto &rule : chain.rules) {
      if (ids.empty() || ids.back() != rule.key->instance_id) {
        ids.push_back(rule.key->instance_id);
      }
    }
    return ids;
  };

  SUBCASE("route rules go to both chains, DNS detour only to OUTPUT") {
    const auto set = lower_firewall_plan(routes(), ipt_context());
    CHECK(instance_ids(gen_a(set, Fam::ipv4)) ==
          std::vector<std::string>{"route", "last"});
    // Plan order is preserved inside each chain.
    CHECK(instance_ids(out_chain(set, Fam::ipv4)) ==
          std::vector<std::string>{"route", "detour", "last"});
    CHECK(out_chain(set, Fam::ipv4).rules.size() == 5);
  }
  SUBCASE("raw mode: raw PREROUTING and mangle OUTPUT place the same way") {
    auto context = ipt_context();
    context.raw_prerouting = RawPreroutingMode{true, true};
    const auto set = lower_firewall_plan(routes(), context);
    CHECK(instance_ids(chain_of(set, ipt_chain("KeenPbrRaw", PhysicalTable::raw,
                                               Fam::ipv4))) ==
          std::vector<std::string>{"route", "last"});
    CHECK(instance_ids(out_chain(set, Fam::ipv4)) ==
          std::vector<std::string>{"route", "detour", "last"});
    CHECK(set.chains.size() == 4);
  }
  SUBCASE("IPv6 disabled leaves only the IPv4 pair") {
    auto context = ipt_context();
    context.ipv6_enabled = false;
    const auto set = lower_firewall_plan(routes(), context);
    CHECK(set.chains.size() == 2);
  }
  SUBCASE("single inbound filter is PREROUTING only") {
    const auto set = with_route(prefilter_plan(false, {"lan0"}),
                                mark_rule("a", Fam::ipv4, for_set("kpbr4s_x")));
    bool pre = false;
    for (const auto &rule : gen_a(set, Fam::ipv4).rules) {
      pre = pre || find_match<IifMatch>(rule) != nullptr;
    }
    CHECK(pre);
    for (const auto &rule : out_chain(set, Fam::ipv4).rules) {
      CHECK(find_match<IifMatch>(rule) == nullptr);
    }
    // dnat, skip marked, MARK, RETURN.
    CHECK(out_chain(set, Fam::ipv4).rules.size() == 4);
  }
  SUBCASE("multi-interface fragments are PREROUTING only") {
    const auto set = with_route(prefilter_plan(false, {"lan0", "br-guest"}),
                                mark_rule("a", Fam::ipv4, for_set("kpbr4s_x")));
    for (const auto &rule : out_chain(set, Fam::ipv4).rules) {
      CHECK(find_match<IifMatch>(rule) == nullptr);
    }
    // dnat, skip marked, MARK, RETURN: one unfragmented classifier.
    CHECK(out_chain(set, Fam::ipv4).rules.size() == 4);
    CHECK(gen_a(set, Fam::ipv4).rules.size() == 6);
  }
  SUBCASE("restore and DNAT skip are not in raw PREROUTING but are in OUTPUT") {
    auto context = ipt_context();
    context.raw_prerouting = RawPreroutingMode{true, true};
    const auto set = lower_firewall_plan(prefilter_plan(true, {"lan0"}), context);
    const auto &raw =
        chain_of(set, ipt_chain("KeenPbrRaw", PhysicalTable::raw, Fam::ipv4));
    for (const auto &rule : raw.rules) {
      CHECK(find_match<CtStateMatch>(rule) == nullptr);
      CHECK(find_match<CtDirMatch>(rule) == nullptr);
    }
    CHECK(raw.rules.size() == 2); // skip marked, inbound
    // restore pair, dnat, skip marked.
    CHECK(out_chain(set, Fam::ipv4).rules.size() == 4);
    CHECK(out_chain(set, Fam::ipv6).rules.size() == 4);
  }
}

TEST_CASE("lowering iptables: order, generations, comments") {
  SUBCASE("overlapping sets keep the order of the plan") {
    const auto set = lower_firewall_plan(
        plan_of({mark_rule("first", Fam::ipv4, for_set("kpbr4s_x"), kMark1),
                 mark_rule("second", Fam::ipv4, for_set("kpbr4s_x"), kMark2),
                 make_rule("route.drop", "third", Fam::ipv4,
                           for_set("kpbr4s_x"), VerdictAction::drop)}),
        ipt_context());
    const auto &rules = gen_a(set, Fam::ipv4).rules;
    REQUIRE(rules.size() == 5);
    CHECK(rules[0].key->instance_id == "first");
    CHECK(rules[2].key->instance_id == "second");
    CHECK(rules[4].key->instance_id == "third");
  }
  SUBCASE("each family has a PREROUTING and an OUTPUT chain, no generations") {
    const auto set =
        lower_firewall_plan(plan_of({mark_rule("a", Fam::any, {})}), ipt_context());
    CHECK(set.chains.size() == 4);
    for (const auto family : {Fam::ipv4, Fam::ipv6}) {
      CHECK(set.find(ipt_chain("KeenPbrTable", PhysicalTable::mangle, family)) !=
            nullptr);
      CHECK(set.find(ipt_chain("KeenPbrOutput", PhysicalTable::mangle, family)) !=
            nullptr);
      CHECK(set.find(ipt_chain("KeenPbrTable_A", PhysicalTable::mangle,
                               family)) == nullptr);
    }
  }
  SUBCASE("keys are kept only where comments are supported") {
    auto context = ipt_context();
    context.comments_ipv6_supported = false;
    const auto set =
        lower_firewall_plan(plan_of({mark_rule("a", Fam::any, {})}), context);
    CHECK(gen_a(set, Fam::ipv4).rules[0].key.has_value());
    CHECK_FALSE(gen_a(set, Fam::ipv6).rules[0].key.has_value());
  }
  SUBCASE("set names are resolved to physical names") {
    auto context = ipt_context();
    context.physical_set_name = [](const std::string &name) {
      return "phys_" + name;
    };
    const auto set = lower_firewall_plan(
        plan_of({mark_rule("a", Fam::ipv4, for_set("kpbr4_x"))}), context);
    CHECK(has_match(gen_a(set, Fam::ipv4).rules[0],
                    SetMatch{"phys_kpbr4_x", PhysicalDir::dst, false}));
  }
  SUBCASE("every rule is canonical") {
    FirewallRuleCriteria criteria;
    criteria.proto = L4Proto::Tcp;
    criteria.dst_port = "443";
    criteria.dst_addr = {"10.0.0.7"};
    criteria.dscp = 46;
    const auto set = lower_firewall_plan(
        plan_of({mark_rule("a", Fam::ipv4, criteria)}), ipt_context());
    for (const auto &rule : gen_a(set, Fam::ipv4).rules) {
      auto again = rule;
      canonicalize_physical_rule(again);
      CHECK(again == rule);
    }
  }
}

// ===========================================================================
// nftables
// ===========================================================================

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("lowering nftables: chains and mark shapes") {
  const auto set = lower_firewall_plan(
      plan_of({mark_rule("m", Fam::ipv4, for_set("kpbr4_x"))}), nft_context());
  REQUIRE(set.chains.size() == 3);
  CHECK(nft_pre(set).base ==
        PhysicalBaseChain{PhysicalBaseChain::Type::filter,
                          PhysicalBaseChain::Hook::prerouting, -150, true});
  CHECK(nft_out(set).base ==
        PhysicalBaseChain{PhysicalBaseChain::Type::route,
                          PhysicalBaseChain::Hook::output, -150, true});
  // Route rules appear in both chains
  REQUIRE(nft_out(set).rules.size() == 1);
  CHECK(nft_out(set).rules[0] == nft_pre(set).rules[0]);

  SUBCASE("without restore the mark is set inline") {
    REQUIRE(nft_pre(set).rules.size() == 1);
    const auto &rule = nft_pre(set).rules[0];
    CHECK(rule.family == Fam::ipv4);
    CHECK(rule.statements ==
          std::vector<PhysicalStatement>{
              SetMarkStmt{PhysicalMarkKind::packet, kMark1, kMask | kMark1},
              VerdictStmt{PhysicalVerdict::accept}});
  }
  SUBCASE("a setter chain exists for the mark") {
    const auto &setter =
        chain_of(set, nft_physical_chain_id(Role::nft_setter, kMark1));
    REQUIRE(setter.rules.size() == 1);
    CHECK(setter.rules[0].statements.size() == 3);
    CHECK(setter.id.name == "setmark_00010000");
  }
  SUBCASE("with restore, marks jump to their setter") {
    auto plan = plan_of({});
    plan.rules.push_back(make_rule("prefilter.restore_conntrack_mark", "m",
                                   Fam::any, {},
                                   RestoreConntrackMarkAction{kMask}));
    plan.rules.push_back(mark_rule("m", Fam::ipv4, for_set("kpbr4_x")));
    const auto restored = lower_firewall_plan(plan, nft_context());
    const auto &pre = nft_pre(restored);
    REQUIRE(pre.rules.size() == 2);
    const auto &vmap = std::get<VmapStmt>(pre.rules[0].statements[0]);
    CHECK(vmap.key == PhysicalVmapKey::conntrack_mark_and);
    CHECK(vmap.entries.size() == 1);
    CHECK(std::holds_alternative<LateMatchStmt>(pre.rules[0].statements[1]));
    CHECK(pre.rules[1].statements ==
          std::vector<PhysicalStatement>{JumpStmt{
              nft_physical_chain_id(Role::nft_setter, kMark1), false}});
    // The restore rule and route rule both appear in OUTPUT
    REQUIRE(nft_out(restored).rules.size() == 2);
    CHECK(nft_out(restored).rules[0] == pre.rules[0]);
    CHECK(nft_out(restored).rules[1] == pre.rules[1]);
  }
  SUBCASE("owned marks always get a setter, restore needs at least one") {
    auto context = nft_context();
    context.owned_marks = {kMark2, 0};
    auto plan = plan_of({make_rule("prefilter.restore_conntrack_mark", "m",
                                   Fam::any, {},
                                   RestoreConntrackMarkAction{kMask})});
    const auto restored = lower_firewall_plan(plan, context);
    CHECK(restored.find(nft_physical_chain_id(Role::nft_setter, kMark2)) !=
          nullptr);
    CHECK(restored.chains.size() == 3);
    CHECK(nft_pre(lower_firewall_plan(plan, nft_context())).rules.empty());
  }
}
#endif

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("lowering nftables: family handling") {
  SUBCASE("a payload match implies the family, a port-only rule has none") {
    FirewallRuleCriteria ports;
    ports.dst_port = "53";
    const auto set = lower_firewall_plan(
        plan_of({mark_rule("a", Fam::ipv4, ports),
                 mark_rule("b", Fam::ipv6, for_set("kpbr6_x"))}),
        nft_context());
    CHECK(nft_pre(set).rules[0].family == Fam::any);
    CHECK(nft_pre(set).rules[1].family == Fam::ipv6);
  }
  SUBCASE("family any without a family selector is one family-neutral rule") {
    FirewallRuleCriteria criteria;
    criteria.proto = L4Proto::Tcp;
    const auto set = lower_firewall_plan(
        plan_of({mark_rule("a", Fam::any, criteria)}), nft_context());
    REQUIRE(nft_pre(set).rules.size() == 1);
    CHECK(nft_pre(set).rules[0].family == Fam::any);
  }
  SUBCASE("a set follows its declared family, IPv6 can be disabled") {
    auto plan = plan_of({mark_rule("a", Fam::any, for_set("kpbr6_x"))});
    plan.sets.push_back({"kpbr6_x", Fam::ipv6, 0});
    CHECK(nft_pre(lower_firewall_plan(plan, nft_context())).rules.size() == 1);
    auto context = nft_context();
    context.ipv6_enabled = false;
    CHECK(nft_pre(lower_firewall_plan(plan, context)).rules.empty());
  }
  SUBCASE("proto tcp/udp expands to two rules") {
    FirewallRuleCriteria criteria;
    criteria.proto = L4Proto::TcpUdp;
    criteria.dst_addr = {"192.0.2.1"};
    const auto set = lower_firewall_plan(
        plan_of({mark_rule("a", Fam::ipv4, criteria)}), nft_context());
    CHECK(nft_pre(set).rules.size() == 2);
  }
}
#endif

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("lowering nftables: route rules in both chains and OUTPUT-only rules") {
  FirewallRuleCriteria criteria;
  criteria.default_gateway = DefaultGatewayFamily::Ipv4;
  criteria.default_gateway_bypass = {"192.168.0.0/16", "10.0.0.0/8"};
  const auto set = lower_firewall_plan(
      plan_of({mark_rule("g", Fam::ipv4, criteria)}), nft_context());
  REQUIRE(nft_out(set).rules.size() == 1);
  REQUIRE(nft_pre(set).rules.size() == 1);
  CHECK(nft_out(set).rules[0] == nft_pre(set).rules[0]);
  CHECK(has_match(nft_out(set).rules[0],
                  AddrMatch{PhysicalDir::dst, true,
                            {"10.0.0.0/8", "192.168.0.0/16"}}));

  SUBCASE("a gateway of the other family produces nothing") {
    const auto none = lower_firewall_plan(
        plan_of({mark_rule("g", Fam::ipv6, criteria)}), nft_context());
    CHECK(nft_out(none).rules.empty());
    CHECK(nft_pre(none).rules.empty());
  }
  SUBCASE("an OUTPUT-only rule (hook=output) has no companion in prerouting") {
    FirewallRuleCriteria only_output = for_set("kpbr4d_x");
    const auto lowered = lower_firewall_plan(
        plan_of({make_rule("route.drop", "o", Fam::ipv4, only_output,
                           VerdictAction::drop, FirewallHook::output)}),
        nft_context());
    CHECK(nft_pre(lowered).rules.empty());
    REQUIRE(nft_out(lowered).rules.size() == 1);
    CHECK(verdict_of(nft_out(lowered).rules[0]) == PhysicalVerdict::drop);
  }
}
#endif

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("lowering: process_router_traffic=false keeps route rules out of OUTPUT") {
  auto plan = plan_of({mark_rule("route", Fam::ipv4, for_set("kpbr4_x")),
                       make_rule("dns.detour", "detour", Fam::ipv4,
                                 for_set("kpbr4d_dns"),
                                 MarkAction{kMark2, kMask},
                                 FirewallHook::output)});
  const auto instance_ids = [](const PhysicalChain &chain) {
    std::vector<std::string> ids;
    for (const auto &rule : chain.rules) {
      if (ids.empty() || ids.back() != rule.key->instance_id) {
        ids.push_back(rule.key->instance_id);
      }
    }
    return ids;
  };
  const std::vector<std::string> detour_only{"detour"};
  const std::vector<std::string> route_and_detour{"route", "detour"};

  plan.process_router_traffic = false;
  SUBCASE("nftables") {
    const auto set = lower_firewall_plan(plan, nft_context());
    CHECK(instance_ids(nft_pre(set)) == std::vector<std::string>{"route"});
    CHECK(instance_ids(nft_out(set)) == detour_only);
  }
  SUBCASE("iptables") {
    const auto set = lower_firewall_plan(plan, ipt_context());
    CHECK(instance_ids(gen_a(set, Fam::ipv4)) ==
          std::vector<std::string>{"route"});
    CHECK(instance_ids(out_chain(set, Fam::ipv4)) == detour_only);
  }
  plan.process_router_traffic = true;
  SUBCASE("nftables, router traffic processed") {
    const auto set = lower_firewall_plan(plan, nft_context());
    CHECK(instance_ids(nft_out(set)) == route_and_detour);
  }
  SUBCASE("iptables, router traffic processed") {
    const auto set = lower_firewall_plan(plan, ipt_context());
    CHECK(instance_ids(out_chain(set, Fam::ipv4)) == route_and_detour);
  }
}
#endif

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("lowering nftables: balance") {
  const auto balance = [](std::vector<FirewallBalanceCandidate> candidates,
                          FirewallRuleCriteria criteria = {}) {
    return make_rule("route.balance", "b", Fam::any, std::move(criteria),
                     BalanceAction{kMark3, std::move(candidates)});
  };

  SUBCASE("several candidates: numgen vmap behind an unmarked guard") {
    FirewallRuleCriteria criteria = for_set("kpbr4_x");
    const auto set = lower_firewall_plan(
        plan_of({balance({{kMark1, true, false}, {kMark2, true, false}},
                         criteria)}),
        nft_context());
    REQUIRE(nft_pre(set).rules.size() == 1);
    const auto &rule = nft_pre(set).rules[0];
    CHECK(rule.family == Fam::ipv4);
    CHECK(has_match(rule, MarkMatch{PhysicalMarkKind::packet, kMask, false, {0}}));
    const auto &vmap = std::get<VmapStmt>(rule.statements[0]);
    CHECK(vmap.key == PhysicalVmapKey::numgen_inc);
    CHECK(vmap.param == 2);
    CHECK(vmap.entries[1].second ==
          nft_physical_chain_id(Role::nft_setter, kMark2));
    CHECK(verdict_of(rule) == PhysicalVerdict::accept);
  }
  SUBCASE("families are filtered independently") {
    const auto set = lower_firewall_plan(
        plan_of({balance({{kMark1, true, false},
                          {kMark2, true, false},
                          {0x40000u, false, true}})}),
        nft_context());
    // IPv4 balances over two marks, IPv6 has a single candidate: plain mark,
    // still guarded by its family.
    REQUIRE(nft_pre(set).rules.size() == 2);
    CHECK(std::holds_alternative<VmapStmt>(nft_pre(set).rules[0].statements[0]));
    const auto &v6 = nft_pre(set).rules[1];
    CHECK(v6.family == Fam::ipv6);
    CHECK(v6.statements[0] ==
          PhysicalStatement{
              SetMarkStmt{PhysicalMarkKind::packet, 0x40000u, kMask | 0x40000u}});
    for (const uint32_t mark : {kMark1, kMark2, 0x40000u}) {
      CHECK(set.find(nft_physical_chain_id(Role::nft_setter, mark)) != nullptr);
    }
  }
  SUBCASE("no candidate falls back to the fallback mark") {
    const auto set = lower_firewall_plan(plan_of({balance({})}), nft_context());
    REQUIRE(nft_pre(set).rules.size() == 2);
    CHECK(set.find(nft_physical_chain_id(Role::nft_setter, kMark3)) != nullptr);
    CHECK(nft_pre(set).rules[0].family == Fam::ipv4);
    CHECK(nft_pre(set).rules[1].family == Fam::ipv6);
  }
}
#endif

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("lowering nftables: prefilters") {
  auto plan = prefilter_plan(true, {"wg0", "lan0"});
  plan.rules.push_back(mark_rule("a", Fam::ipv4, for_set("kpbr4_x")));
  const auto set = lower_firewall_plan(plan, nft_context());
  const auto &pre = nft_pre(set).rules;
  const auto &out = nft_out(set).rules;
  // pre: restore, dnat, skip marked, inbound, route.  out: restore, dnat (now in both), skip marked, route.
  REQUIRE(pre.size() == 5);
  REQUIRE(out.size() == 4);
  CHECK(has_match(pre[1], CtStateMatch{ct_dnat, false}));
  CHECK(has_match(pre[3], IifMatch{true, {"lan0", "wg0"}}));
  CHECK(verdict_of(pre[3]) == PhysicalVerdict::accept);
  CHECK(has_match(out[2], MarkMatch{PhysicalMarkKind::packet, 0xFFFFFFFFu, true,
                                    {0}}));
  CHECK(pre[0] == out[0]);
  // DNAT skip appears in both chains
  CHECK(pre[1] == out[1]);
  // Route rule appears in both chains
  CHECK(pre[4] == out[3]);
}
#endif

// ===========================================================================
// nftables placement tests (a-e from brief)
// ===========================================================================

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("nftables lowering: route mark rule appears in both prerouting and output") {
  // Test (a): nft route mark rule in both prerouting and output
  const auto set = lower_firewall_plan(
      plan_of({mark_rule("route", Fam::ipv4, for_set("kpbr4_x"), kMark1)}),
      nft_context());
  CHECK(nft_pre(set).rules.size() == 1);
  CHECK(nft_out(set).rules.size() == 1);
  CHECK(nft_pre(set).rules[0] == nft_out(set).rules[0]);
}
#endif

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("nftables lowering: hook=output rule appears only in output chain") {
  // Test (b): nft hook=output rule only in output
  const auto set = lower_firewall_plan(
      plan_of({make_rule("dns.detour", "output_only", Fam::ipv4,
                         for_set("kpbr4_dns"), MarkAction{kMark1, kMask},
                         FirewallHook::output)}),
      nft_context());
  CHECK(nft_pre(set).rules.empty());
  CHECK(nft_out(set).rules.size() == 1);
}
#endif

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("nftables lowering: inbound interface filter only in prerouting") {
  // Test (c): nft inbound filter only in prerouting
  auto plan = plan_of({});
  plan.rules.push_back(make_rule("prefilter.inbound_interface", "iif",
                                 Fam::any, {},
                                 InboundInterfaceFilterAction{{"lan0"}}));
  const auto set = lower_firewall_plan(plan, nft_context());
  CHECK(nft_pre(set).rules.size() == 1);
  CHECK(has_match(nft_pre(set).rules[0], IifMatch{true, {"lan0"}}));
  CHECK(nft_out(set).rules.empty());
}
#endif

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("nftables lowering: DNAT skip in both chains") {
  // Test (d): nft DNAT skip in both prerouting and output
  auto plan = plan_of({});
  plan.rules.push_back(make_rule("prefilter.skip_established_or_dnat", "dnat",
                                 Fam::any, {},
                                 SkipEstablishedOrDnatAction{}));
  const auto set = lower_firewall_plan(plan, nft_context());
  CHECK(nft_pre(set).rules.size() == 1);
  CHECK(nft_out(set).rules.size() == 1);
  CHECK(has_match(nft_pre(set).rules[0], CtStateMatch{ct_dnat, false}));
  CHECK(has_match(nft_out(set).rules[0], CtStateMatch{ct_dnat, false}));
}
#endif

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("nftables lowering: reply skip in prerouting and output") {
  auto plan = plan_of({});
  plan.rules.push_back(make_rule("prefilter.skip_local_replies", "reply",
                                 Fam::any, {}, SkipLocalRepliesAction{}));
  const auto set = lower_firewall_plan(plan, nft_context());
  for (const auto *chain : {&nft_pre(set), &nft_out(set)}) {
    REQUIRE(chain->rules.size() == 1);
    CHECK(has_match(chain->rules[0], CtDirMatch{false}));
    CHECK(verdict_of(chain->rules[0]) == PhysicalVerdict::accept);
  }
}
#endif

TEST_CASE("iptables lowering: reply skip in PREROUTING (mangle) and OUTPUT") {
  auto plan = plan_of({});
  plan.rules.push_back(make_rule("prefilter.skip_local_replies", "reply",
                                 Fam::any, {}, SkipLocalRepliesAction{}));
  const auto set = lower_firewall_plan(plan, ipt_context());
  for (const Fam family : {Fam::ipv4, Fam::ipv6}) {
    for (const auto *chain : {&gen_a(set, family), &out_chain(set, family)}) {
      REQUIRE(chain->rules.size() == 1);
      const auto &rule = chain->rules[0];
      CHECK(has_match(rule, CtDirMatch{false}));
      CHECK(verdict_of(rule) == PhysicalVerdict::return_);
    }
  }
  const auto rendered = render_iptables_rule(out_chain(set, Fam::ipv4).rules[0],
                                             "KeenPbrOutput");
  CHECK(rendered == "-A KeenPbrOutput -m conntrack --ctdir REPLY -m comment --comment "
                  "kpbr:v1:prefilter.skip_local_replies:reply -j RETURN\n");
}

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("lowering: catch-all destinations are not emitted") {
  FirewallRuleCriteria v4;
  v4.proto = L4Proto::Udp;
  v4.dst_addr = {"0.0.0.0/0"};
  FirewallRuleCriteria halves = v4;
  halves.dst_addr = {"0.0.0.0/1", "128.0.0.0/1"};
  FirewallRuleCriteria v6;
  v6.dst_addr = {"::/0"};
  const auto plan = plan_of({mark_rule("a", Fam::ipv4, v4, kMark1),
                             mark_rule("b", Fam::ipv4, halves, kMark1),
                             mark_rule("c", Fam::ipv6, v6, kMark1)});
  SUBCASE("iptables") {
    const auto set = lower_firewall_plan(plan, ipt_context());
    for (const auto &rule : gen_a(set, Fam::ipv4).rules) {
      CHECK(find_match<AddrMatch>(rule) == nullptr);
    }
    for (const auto &rule : gen_a(set, Fam::ipv6).rules) {
      CHECK(find_match<AddrMatch>(rule) == nullptr);
    }
    const auto text =
        render_iptables_rule(gen_a(set, Fam::ipv4).rules[0], "KeenPbrTable");
    CHECK(text.find("-d ") == std::string::npos);
  }
  SUBCASE("nftables keeps the family as an nfproto guard") {
    const auto set = lower_firewall_plan(plan, nft_context());
    const auto &rules = nft_pre(set).rules;
    REQUIRE(rules.size() >= 3);
    for (const auto &rule : rules) {
      CHECK(find_match<AddrMatch>(rule) == nullptr);
    }
    CHECK(rules[0].family == Fam::ipv4);
    CHECK(rules[1].family == Fam::ipv4);
    CHECK(rules[2].family == Fam::ipv6);
    const auto json = render_nft_rule(nft_pre(set).id, rules[0]).dump();
    CHECK(json.find("nfproto") != std::string::npos);
    CHECK(json.find("daddr") == std::string::npos);
  }
  SUBCASE("a negated catch-all is rejected") {
    FirewallRuleCriteria never = v4;
    never.negate_dst_addr = true;
    CHECK_THROWS_AS(lower_firewall_plan(
                        plan_of({mark_rule("n", Fam::ipv4, never, kMark1)}),
                        ipt_context()),
                    FirewallError);
  }
}
#endif

TEST_CASE("lowering: reply skip is absent from raw PREROUTING") {
  auto plan = plan_of({});
  plan.rules.push_back(make_rule("prefilter.skip_local_replies", "reply",
                                 Fam::any, {}, SkipLocalRepliesAction{}));
  auto context = ipt_context();
  context.raw_prerouting = RawPreroutingMode{true, true};
  const auto set = lower_firewall_plan(plan, context);
  CHECK(chain_of(set, ipt_chain("KeenPbrRaw", PhysicalTable::raw, Fam::ipv4))
            .rules.empty());
  CHECK(out_chain(set, Fam::ipv4).rules.size() == 1);
}

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("nftables lowering: default_gateway route rule in both prerouting and output") {
  // Test (e): nft default_gateway route rule exactly once in prerouting and once in output
  FirewallRuleCriteria criteria;
  criteria.default_gateway = DefaultGatewayFamily::Ipv4;
  criteria.default_gateway_bypass = {"192.168.0.0/16"};
  const auto set = lower_firewall_plan(
      plan_of({mark_rule("gateway", Fam::ipv4, criteria, kMark1)}),
      nft_context());
  CHECK(nft_pre(set).rules.size() == 1);
  CHECK(nft_out(set).rules.size() == 1);
  CHECK(nft_pre(set).rules[0] == nft_out(set).rules[0]);
  CHECK(has_match(nft_pre(set).rules[0],
                  AddrMatch{PhysicalDir::dst, true, {"192.168.0.0/16"}}));
}
#endif

// ===========================================================================
// Emitters
// ===========================================================================

namespace {

PhysicalRule rule_of(Fam family, std::vector<PhysicalMatch> matches,
                     std::vector<PhysicalStatement> statements) {
  PhysicalRule rule;
  rule.family = family;
  rule.matches = std::move(matches);
  rule.statements = std::move(statements);
  canonicalize_physical_rule(rule);
  return rule;
}

} // namespace

TEST_CASE("iptables renderer: every match and statement kind") {
  const auto render = [](PhysicalRule rule) {
    return render_iptables_rule(rule, "C");
  };
  const auto accept = std::vector<PhysicalStatement>{
      VerdictStmt{PhysicalVerdict::accept}};

  CHECK(render(rule_of(Fam::ipv4, {}, {VerdictStmt{PhysicalVerdict::return_}})) ==
        "-A C -j RETURN\n");
  CHECK(render(rule_of(Fam::ipv4, {SetMatch{"s", PhysicalDir::dst, false}},
                       accept)) == "-A C -m set --match-set s dst -j ACCEPT\n");
  CHECK(render(rule_of(Fam::ipv4, {SetMatch{"s", PhysicalDir::src, true}},
                       accept)) ==
        "-A C -m set ! --match-set s src -j ACCEPT\n");
  CHECK(render(rule_of(Fam::ipv4, {IifMatch{true, {"lan0"}}}, accept)) ==
        "-A C ! -i lan0 -j ACCEPT\n");
  CHECK_THROWS_AS(render(rule_of(Fam::ipv4, {IifMatch{false, {"a", "b"}}},
                                 accept)),
                  FirewallError);
  CHECK(render(rule_of(Fam::ipv4,
                       {AddrMatch{PhysicalDir::src, true, {"10.0.0.0/8"}},
                        AddrMatch{PhysicalDir::dst, false, {"192.0.2.1"}}},
                       accept)) ==
        "-A C ! -s 10.0.0.0/8 -d 192.0.2.1/32 -j ACCEPT\n");
  CHECK(render(rule_of(Fam::ipv4, {DscpMatch{46}}, accept)) ==
        "-A C -m dscp --dscp 46 -j ACCEPT\n");
  // Emission order: iif, addr, dscp, proto, ports; canonical order differs.
  CHECK(render(rule_of(
            Fam::ipv4,
            {PortMatch{PhysicalTransport::tcp, PhysicalDir::dst, false, {{443, 443}}},
             DscpMatch{1}, IifMatch{false, {"lan0"}},
             AddrMatch{PhysicalDir::dst, false, {"10.0.0.0/8"}}},
            accept)) ==
        "-A C -i lan0 -d 10.0.0.0/8 -m dscp --dscp 1 -p tcp --dport 443 -j "
        "ACCEPT\n");
  CHECK(render(rule_of(Fam::ipv4,
                       {PortMatch{PhysicalTransport::udp, PhysicalDir::src, true,
                                  {{1024, 65535}}}},
                       accept)) ==
        "-A C -p udp ! --sport 1024:65535 -j ACCEPT\n");
  CHECK(render(rule_of(Fam::ipv4,
                       {PortMatch{PhysicalTransport::tcp, PhysicalDir::dst, true,
                                  {{80, 80}, {443, 443}, {8000, 9000}}}},
                       accept)) ==
        "-A C -p tcp -m multiport ! --dports 80,443,8000:9000 -j ACCEPT\n");
  CHECK(render(rule_of(Fam::ipv4,
                       {CtDirMatch{true},
                        MarkMatch{PhysicalMarkKind::conntrack, kMask, true, {0}}},
                       {CopyMarkStmt{false, kMask, kMask}})) ==
        "-A C -m conntrack --ctdir ORIGINAL -m connmark ! --mark 0x0/0xff0000 "
        "-j CONNMARK --restore-mark --mask 0xff0000\n");
  CHECK(render(rule_of(Fam::ipv4, {CtStateMatch{ct_dnat | ct_established, false}},
                       accept)) ==
        "-A C -m conntrack --ctstate ESTABLISHED,DNAT -j ACCEPT\n");
  CHECK(render(rule_of(Fam::ipv4,
                       {MarkMatch{PhysicalMarkKind::packet, 0xFFFFFFFFu, true, {0}}},
                       accept)) ==
        "-A C -m mark ! --mark 0x0/0xffffffff -j ACCEPT\n");
  CHECK(render(rule_of(Fam::ipv4, {},
                       {SetMarkStmt{PhysicalMarkKind::packet, 0x10000, kMask}})) ==
        "-A C -j MARK --set-xmark 0x10000/0xff0000\n");
  CHECK(render(rule_of(Fam::ipv4, {},
                       {CopyMarkStmt{true, 0xff, 0xf}})) ==
        "-A C -j CONNMARK --save-mark --nfmask 0xff --ctmask 0xf\n");
  CHECK(render(rule_of(Fam::ipv4, {},
                       {JumpStmt{ipt_chain("KeenPbrTable", PhysicalTable::mangle,
                                           Fam::ipv4),
                                 false}})) == "-A C -j KeenPbrTable\n");
  CHECK(render(rule_of(Fam::ipv4, {}, {VerdictStmt{PhysicalVerdict::drop}})) ==
        "-A C -j DROP\n");

  SUBCASE("the ownership key becomes a comment before the target") {
    auto rule = rule_of(Fam::ipv4, {}, accept);
    rule.key = FirewallRuleKey{"route.mark", "x"};
    CHECK(render(rule) ==
          "-A C -m comment --comment kpbr:v1:route.mark:x -j ACCEPT\n");
  }
  SUBCASE("what iptables cannot express is rejected") {
    CHECK_THROWS_AS(render(rule_of(Fam::ipv4, {UnknownMatch{"x"}}, accept)),
                    FirewallError);
    CHECK_THROWS_AS(render(rule_of(Fam::ipv4, {}, {UnknownStmt{"x"}})),
                    FirewallError);
    CHECK_THROWS_AS(render(rule_of(Fam::ipv4, {}, {})), FirewallError);
    CHECK_THROWS_AS(
        render(rule_of(Fam::ipv4,
                       {MarkMatch{PhysicalMarkKind::packet, 0xff, false, {1, 2}}},
                       accept)),
        FirewallError);
  }
}

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("nft renderer: every match and statement kind") {
  const auto pre = nft_physical_chain_id(Role::nft_prerouting);
  const auto render = [&](PhysicalRule rule) {
    return render_nft_rule(pre, rule)["add"]["rule"];
  };
  const auto accept = std::vector<PhysicalStatement>{
      VerdictStmt{PhysicalVerdict::accept}};

  SUBCASE("envelope, counter and comment") {
    auto rule = rule_of(Fam::any, {}, accept);
    rule.key = FirewallRuleKey{"route.pass", "x"};
    const auto json = render(rule);
    CHECK(json["family"] == "inet");
    CHECK(json["table"] == "KeenPbrTable");
    CHECK(json["chain"] == "prerouting");
    CHECK(json["comment"] == "kpbr:v1:route.pass:x");
    REQUIRE(json["expr"].size() == 2);
    CHECK(json["expr"][0].contains("counter"));
    CHECK(json["expr"][1].contains("accept"));
  }
  SUBCASE("family guard only when no match implies the family") {
    CHECK(render(rule_of(Fam::ipv6, {}, accept))["expr"][0]["match"]["right"] ==
          "ipv6");
    const auto implied = render(rule_of(
        Fam::ipv6, {SetMatch{"s", PhysicalDir::dst, false}}, accept));
    CHECK(implied["expr"][0]["match"]["left"]["payload"]["protocol"] == "ip6");
    CHECK(implied["expr"][0]["match"]["right"] == "@s");
    CHECK(render(rule_of(Fam::any, {}, accept))["expr"].size() == 2);
  }
  SUBCASE("addresses, ports, dscp, proto") {
    const auto json = render(rule_of(
        Fam::ipv4,
        {AddrMatch{PhysicalDir::src, true, {"10.0.0.0/8", "192.0.2.1"}},
         DscpMatch{46},
         PortMatch{PhysicalTransport::tcp, PhysicalDir::dst, false,
                   {{80, 80}, {8000, 9000}}}},
        accept));
    const auto &expr = json["expr"];
    // dscp, addr, proto (implied by the tcp port), port
    CHECK(expr[0]["match"]["left"]["payload"]["field"] == "dscp");
    CHECK(expr[0]["match"]["right"] == 46);
    CHECK(expr[1]["match"]["op"] == "!=");
    CHECK(expr[1]["match"]["right"]["set"].size() == 2);
    CHECK(expr[1]["match"]["right"]["set"][1]["prefix"]["len"] == 32);
    CHECK(expr[2]["match"]["left"]["meta"]["key"] == "l4proto");
    CHECK(expr[3]["match"]["left"]["payload"]["protocol"] == "tcp");
    CHECK(expr[3]["match"]["right"]["set"][1]["range"][1] == 9000);
  }
  SUBCASE("th ports, single range, interface list") {
    const auto ports = render(rule_of(
        Fam::any,
        {PortMatch{PhysicalTransport::any, PhysicalDir::src, false,
                   {{1024, 65535}}}},
        accept));
    CHECK(ports["expr"][0]["match"]["left"]["payload"]["protocol"] == "th");
    CHECK(ports["expr"][0]["match"]["right"]["range"][0] == 1024);
    const auto iif = render(
        rule_of(Fam::any, {IifMatch{true, {"b", "a"}}}, accept));
    CHECK(iif["expr"][0]["match"]["right"]["set"][0] == "a");
    CHECK(iif["expr"][0]["match"]["op"] == "!=");
  }
  SUBCASE("marks, conntrack and the ct restore statements") {
    const auto json = render(rule_of(
        Fam::any,
        {CtDirMatch{true},
         MarkMatch{PhysicalMarkKind::conntrack, kMask, true, {0}}},
        {VmapStmt{PhysicalVmapKey::conntrack_mark_and, kMask,
                  {{kMark1, nft_physical_chain_id(Role::nft_setter, kMark1)}}},
         LateMatchStmt{MarkMatch{PhysicalMarkKind::conntrack, kMask, false,
                                 {kMark1}}},
         VerdictStmt{PhysicalVerdict::accept}}));
    const auto &expr = json["expr"];
    // No counter on the restore hot path.
    REQUIRE(expr.size() == 5);
    CHECK(expr[0]["match"]["left"]["ct"]["key"] == "direction");
    CHECK(expr[1]["match"]["op"] == "!=");
    CHECK(expr[1]["match"]["left"]["&"][1] == kMask);
    CHECK(expr[2]["vmap"]["data"]["set"][0][1]["jump"]["target"] ==
          "setmark_00010000");
    CHECK(expr[3]["match"]["op"] == "in");
    CHECK(expr[3]["match"]["right"]["set"][0] == kMark1);
    CHECK(expr[4].contains("accept"));
  }
  SUBCASE("ct status, numgen vmap, set mark, jump, drop") {
    const auto status = render(rule_of(Fam::any, {CtStateMatch{ct_dnat, false}},
                                       accept));
    CHECK(status["expr"][0]["match"]["left"]["ct"]["key"] == "status");
    CHECK(status["expr"][0]["match"]["right"] == "dnat");
    const auto numgen = render(rule_of(
        Fam::ipv4, {MarkMatch{PhysicalMarkKind::packet, kMask, false, {0}}},
        {VmapStmt{PhysicalVmapKey::numgen_inc, 2,
                  {{0, nft_physical_chain_id(Role::nft_setter, kMark1)},
                   {1, nft_physical_chain_id(Role::nft_setter, kMark2)}}},
         VerdictStmt{PhysicalVerdict::accept}}));
    CHECK(numgen["expr"][0]["match"]["right"] == "ipv4");
    CHECK(numgen["expr"][1]["match"]["left"]["&"][1] == kMask);
    CHECK(numgen["expr"][3]["vmap"]["key"]["numgen"]["mod"] == 2);
    const auto mark = render(rule_of(
        Fam::any, {},
        {SetMarkStmt{PhysicalMarkKind::packet, kMark1, kMask | kMark1},
         VerdictStmt{PhysicalVerdict::accept}}));
    CHECK(mark["expr"][1]["mangle"]["value"]["|"][1] == kMark1);
    CHECK(mark["expr"][1]["mangle"]["value"]["|"][0]["&"][1] ==
          static_cast<uint32_t>(~(kMask | kMark1)));
    const auto plain = render(rule_of(
        Fam::any, {}, {SetMarkStmt{PhysicalMarkKind::packet, 7, 0xFFFFFFFFu}}));
    CHECK(plain["expr"][1]["mangle"]["value"] == 7);
    CHECK(render(rule_of(Fam::any, {}, {VerdictStmt{PhysicalVerdict::drop}}))
              ["expr"][1].contains("drop"));
    CHECK(render(rule_of(Fam::any, {},
                         {JumpStmt{nft_physical_chain_id(Role::nft_setter, 1),
                                   false}}))["expr"][1]["jump"]["target"] ==
          "setmark_00000001");
  }
  SUBCASE("setter chains carry no counter") {
    const auto setter = nft_physical_chain_id(Role::nft_setter, kMark1);
    const auto json = render_nft_rule(
        setter, rule_of(Fam::any, {}, {VerdictStmt{PhysicalVerdict::accept}}));
    CHECK(json["add"]["rule"]["expr"].size() == 1);
  }
  SUBCASE("unsupported elements are rejected") {
    CHECK_THROWS_AS(render(rule_of(Fam::any, {UnknownMatch{"x"}}, accept)),
                    FirewallError);
    CHECK_THROWS_AS(render(rule_of(Fam::any, {}, {UnknownStmt{"x"}})),
                    FirewallError);
    CHECK_THROWS_AS(
        render(rule_of(Fam::any, {}, {CopyMarkStmt{true, 1, 1}})),
        FirewallError);
  }
}
#endif

TEST_CASE("lowering: invalid address in firewall rule criteria") {
  FirewallRuleCriteria criteria;
  criteria.dst_addr = {"not-an-ip"};
  CHECK_THROWS_AS(
      lower_firewall_plan(
          plan_of({mark_rule("a", Fam::ipv4, criteria)}),
          ipt_context()),
      FirewallError);
}

// ===========================================================================
// Interception: DNS hold (NFQUEUE) and L7 sniff (NFLOG)
// ===========================================================================

namespace {

FirewallPlan intercept_plan(const InterceptFirewallSettings &settings) {
  return plan_of(intercept_module_rules(settings));
}

InterceptFirewallSettings dns_only() {
  InterceptFirewallSettings settings;
  settings.dns_hold = true;
  return settings;
}

InterceptFirewallSettings sniff_only() {
  InterceptFirewallSettings settings;
  settings.l7_sniff = true;
  return settings;
}

std::vector<std::string> ipt_lines(const PhysicalRuleset &set,
                                   const std::string &name, Fam family) {
  std::vector<std::string> lines;
  for (const auto &rule :
       chain_of(set, ipt_chain(name, PhysicalTable::mangle, family)).rules) {
    lines.push_back(render_iptables_rule(rule, name));
  }
  return lines;
}

bool has_chain_named(const PhysicalRuleset &set, const std::string &name) {
  for (const auto &chain : set.chains) {
    if (chain.id.name == name) return true;
  }
  return false;
}

#ifndef KEEN_PBR_PLATFORM_KEENETIC
nlohmann::json nft_rule_json(const PhysicalRuleset &set, Role role,
                             std::size_t index) {
  const auto &chain = chain_of(set, nft_physical_chain_id(role));
  REQUIRE(index < chain.rules.size());
  auto json = render_nft_rule(chain.id, chain.rules[index]);
  json["add"]["rule"].erase("comment");
  return json["add"]["rule"]["expr"];
}
#endif

} // namespace

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("lowering interception: nothing is emitted without interception rules") {
  const auto plan = prefilter_plan(true, {"lan0"});
  for (const auto &set : {lower_firewall_plan(plan, ipt_context()),
                          lower_firewall_plan(plan, nft_context())}) {
    for (const auto &chain : set.chains) {
      CHECK(chain.id.role != Role::iptables_dns_hold);
      CHECK(chain.id.role != Role::iptables_sniff);
      CHECK(chain.id.role != Role::nft_dns_hold);
      CHECK(chain.id.role != Role::nft_sniff_forward);
      CHECK(chain.id.role != Role::nft_sniff_output);
    }
  }
  // Both groups disabled: the modules plan nothing at all.
  CHECK(intercept_module_rules(InterceptFirewallSettings{}).empty());
}
#endif

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("lowering interception: DNS hold") {
  const auto plan = intercept_plan(dns_only());
  SUBCASE("iptables emits one chain per family in mangle") {
    const auto set = lower_firewall_plan(plan, ipt_context());
    for (const auto family : {Fam::ipv4, Fam::ipv6}) {
      const auto &chain = chain_of(
          set, ipt_chain("KeenPbrDnsHold", PhysicalTable::mangle, family));
      CHECK(chain.id.role == Role::iptables_dns_hold);
      CHECK_FALSE(chain.base.has_value());
      const auto lines = ipt_lines(set, "KeenPbrDnsHold", family);
      REQUIRE(lines.size() == 2);
      CHECK(lines[0] ==
            "-A KeenPbrDnsHold -p udp --sport 53 -m conntrack --ctstate "
            "ESTABLISHED --ctdir REPLY -m comment --comment " +
                chain.rules[0].key->comment() +
                " -j NFQUEUE --queue-num 9053 --queue-bypass\n");
      CHECK(lines[1].find("-p tcp --sport 53 -m conntrack --ctstate "
                          "ESTABLISHED --ctdir REPLY") != std::string::npos);
    }
    CHECK_FALSE(has_chain_named(set, "KeenPbrSniff"));
    // The classification chains stay empty.
    CHECK(gen_a(set, Fam::ipv4).rules.empty());
    CHECK(out_chain(set, Fam::ipv6).rules.empty());
  }
  SUBCASE("nftables emits a postrouting base chain") {
    const auto set = lower_firewall_plan(plan, nft_context());
    const auto &chain = chain_of(set, nft_physical_chain_id(Role::nft_dns_hold));
    REQUIRE(chain.base.has_value());
    CHECK(chain.base->type == PhysicalBaseChain::Type::filter);
    CHECK(chain.base->hook == PhysicalBaseChain::Hook::postrouting);
    CHECK(chain.base->priority == -150);
    CHECK(chain.base->policy_accept);
    // One family-agnostic rule per protocol.
    REQUIRE(chain.rules.size() == 2);
    CHECK(chain.rules[0].family == Fam::any);
    const auto expr = nft_rule_json(set, Role::nft_dns_hold, 0);
    CHECK(expr[0]["match"]["right"] == "udp");
    CHECK(expr[1]["match"]["right"] == 53);
    CHECK(expr[1]["match"]["left"]["payload"]["protocol"] == "udp");
    CHECK(expr[1]["match"]["left"]["payload"]["field"] == "sport");
    CHECK(expr[2]["match"]["left"]["ct"]["key"] == "state");
    CHECK(expr[2]["match"]["right"] == "established");
    CHECK(expr[3]["match"]["left"]["ct"]["key"] == "direction");
    CHECK(expr[3]["match"]["right"] == 1);
    CHECK(expr.back() == nlohmann::json{{"queue", {{"num", 9053},
                                                   {"flags", {"bypass"}}}}});
    CHECK_FALSE(set.find(nft_physical_chain_id(Role::nft_sniff_forward)));
  }
  SUBCASE("a custom queue number is carried") {
    InterceptFirewallSettings settings = dns_only();
    settings.queue_num = 7;
    const auto set = lower_firewall_plan(intercept_plan(settings), ipt_context());
    CHECK(ipt_lines(set, "KeenPbrDnsHold", Fam::ipv4)[0].find(
              "--queue-num 7 --queue-bypass") != std::string::npos);
  }
}
#endif

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("lowering interception: DNS hold excludes loopback replies when asked") {
  auto rules = intercept_module_rules(dns_only());
  for (auto &rule : rules) rule.criteria.exclude_oif = {"lo"};
  const auto plan = plan_of(std::move(rules));
  SUBCASE("iptables") {
    const auto set = lower_firewall_plan(plan, ipt_context());
    const auto lines = ipt_lines(set, "KeenPbrDnsHold", Fam::ipv4);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].find("! -o lo") != std::string::npos);
  }
  SUBCASE("nftables") {
    const auto set = lower_firewall_plan(plan, nft_context());
    const auto &chain = chain_of(set, nft_physical_chain_id(Role::nft_dns_hold));
    REQUIRE(chain.rules.size() == 2);
    CHECK(has_match(chain.rules[0], OifMatch{true, {"lo"}}));
  }
}
#endif

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("lowering interception: client interface scope") {
  InterceptFirewallSettings settings;
  settings.dns_hold = true;
  settings.l7_sniff = true;
  const auto rules_for = [&](const std::vector<std::string> &inbound,
                             const std::vector<std::string> &wan) {
    return plan_of(intercept_module_rules(settings, inbound, wan));
  };

  SUBCASE("iptables, WAN denylist of one interface: negated match, no guards") {
    const auto set = lower_firewall_plan(rules_for({}, {"wan0"}), ipt_context());
    const auto sniff = ipt_lines(set, "KeenPbrSniff", Fam::ipv4);
    // The output copy is covered by the forward rule (no input interface).
    REQUIRE(sniff.size() == 2);
    CHECK(sniff[0].find("-A KeenPbrSniff ! -i wan0 -p tcp") == 0);
    CHECK(sniff[0].find("-m conntrack --ctdir ORIGINAL") != std::string::npos);
    const auto hold = ipt_lines(set, "KeenPbrDnsHold", Fam::ipv4);
    REQUIRE(hold.size() == 2);
    CHECK(hold[0].find("-A KeenPbrDnsHold ! -o wan0 -p udp") == 0);
  }
  SUBCASE("iptables, WAN denylist of several interfaces: leading RETURN guards") {
    const auto set =
        lower_firewall_plan(rules_for({}, {"wan1", "wan0"}), ipt_context());
    const auto sniff = ipt_lines(set, "KeenPbrSniff", Fam::ipv4);
    REQUIRE(sniff.size() == 4);
    CHECK(sniff[0] == "-A KeenPbrSniff -i wan0 -m comment --comment " +
                          chain_of(set, ipt_chain("KeenPbrSniff",
                                                  PhysicalTable::mangle,
                                                  Fam::ipv4))
                              .rules[0]
                              .key->comment() +
                          " -j RETURN\n");
    CHECK(sniff[1].find("-A KeenPbrSniff -i wan1 ") == 0);
    CHECK(sniff[2].find("-A KeenPbrSniff -p tcp") == 0);
    CHECK(sniff[2].find("-i ") == std::string::npos);
    const auto hold = ipt_lines(set, "KeenPbrDnsHold", Fam::ipv4);
    REQUIRE(hold.size() == 4);
    CHECK(hold[0].find("-A KeenPbrDnsHold -o wan0 ") == 0);
    CHECK(hold[0].find("-j RETURN") != std::string::npos);
    CHECK(hold[1].find("-A KeenPbrDnsHold -o wan1 ") == 0);
  }
  SUBCASE("iptables, inbound allowlist: one fragment per interface") {
    const auto set =
        lower_firewall_plan(rules_for({"br0", "br1"}, {}), ipt_context());
    const auto sniff = ipt_lines(set, "KeenPbrSniff", Fam::ipv4);
    // The shared chain is jumped from FORWARD: every rule needs an `-i`.
    REQUIRE(sniff.size() == 4);
    CHECK(sniff[0].find("-A KeenPbrSniff -i br0 -p tcp") == 0);
    CHECK(sniff[1].find("-A KeenPbrSniff -i br1 -p tcp") == 0);
    CHECK(sniff[2].find("-A KeenPbrSniff -i br0 -p udp") == 0);
    CHECK(sniff[3].find("-A KeenPbrSniff -i br1 -p udp") == 0);
    // Router-originated copies live in their own chain, jumped from OUTPUT.
    const auto &out_chain = chain_of(
        set, ipt_chain("KeenPbrSniffOut", PhysicalTable::mangle, Fam::ipv4));
    CHECK(out_chain.id.role == Role::iptables_sniff_out);
    CHECK_FALSE(chain_of(set, ipt_chain("KeenPbrSniff", PhysicalTable::mangle,
                                        Fam::ipv4))
                    .output_hook);
    const auto out = ipt_lines(set, "KeenPbrSniffOut", Fam::ipv4);
    REQUIRE(out.size() == 2);
    CHECK(out[0].find("-A KeenPbrSniffOut -p tcp") == 0);
    CHECK(out[0].find("-i ") == std::string::npos);
    const auto hold = ipt_lines(set, "KeenPbrDnsHold", Fam::ipv4);
    // br0, br1 and lo (router traffic is processed in this plan), per proto.
    REQUIRE(hold.size() == 6);
    CHECK(hold[0].find("-A KeenPbrDnsHold -o br0 -p udp") == 0);
    CHECK(hold[2].find("-A KeenPbrDnsHold -o lo -p udp") == 0);
    CHECK(hold[3].find("-A KeenPbrDnsHold -o br0 -p tcp") == 0);
  }
  SUBCASE("iptables, allowlist: no FORWARD-reachable sniff rule lacks the -i match") {
    InterceptFirewallSettings sniff = sniff_only();
    const auto set = lower_firewall_plan(
        plan_of(intercept_module_rules(sniff, {"lan0"}, {})), ipt_context());
    for (const auto family : {Fam::ipv4, Fam::ipv6}) {
      const auto &shared = chain_of(
          set, ipt_chain("KeenPbrSniff", PhysicalTable::mangle, family));
      REQUIRE_FALSE(shared.rules.empty());
      for (const auto &rule : shared.rules) {
        CHECK(has_match(rule, IifMatch{false, {"lan0"}}));
      }
    }
  }
  SUBCASE("iptables, denylist: one shared sniff chain, no separate OUTPUT chain") {
    const auto set = lower_firewall_plan(rules_for({}, {"wan0"}), ipt_context());
    CHECK_FALSE(has_chain_named(set, "KeenPbrSniffOut"));
    CHECK(chain_of(set, ipt_chain("KeenPbrSniff", PhysicalTable::mangle,
                                  Fam::ipv4))
              .output_hook);
  }
  SUBCASE("iptables, no WAN known and no allowlist: no interface match") {
    const auto set = lower_firewall_plan(rules_for({}, {}), ipt_context());
    for (const auto &line : ipt_lines(set, "KeenPbrSniff", Fam::ipv4)) {
      CHECK(line.find(" -i ") == std::string::npos);
    }
    for (const auto &line : ipt_lines(set, "KeenPbrDnsHold", Fam::ipv4)) {
      CHECK(line.find(" -o ") == std::string::npos);
    }
  }
  SUBCASE("nftables, WAN denylist: one rule with a negated interface set") {
    const auto set =
        lower_firewall_plan(rules_for({}, {"wan1", "wan0"}), nft_context());
    const auto &forward =
        chain_of(set, nft_physical_chain_id(Role::nft_sniff_forward));
    REQUIRE(forward.rules.size() == 2);
    for (const auto &rule : forward.rules) {
      CHECK(has_match(rule, IifMatch{true, {"wan0", "wan1"}}));
      CHECK(has_match(rule, CtDirMatch{true}));
    }
    // The router-originated chain has no input interface to test.
    const auto &output =
        chain_of(set, nft_physical_chain_id(Role::nft_sniff_output));
    REQUIRE(output.rules.size() == 2);
    for (const auto &rule : output.rules) {
      for (const auto &match : rule.matches) {
        CHECK_FALSE(std::holds_alternative<IifMatch>(match));
      }
      CHECK(has_match(rule, CtDirMatch{true}));
    }
    const auto &hold = chain_of(set, nft_physical_chain_id(Role::nft_dns_hold));
    REQUIRE(hold.rules.size() == 2);
    CHECK(has_match(hold.rules[0], OifMatch{true, {"wan0", "wan1"}}));
  }
  SUBCASE("nftables, inbound allowlist: one rule with an interface set") {
    const auto set =
        lower_firewall_plan(rules_for({"br1", "br0"}, {}), nft_context());
    const auto &forward =
        chain_of(set, nft_physical_chain_id(Role::nft_sniff_forward));
    REQUIRE(forward.rules.size() == 2);
    for (const auto &rule : forward.rules) {
      CHECK(has_match(rule, IifMatch{false, {"br0", "br1"}}));
    }
    const auto &hold = chain_of(set, nft_physical_chain_id(Role::nft_dns_hold));
    REQUIRE(hold.rules.size() == 2);
    CHECK(has_match(hold.rules[0], OifMatch{false, {"br0", "br1", "lo"}}));
  }
}
#endif

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("lowering interception: L7 sniff") {
  SUBCASE("iptables: one shared chain serves FORWARD and OUTPUT") {
    const auto set = lower_firewall_plan(intercept_plan(sniff_only()), ipt_context());
    for (const auto family : {Fam::ipv4, Fam::ipv6}) {
      const auto &chain = chain_of(
          set, ipt_chain("KeenPbrSniff", PhysicalTable::mangle, family));
      CHECK(chain.id.role == Role::iptables_sniff);
      CHECK(chain.output_hook);
      const auto lines = ipt_lines(set, "KeenPbrSniff", family);
      // The forward and output copies are the same physical rule.
      REQUIRE(lines.size() == 2);
      CHECK(lines[0].find("-A KeenPbrSniff -p tcp -m multiport --dports 80,443 "
                          "-m conntrack --ctdir ORIGINAL -m connbytes "
                          "--connbytes 1:6 --connbytes-dir original "
                          "--connbytes-mode packets") == 0);
      CHECK(lines[0].find(" -j NFLOG --nflog-group 9054 --nflog-size 2048\n") !=
            std::string::npos);
      CHECK(lines[1].find("-A KeenPbrSniff -p udp --dport 443 -m conntrack "
                          "--ctdir ORIGINAL -m connbytes --connbytes 1:6 "
                          "--connbytes-dir original --connbytes-mode "
                          "packets") == 0);
    }
    CHECK_FALSE(has_chain_named(set, "KeenPbrDnsHold"));
  }
  SUBCASE("nftables: separate forward and output chains") {
    const auto set = lower_firewall_plan(intercept_plan(sniff_only()), nft_context());
    for (const auto role : {Role::nft_sniff_forward, Role::nft_sniff_output}) {
      const auto &chain = chain_of(set, nft_physical_chain_id(role));
      REQUIRE(chain.base.has_value());
      CHECK(chain.base->type == PhysicalBaseChain::Type::filter);
      CHECK(chain.base->hook == (role == Role::nft_sniff_forward
                                     ? PhysicalBaseChain::Hook::forward
                                     : PhysicalBaseChain::Hook::output));
      CHECK(chain.base->priority == -150);
      REQUIRE(chain.rules.size() == 2);
      const auto expr = nft_rule_json(set, role, 0);
      CHECK(expr[2]["match"]["left"] ==
            nlohmann::json{{"ct", {{"key", "direction"}}}});
      CHECK(expr[2]["match"]["right"] == 0);
      CHECK(expr[3]["match"]["left"] ==
            nlohmann::json{{"ct", {{"key", "packets"}, {"dir", "original"}}}});
      CHECK(expr[3]["match"]["right"] == nlohmann::json{{"range", {1, 6}}});
      CHECK(expr[4] == nlohmann::json{{"counter", nullptr}});
      CHECK(expr.back() ==
            nlohmann::json{{"log", {{"group", 9054}, {"snaplen", 2048}}}});
    }
    CHECK_FALSE(set.find(nft_physical_chain_id(Role::nft_dns_hold)));
  }
  SUBCASE("protocol toggles") {
    const auto ports_of = [](bool tls, bool http, bool quic) {
      InterceptFirewallSettings settings = sniff_only();
      settings.tls = tls;
      settings.http = http;
      settings.quic = quic;
      const auto set = lower_firewall_plan(intercept_plan(settings), ipt_context());
      std::string text;
      if (!has_chain_named(set, "KeenPbrSniff")) return std::string("<none>");
      for (const auto &line : ipt_lines(set, "KeenPbrSniff", Fam::ipv4)) {
        text += line.substr(line.find(" -p ") + 1, line.find(" -m conntrack") -
                                                   line.find(" -p ") - 1) + "|";
      }
      return text;
    };
    CHECK(ports_of(true, true, true) ==
          "-p tcp -m multiport --dports 80,443|-p udp --dport 443|");
    CHECK(ports_of(false, true, true) == "-p tcp --dport 80|-p udp --dport 443|");
    CHECK(ports_of(true, false, true) == "-p tcp --dport 443|-p udp --dport 443|");
    CHECK(ports_of(true, true, false) == "-p tcp -m multiport --dports 80,443|");
    CHECK(ports_of(false, false, true) == "-p udp --dport 443|");
    CHECK(ports_of(true, false, false) == "-p tcp --dport 443|");
    CHECK(ports_of(false, false, false) == "<none>");
  }
  SUBCASE("group, snaplen and packet window come from the settings") {
    InterceptFirewallSettings settings = sniff_only();
    settings.nflog_group = 12;
    settings.snaplen = 512;
    settings.max_packets = 3;
    const auto set = lower_firewall_plan(intercept_plan(settings), ipt_context());
    const auto line = ipt_lines(set, "KeenPbrSniff", Fam::ipv4)[0];
    CHECK(line.find("--connbytes 1:3") != std::string::npos);
    CHECK(line.find("--nflog-group 12 --nflog-size 512") != std::string::npos);
  }
}
#endif

TEST_CASE("lowering interception: IPv6 disabled drops the IPv6 chains") {
  InterceptFirewallSettings settings = dns_only();
  settings.l7_sniff = true;
  auto context = ipt_context();
  context.ipv6_enabled = false;
  const auto set = lower_firewall_plan(intercept_plan(settings), context);
  for (const auto &chain : set.chains) {
    CHECK(chain.id.family != Fam::ipv6);
  }
  CHECK(set.find(ipt_chain("KeenPbrDnsHold", PhysicalTable::mangle, Fam::ipv4)));
  CHECK(set.find(ipt_chain("KeenPbrSniff", PhysicalTable::mangle, Fam::ipv4)));
}

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("lowering interception: raw mode and prefilters do not affect it") {
  InterceptFirewallSettings settings = dns_only();
  settings.l7_sniff = true;
  auto plan = prefilter_plan(true, {"lan0", "wg0"});
  for (auto &rule : intercept_module_rules(settings)) {
    plan.rules.push_back(std::move(rule));
  }
  plan.rules.push_back(mark_rule("a", Fam::ipv4, for_set("kpbr4_x")));
  auto raw = ipt_context();
  raw.raw_prerouting = RawPreroutingMode{true, true};
  const auto raw_set = lower_firewall_plan(plan, raw);
  const auto mangle_set = lower_firewall_plan(plan, ipt_context());
  for (const auto family : {Fam::ipv4, Fam::ipv6}) {
    for (const char *name : {"KeenPbrDnsHold", "KeenPbrSniff"}) {
      const auto id = ipt_chain(name, PhysicalTable::mangle, family);
      CHECK_FALSE(raw_set.find(ipt_chain(name, PhysicalTable::raw, family)));
      const auto &in_raw = chain_of(raw_set, id);
      const auto &in_mangle = chain_of(mangle_set, id);
      CHECK(in_raw == in_mangle);
      // Only interception rules: no marks, skips or interface guards.
      for (const auto &rule : in_raw.rules) {
        REQUIRE(rule.statements.size() == 1);
        CHECK((std::holds_alternative<QueueStmt>(rule.statements[0]) ||
               std::holds_alternative<LogStmt>(rule.statements[0])));
        CHECK_FALSE(find_match<IifMatch>(rule));
        CHECK_FALSE(find_match<MarkMatch>(rule));
      }
    }
  }
  // And the classification chains never see interception rules.
  for (const auto &chain : raw_set.chains) {
    if (chain.id.role != Role::iptables_prerouting &&
        chain.id.role != Role::iptables_output) {
      continue;
    }
    for (const auto &rule : chain.rules) {
      for (const auto &statement : rule.statements) {
        CHECK_FALSE(std::holds_alternative<QueueStmt>(statement));
        CHECK_FALSE(std::holds_alternative<LogStmt>(statement));
      }
    }
  }
  // nft: the same plan yields the classification chains plus the base chains.
  const auto nft = lower_firewall_plan(plan, nft_context());
  CHECK(nft.find(nft_physical_chain_id(Role::nft_dns_hold)));
  CHECK(nft.find(nft_physical_chain_id(Role::nft_sniff_forward)));
  CHECK(nft.find(nft_physical_chain_id(Role::nft_sniff_output)));
  for (const auto role : {Role::nft_prerouting, Role::nft_output}) {
    for (const auto &rule : chain_of(nft, nft_physical_chain_id(role)).rules) {
      for (const auto &statement : rule.statements) {
        CHECK_FALSE(std::holds_alternative<QueueStmt>(statement));
        CHECK_FALSE(std::holds_alternative<LogStmt>(statement));
      }
    }
  }
}
#endif

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("lowering interception: plan rules are attributed and keyed") {
  InterceptFirewallSettings settings = dns_only();
  settings.l7_sniff = true;
  const auto plan = intercept_plan(settings);
  for (const auto backend : {FirewallBackend::iptables, FirewallBackend::nftables}) {
    auto context = backend == FirewallBackend::iptables ? ipt_context()
                                                         : nft_context();
    const auto set = lower_firewall_plan(plan, context);
    for (const auto &chain : set.chains) {
      for (const auto &rule : chain.rules) {
        REQUIRE(rule.plan_rule < plan.rules.size());
        REQUIRE(rule.key.has_value());
        CHECK(*rule.key == plan.rules[rule.plan_rule].key);
      }
    }
  }
}
#endif

namespace {

FirewallPlan skip_lan_plan(std::vector<std::string> lan) {
  auto plan = plan_of({});
  plan.rules.push_back(make_rule("prefilter.skip_local_replies", "reply",
                                 Fam::any, {}, SkipLocalRepliesAction{}));
  if (!lan.empty()) {
    plan.rules.push_back(make_rule(
        "prefilter.skip_lan_output", "lan_oif", Fam::any, {},
        SkipLanOutputAction{SkipLanOutputAction::Kind::lan_oif, std::move(lan)},
        FirewallHook::output));
  }
  plan.rules.push_back(make_rule(
      "prefilter.skip_lan_output", "bcast", Fam::any, {},
      SkipLanOutputAction{SkipLanOutputAction::Kind::broadcast, {}},
      FirewallHook::output));
  plan.rules.push_back(make_rule(
      "prefilter.skip_lan_output", "mcast", Fam::any, {},
      SkipLanOutputAction{SkipLanOutputAction::Kind::multicast, {}},
      FirewallHook::output));
  return plan;
}

} // namespace

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("lowering nftables: skip_lan_output only in OUTPUT, after the reply skip") {
  const auto set =
      lower_firewall_plan(skip_lan_plan({"wg0", "br-lan"}), nft_context());
  // PREROUTING only has the reply skip.
  REQUIRE(nft_pre(set).rules.size() == 1);
  CHECK(has_match(nft_pre(set).rules[0], CtDirMatch{false}));
  for (const auto &rule : nft_pre(set).rules) {
    CHECK(find_match<OifMatch>(rule) == nullptr);
    CHECK(find_match<AddrTypeMatch>(rule) == nullptr);
  }
  const auto &out = nft_out(set).rules;
  REQUIRE(out.size() == 4);
  CHECK(has_match(out[0], CtDirMatch{false}));
  CHECK(has_match(out[1], OifMatch{false, {"br-lan", "wg0"}}));
  CHECK(has_match(out[2], AddrTypeMatch{addr_broadcast}));
  CHECK(has_match(out[3], AddrTypeMatch{addr_multicast}));
  for (std::size_t i = 1; i < out.size(); ++i) {
    CHECK(verdict_of(out[i]) == PhysicalVerdict::accept);
  }
  CHECK(out[1].key->module_id == "prefilter.skip_lan_output");

  const auto json =
      render_nft_rule(nft_out(set).id, out[1])["add"]["rule"]["expr"].dump();
  CHECK(json.find(R"("key":"oifname")") != std::string::npos);
  const auto mcast =
      render_nft_rule(nft_out(set).id, out[3])["add"]["rule"]["expr"].dump();
  CHECK(mcast.find(R"("fib":{"flags":["daddr"],"result":"type"})") !=
        std::string::npos);
}
#endif

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("lowering nftables: empty inbound interfaces keep bcast/mcast only") {
  const auto set = lower_firewall_plan(skip_lan_plan({}), nft_context());
  const auto &out = nft_out(set).rules;
  REQUIRE(out.size() == 3);
  CHECK(find_match<OifMatch>(out[1]) == nullptr);
  CHECK(has_match(out[1], AddrTypeMatch{addr_broadcast}));
  CHECK(has_match(out[2], AddrTypeMatch{addr_multicast}));
}
#endif

TEST_CASE("lowering iptables: skip_lan_output only in OUTPUT, one oif rule per interface") {
  const auto set =
      lower_firewall_plan(skip_lan_plan({"wg0", "br-lan"}), ipt_context());
  for (const Fam family : {Fam::ipv4, Fam::ipv6}) {
    // PREROUTING: only the reply skip.
    REQUIRE(gen_a(set, family).rules.size() == 1);
    const auto &out = out_chain(set, family).rules;
    CHECK(has_match(out[0], CtDirMatch{false}));
    CHECK(has_match(out[1], OifMatch{false, {"wg0"}}));
    CHECK(has_match(out[2], OifMatch{false, {"br-lan"}}));
    for (std::size_t i = 1; i < out.size(); ++i) {
      CHECK(verdict_of(out[i]) == PhysicalVerdict::return_);
    }
    if (family == Fam::ipv4) {
      REQUIRE(out.size() == 5);
      CHECK(has_match(out[3], AddrTypeMatch{addr_broadcast}));
      CHECK(has_match(out[4], AddrTypeMatch{addr_multicast}));
    } else {
      // IPv6 has no broadcast.
      REQUIRE(out.size() == 4);
      CHECK(has_match(out[3], AddrTypeMatch{addr_multicast}));
    }
  }
  CHECK(render_iptables_rule(out_chain(set, Fam::ipv4).rules[1], "KeenPbrOutput") ==
        "-A KeenPbrOutput -o wg0 -m comment --comment "
        "kpbr:v1:prefilter.skip_lan_output:lan_oif -j RETURN\n");
  CHECK(render_iptables_rule(out_chain(set, Fam::ipv4).rules[4], "KeenPbrOutput") ==
        "-A KeenPbrOutput -m addrtype --dst-type MULTICAST -m comment --comment "
        "kpbr:v1:prefilter.skip_lan_output:mcast -j RETURN\n");
}

TEST_CASE("lowering iptables: skip_lan_output also applies in raw PREROUTING mode") {
  auto context = ipt_context();
  context.raw_prerouting = RawPreroutingMode{true, true};
  const auto set = lower_firewall_plan(skip_lan_plan({"br-lan"}), context);
  CHECK(chain_of(set, ipt_chain("KeenPbrRaw", PhysicalTable::raw, Fam::ipv4))
            .rules.empty());
  CHECK(out_chain(set, Fam::ipv4).rules.size() == 4);
}

TEST_CASE("renderers: output interface and destination type") {
  const auto accept = std::vector<PhysicalStatement>{
      VerdictStmt{PhysicalVerdict::accept}};
  CHECK(render_iptables_rule(
            rule_of(Fam::ipv4, {OifMatch{true, {"lan0"}}}, accept), "C") ==
        "-A C ! -o lan0 -j ACCEPT\n");
  CHECK_THROWS_AS(render_iptables_rule(
                      rule_of(Fam::ipv4, {OifMatch{false, {"a", "b"}}}, accept),
                      "C"),
                  FirewallError);
  CHECK_THROWS_AS(
      render_iptables_rule(
          rule_of(Fam::ipv4, {AddrTypeMatch{addr_broadcast | addr_multicast}},
                  accept),
          "C"),
      FirewallError);
}

TEST_CASE("plan validation: skip_lan_output is OUTPUT only with a valid list") {
  const auto make = [](SkipLanOutputAction action, FirewallHook hook) {
    FirewallPlan plan;
    plan.fwmark_mask = kMask;
    FirewallRuleRegistrar registrar(plan);
    registrar.register_rule(
        make_rule("m", "i", Fam::any, {}, std::move(action), hook));
  };
  using Kind = SkipLanOutputAction::Kind;
  CHECK_NOTHROW(make({Kind::lan_oif, {"br-lan"}}, FirewallHook::output));
  CHECK_NOTHROW(make({Kind::broadcast, {}}, FirewallHook::output));
  CHECK_THROWS_AS(make({Kind::lan_oif, {"br-lan"}}, FirewallHook::prerouting),
                  std::invalid_argument);
  CHECK_THROWS_AS(make({Kind::lan_oif, {}}, FirewallHook::output),
                  std::invalid_argument);
  CHECK_THROWS_AS(make({Kind::multicast, {"br-lan"}}, FirewallHook::output),
                  std::invalid_argument);
}

TEST_CASE("plan validation: interception actions only on their hooks") {
  const auto make = [](FirewallRuleAction action, FirewallHook hook) {
    FirewallPlan plan;
    plan.fwmark_mask = kMask;
    FirewallRuleRegistrar registrar(plan);
    registrar.register_rule(make_rule("m", "i", Fam::any, {}, std::move(action),
                                      hook));
  };
  CHECK_NOTHROW(make(QueueAction{1, true}, FirewallHook::postrouting));
  CHECK_NOTHROW(make(LogAction{1, 0}, FirewallHook::forward));
  CHECK_NOTHROW(make(LogAction{1, 0}, FirewallHook::output));
  CHECK_THROWS_AS(make(QueueAction{1, true}, FirewallHook::prerouting),
                  std::invalid_argument);
  CHECK_THROWS_AS(make(QueueAction{1, true}, FirewallHook::forward),
                  std::invalid_argument);
  CHECK_THROWS_AS(make(LogAction{1, 0}, FirewallHook::postrouting),
                  std::invalid_argument);
  CHECK_THROWS_AS(make(LogAction{1, 0}, FirewallHook::prerouting),
                  std::invalid_argument);
  CHECK_THROWS_AS(make(VerdictAction::drop, FirewallHook::forward),
                  std::invalid_argument);
  CHECK_THROWS_AS(make(VerdictAction::drop, FirewallHook::postrouting),
                  std::invalid_argument);
}

#ifndef KEEN_PBR_PLATFORM_KEENETIC
TEST_CASE("iptables balance lowers to a guarded statistic cascade") {
  const auto restore = make_rule("prefilter.restore_conntrack_mark", "m",
                                 Fam::any, {}, RestoreConntrackMarkAction{kMask});
  const auto balance = make_rule(
      "route.balance", "b", Fam::ipv4, for_set("kpbr4_x"),
      BalanceAction{kMark1, {{kMark1, true, true},
                             {kMark2, true, true},
                             {kMark3, true, false}}});
  const auto lowered = lower_firewall_plan(plan_of({restore, balance}),
                                           ipt_context());
  const auto &rules = gen_a(lowered, Fam::ipv4).rules;
  // restore pair, then 3 picks + save + return
  REQUIRE(rules.size() == 7);
  const auto probability = [&](std::size_t index) {
    const auto *stat = find_match<StatisticMatch>(rules[index]);
    return stat == nullptr ? 0U : stat->probability;
  };
  // 1/3, 1/2, then unconditional; every pick only sees unmarked packets.
  CHECK(probability(2) == 0x2AAAAAABu);
  CHECK(probability(3) == 0x40000000u);
  CHECK(find_match<StatisticMatch>(rules[4]) == nullptr);
  for (std::size_t i = 2; i < 5; ++i) {
    CHECK(has_match(rules[i],
                    MarkMatch{PhysicalMarkKind::packet, kMask, false, {0}}));
  }
  CHECK(std::get<SetMarkStmt>(rules[4].statements[0]).value == kMark3);
  CHECK(std::get<CopyMarkStmt>(rules[5].statements[0]).to_conntrack);
  CHECK(verdict_of(rules[6]) == PhysicalVerdict::return_);
  // IPv6 has no usable candidate: the fallback mark, no cascade.
  const auto &v6 = gen_a(lowered, Fam::ipv6).rules;
  for (const auto &rule : v6) CHECK(find_match<StatisticMatch>(rule) == nullptr);

  SUBCASE("a balance rule without the connection mark cannot be sticky") {
    CHECK_THROWS_AS(lower_firewall_plan(plan_of({balance}), ipt_context()),
                    FirewallError);
  }
}
#endif

// --nflog-size exists only since iptables 1.6.0.  Without it (Keenetic ships
// 1.4.21) the rule carries no snaplen and the NFLOG group's copy range bounds
// the payload instead.
TEST_CASE("lowering interception: --nflog-size follows the iptables capability") {
  InterceptFirewallSettings settings;
  settings.dns_hold = true;
  settings.l7_sniff = true;
  const auto plan = plan_of(intercept_module_rules(settings, {"br0"}, {}));

  auto context = ipt_context();
  const auto modern = lower_firewall_plan(plan, context);
  for (const auto &line : ipt_lines(modern, "KeenPbrSniff", Fam::ipv4)) {
    CHECK(line.find(" -j NFLOG --nflog-group 9054 --nflog-size 2048\n") !=
          std::string::npos);
  }

  context.nflog_size_ipv4_supported = false;
  context.nflog_size_ipv6_supported = false;
  const auto legacy = lower_firewall_plan(plan, context);
  for (const auto family : {Fam::ipv4, Fam::ipv6}) {
    const auto lines = ipt_lines(legacy, "KeenPbrSniff", family);
    REQUIRE_FALSE(lines.empty());
    for (const auto &line : lines) {
      CHECK(line.find("--nflog-size") == std::string::npos);
      CHECK(line.find(" -j NFLOG --nflog-group 9054\n") != std::string::npos);
    }
  }
  // One family can lack it while the other has it.
  context.nflog_size_ipv6_supported = true;
  const auto mixed = lower_firewall_plan(plan, context);
  CHECK(ipt_lines(mixed, "KeenPbrSniff", Fam::ipv4)[0].find("--nflog-size") ==
        std::string::npos);
  CHECK(ipt_lines(mixed, "KeenPbrSniff", Fam::ipv6)[0].find("--nflog-size") !=
        std::string::npos);
}

// Every option the interception lowering can emit must exist in iptables
// 1.4.21 (Keenetic) when the optional capabilities are off.
TEST_CASE("lowering interception: only iptables 1.4.21 options without optional capabilities") {
  static const std::set<std::string> kAllowed = {
      "-A", "-i", "-o", "-p", "-s", "-d", "-j", "-g", "-m", "!",
      "--sport", "--dport", "--sports", "--dports",
      "--ctstate", "--ctdir", "--connbytes", "--connbytes-dir",
      "--connbytes-mode", "--dst-type", "--dscp", "--mark",
      "--match-set", "--comment", "--set-xmark", "--save-mark",
      "--restore-mark", "--mask", "--nfmask", "--ctmask",
      "--queue-num", "--queue-bypass", "--nflog-group",
      "--nflog-threshold"};
  InterceptFirewallSettings settings;
  settings.dns_hold = true;
  settings.l7_sniff = true;
  auto context = ipt_context();
  context.comments_ipv4_supported = false;
  context.comments_ipv6_supported = false;
  context.nflog_size_ipv4_supported = false;
  context.nflog_size_ipv6_supported = false;
  for (const auto &inbound : {std::vector<std::string>{"br0"},
                              std::vector<std::string>{"br0", "br1"},
                              std::vector<std::string>{}}) {
    const auto set = lower_firewall_plan(
        plan_of(intercept_module_rules(settings, inbound, {"wan0", "wan1"})),
        context);
    std::size_t checked = 0;
    for (const auto &chain : set.chains) {
      for (const auto &rule : chain.rules) {
        std::istringstream words(render_iptables_rule(rule, chain.id.name));
        std::string word;
        while (words >> word) {
          if (word[0] != '-' && word != "!") continue;
          if (word.size() > 1 && std::isdigit(static_cast<unsigned char>(word[1]))) continue;
          CAPTURE(word);
          CHECK(kAllowed.count(word) == 1);
          ++checked;
        }
      }
    }
    CHECK(checked > 0);
  }
}

} // namespace keen_pbr3
