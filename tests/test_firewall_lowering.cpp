#include <doctest/doctest.h>

#include "../src/firewall/firewall_lowering.hpp"
#include "../src/firewall/iptables.hpp"
#include "../src/firewall/nftables.hpp"

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
                               FirewallRuleAction action) {
  FirewallRuleInstance rule;
  rule.key = {std::move(module), std::move(instance)};
  rule.family = family;
  rule.hook = criteria.apply_output ? FirewallHook::output
                                    : FirewallHook::prerouting;
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

const PhysicalChain &gen_a(const PhysicalRuleset &set, Fam family) {
  return chain_of(set, ipt_chain("KeenPbrTable_A", PhysicalTable::mangle, family));
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
    REQUIRE(set.chains.size() == 2);
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
    REQUIRE(set.chains.size() == 1);
    CHECK(set.chains[0].id.family == Fam::ipv4);
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
  SUBCASE("balance and default gateway are nft only") {
    CHECK_THROWS_AS(
        lower_firewall_plan(
            plan_of({make_rule("route.balance", "b", Fam::ipv4, {},
                               BalanceAction{kMark1, {}})}),
            ipt_context()),
        FirewallError);
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
        set, ipt_chain("KeenPbrRaw_A", PhysicalTable::raw, Fam::ipv4));
    const auto &output = chain_of(
        set, ipt_chain("KeenPbrOutput_A", PhysicalTable::mangle, Fam::ipv4));
    CHECK(raw.rules.size() == 2); // skip marked, inbound
    CHECK(output.rules.size() == 5);
  }
  SUBCASE("only the family in raw mode moves") {
    auto context = ipt_context();
    context.raw_prerouting = RawPreroutingMode{false, true};
    const auto set = lower_firewall_plan(prefilter_plan(false, {}), context);
    CHECK(set.chains.size() == 3);
    CHECK(set.find(ipt_chain("KeenPbrTable_A", PhysicalTable::mangle,
                             Fam::ipv4)) != nullptr);
    CHECK(set.find(ipt_chain("KeenPbrRaw_A", PhysicalTable::raw, Fam::ipv6)) !=
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
    CHECK(chain_of(raw, ipt_chain("KeenPbrRaw_A", PhysicalTable::raw, Fam::ipv4))
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
  SUBCASE("generation B names the B chains per family") {
    auto context = ipt_context();
    context.generation_ipv6 = FirewallSetGeneration::B;
    const auto set =
        lower_firewall_plan(plan_of({mark_rule("a", Fam::any, {})}), context);
    CHECK(set.find(ipt_chain("KeenPbrTable_A", PhysicalTable::mangle,
                             Fam::ipv4)) != nullptr);
    CHECK(set.find(ipt_chain("KeenPbrTable_B", PhysicalTable::mangle,
                             Fam::ipv6)) != nullptr);
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
  CHECK(nft_out(set).rules.empty());

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
    // The restore rule guards OUTPUT too.
    CHECK(nft_out(restored).rules.size() == 1);
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

TEST_CASE("lowering nftables: default gateway companion and OUTPUT rules") {
  FirewallRuleCriteria criteria;
  criteria.apply_output = true;
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
  SUBCASE("an OUTPUT-only rule has no companion") {
    FirewallRuleCriteria only_output = for_set("kpbr4d_x");
    only_output.apply_output = true;
    const auto lowered = lower_firewall_plan(
        plan_of({make_rule("route.drop", "o", Fam::ipv4, only_output,
                           VerdictAction::drop)}),
        nft_context());
    CHECK(nft_pre(lowered).rules.empty());
    REQUIRE(nft_out(lowered).rules.size() == 1);
    CHECK(verdict_of(nft_out(lowered).rules[0]) == PhysicalVerdict::drop);
  }
}

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

TEST_CASE("lowering nftables: prefilters") {
  auto plan = prefilter_plan(true, {"wg0", "lan0"});
  plan.rules.push_back(mark_rule("a", Fam::ipv4, for_set("kpbr4_x")));
  const auto set = lower_firewall_plan(plan, nft_context());
  const auto &pre = nft_pre(set).rules;
  const auto &out = nft_out(set).rules;
  // pre: restore, dnat, skip marked, inbound, route.  out: restore, skip marked.
  REQUIRE(pre.size() == 5);
  REQUIRE(out.size() == 2);
  CHECK(has_match(pre[1], CtStateMatch{ct_dnat, false}));
  CHECK(has_match(pre[3], IifMatch{true, {"lan0", "wg0"}}));
  CHECK(verdict_of(pre[3]) == PhysicalVerdict::accept);
  CHECK(has_match(out[1], MarkMatch{PhysicalMarkKind::packet, 0xFFFFFFFFu, true,
                                    {0}}));
  CHECK(pre[0] == out[0]);
}

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
                       {JumpStmt{ipt_chain("KeenPbrTable_A", PhysicalTable::mangle,
                                           Fam::ipv4),
                                 false}})) == "-A C -j KeenPbrTable_A\n");
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

} // namespace keen_pbr3
