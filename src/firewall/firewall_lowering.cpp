#include "firewall_lowering.hpp"

#include "ip_family.hpp"
#include <algorithm>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace keen_pbr3 {

// ===========================================================================
// Classifier expansion
// ===========================================================================

namespace {

FirewallFamily get_address_family(const std::string& address) {
  const auto family = ip_family_of(address);
  if (!family.has_value()) {
    throw FirewallError("invalid address in firewall rule criteria: " + address);
  }
  return *family;
}

bool needs_family_specific_rule(const FirewallRuleCriteria& criteria) {
  return criteria.dst_set_name.has_value() || criteria.dscp.has_value() ||
         !criteria.src_addr.empty() || !criteria.dst_addr.empty() ||
         !criteria.src_port.empty() || !criteria.dst_port.empty() ||
         criteria.default_gateway != DefaultGatewayFamily::None;
}

std::vector<FirewallFamily> materialization_families(
    const FirewallRuleInstance& rule, FirewallBackend backend) {
  if ((rule.family == FirewallFamily::ipv4 &&
       rule.criteria.default_gateway == DefaultGatewayFamily::Ipv6) ||
      (rule.family == FirewallFamily::ipv6 &&
       rule.criteria.default_gateway == DefaultGatewayFamily::Ipv4)) {
    return {};
  }
  if (rule.family != FirewallFamily::any) return {rule.family};
  if (rule.criteria.default_gateway == DefaultGatewayFamily::Ipv4) {
    return {FirewallFamily::ipv4};
  }
  if (rule.criteria.default_gateway == DefaultGatewayFamily::Ipv6) {
    return {FirewallFamily::ipv6};
  }
  if (backend == FirewallBackend::nftables &&
      std::holds_alternative<BalanceAction>(rule.action)) {
    return {FirewallFamily::ipv4, FirewallFamily::ipv6};
  }
  if (backend == FirewallBackend::nftables &&
      !needs_family_specific_rule(rule.criteria)) {
    return {FirewallFamily::ipv4};
  }
  return {FirewallFamily::ipv4, FirewallFamily::ipv6};
}

std::vector<L4Proto> materialization_protocols(
    const FirewallRuleCriteria& criteria, FirewallBackend backend) {
  if (criteria.proto == L4Proto::TcpUdp ||
      (backend == FirewallBackend::iptables && criteria.proto == L4Proto::Any &&
       (!criteria.src_port.empty() || !criteria.dst_port.empty()))) {
    return {L4Proto::Tcp, L4Proto::Udp};
  }
  return {criteria.proto};
}

std::vector<std::string> addresses_for_family(
    const std::vector<std::string>& addresses, FirewallFamily family) {
  std::vector<std::string> result;
  for (const auto& address : addresses) {
    if (get_address_family(address) == family) {
      result.push_back(address);
    }
  }
  return result;
}

std::optional<FirewallRuleAction> action_for_family(
    const FirewallRuleAction& action, FirewallFamily family,
    uint32_t fwmark_mask) {
  const auto* balance = std::get_if<BalanceAction>(&action);
  if (balance == nullptr) {
    if (std::holds_alternative<MarkAction>(action) ||
        std::holds_alternative<VerdictAction>(action)) {
      return action;
    }
    return std::nullopt;
  }

  BalanceAction filtered;
  filtered.fallback_mark = balance->fallback_mark;
  for (const auto& candidate : balance->candidates) {
    if ((family == FirewallFamily::ipv4 && candidate.ipv4) ||
        (family == FirewallFamily::ipv6 && candidate.ipv6)) {
      filtered.candidates.push_back(
          {candidate.fwmark, family == FirewallFamily::ipv4,
           family == FirewallFamily::ipv6});
    }
  }
  if (filtered.candidates.empty()) {
    return FirewallRuleAction{
        MarkAction{filtered.fallback_mark, fwmark_mask}};
  }
  if (filtered.candidates.size() == 1U) {
    return FirewallRuleAction{
        MarkAction{filtered.candidates.front().fwmark, fwmark_mask}};
  }
  // The specialized nft balance compiler owns multi-candidate forms; retain
  // the family-filtered action for the verifier's physical expectation.
  filtered.fallback_mark = 0;
  return FirewallRuleAction{std::move(filtered)};
}

} // namespace

std::vector<FirewallPhysicalClassifier> materialize_firewall_classifiers(
    const FirewallRuleInstance& rule, FirewallBackend backend,
    uint32_t fwmark_mask) {
  std::vector<FirewallPhysicalClassifier> result;
  for (const auto family : materialization_families(rule, backend)) {
    const auto src = addresses_for_family(rule.criteria.src_addr, family);
    const auto dst = addresses_for_family(rule.criteria.dst_addr, family);
    if ((!rule.criteria.src_addr.empty() && src.empty()) ||
        (!rule.criteria.dst_addr.empty() && dst.empty())) {
      continue;
    }

    const auto action = action_for_family(rule.action, family, fwmark_mask);
    if (!action.has_value()) continue;
    const auto protocols = materialization_protocols(rule.criteria, backend);
    const std::size_t src_count = backend == FirewallBackend::iptables
        ? std::max<std::size_t>(1U, src.size()) : 1U;
    const std::size_t dst_count = backend == FirewallBackend::iptables
        ? std::max<std::size_t>(1U, dst.size()) : 1U;
    for (const auto proto : protocols) {
      for (std::size_t src_index = 0; src_index < src_count; ++src_index) {
        for (std::size_t dst_index = 0; dst_index < dst_count; ++dst_index) {
          FirewallPhysicalClassifier physical;
          physical.family = family;
          physical.hook = rule.hook;
          physical.action = *action;
          physical.criteria = rule.criteria;
          physical.criteria.proto = proto;
          if (!rule.criteria.src_addr.empty()) {
            physical.criteria.src_addr = backend == FirewallBackend::iptables
                ? std::vector<std::string>{src[src_index]} : src;
          }
          if (!rule.criteria.dst_addr.empty()) {
            physical.criteria.dst_addr = backend == FirewallBackend::iptables
                ? std::vector<std::string>{dst[dst_index]} : dst;
          }

          const bool add_gateway_companion =
              backend == FirewallBackend::nftables &&
              rule.criteria.apply_output &&
              rule.criteria.default_gateway != DefaultGatewayFamily::None;
          result.push_back(physical);
          if (add_gateway_companion) {
            auto companion = physical;
            companion.hook = FirewallHook::prerouting;
            companion.criteria.apply_output = false;
            result.push_back(std::move(companion));
          }
        }
      }
    }
  }
  return result;
}


// ===========================================================================
// Chain naming
// ===========================================================================

const char *iptables_prerouting_generation_chain_name(
    bool raw, FirewallSetGeneration generation) {
  const bool a = generation == FirewallSetGeneration::A;
  if (raw) {
    return a ? "KeenPbrRaw_A" : "KeenPbrRaw_B";
  }
  return a ? "KeenPbrTable_A" : "KeenPbrTable_B";
}

const char *
iptables_output_generation_chain_name(FirewallSetGeneration generation) {
  return generation == FirewallSetGeneration::A ? "KeenPbrOutput_A"
                                                : "KeenPbrOutput_B";
}

PhysicalChainId iptables_physical_chain_id(const std::string &name,
                                           PhysicalTable table,
                                           FirewallFamily family) {
  if (auto id = classify_iptables_chain(name, table, family)) {
    return *id;
  }
  PhysicalChainId id;
  id.role = PhysicalChainRole::other_owned;
  id.table = table;
  id.family = family;
  id.name = name;
  return id;
}

std::string nft_setter_chain_name(uint32_t mark) {
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "setmark_%08x", mark);
  return buffer;
}

PhysicalChainId nft_physical_chain_id(PhysicalChainRole role, uint32_t mark) {
  PhysicalChainId id;
  id.role = role;
  id.table = PhysicalTable::nft_inet;
  id.family = FirewallFamily::any;
  switch (role) {
  case PhysicalChainRole::nft_prerouting:
    id.name = "prerouting";
    break;
  case PhysicalChainRole::nft_output:
    id.name = "output";
    break;
  case PhysicalChainRole::nft_setter:
    id.setter_mark = mark;
    id.name = nft_setter_chain_name(mark);
    break;
  default:
    break;
  }
  return id;
}

// ===========================================================================
// Lowering
// ===========================================================================

namespace {

using Matches = std::vector<PhysicalMatch>;
using Statements = std::vector<PhysicalStatement>;

constexpr uint32_t kFullMask = 0xFFFFFFFFu;

// Plan-wide facts that physical rules depend on.  The last action of a kind
// wins, like the singleton prefilter it replaces.
struct PlanFacts {
  uint32_t restore_mask{0};
  const std::vector<std::string> *inbound_interfaces{nullptr};
};

PlanFacts collect_plan_facts(const FirewallPlan &plan) {
  PlanFacts facts;
  for (const auto &rule : plan.rules) {
    if (const auto *restore =
            std::get_if<RestoreConntrackMarkAction>(&rule.action)) {
      facts.restore_mask = restore->mask;
    } else if (const auto *inbound =
                   std::get_if<InboundInterfaceFilterAction>(&rule.action)) {
      facts.inbound_interfaces = &inbound->interfaces;
    }
  }
  return facts;
}

bool is_classifier_action(const FirewallRuleAction &action) {
  return std::holds_alternative<MarkAction>(action) ||
         std::holds_alternative<BalanceAction>(action) ||
         std::holds_alternative<VerdictAction>(action);
}

std::optional<FirewallRuleKey> physical_key(const FirewallRuleKey &key,
                                            bool comments_supported) {
  if (!comments_supported ||
      (key.module_id.empty() && key.instance_id.empty())) {
    return std::nullopt;
  }
  return key;
}

PhysicalRule build_rule(FirewallFamily family, Matches matches,
                        Statements statements,
                        const std::optional<FirewallRuleKey> &key) {
  PhysicalRule rule;
  rule.family = family;
  rule.matches = std::move(matches);
  rule.statements = std::move(statements);
  rule.key = key;
  canonicalize_physical_rule(rule);
  return rule;
}

// result = (mark & ~mask) | value, with the value contained in the mask (the
// form nft prints back and iptables stores).
SetMarkStmt set_mark(PhysicalMarkKind kind, uint32_t value, uint32_t mask) {
  return SetMarkStmt{kind, value, mask | value};
}

MarkMatch mark_is_not_zero(PhysicalMarkKind kind, uint32_t mask) {
  return MarkMatch{kind, mask, true, {0}};
}

// Record the plan rule that produced the rules appended to `chain` since it
// held `from` rules.
void stamp_plan_rule(PhysicalChain &chain, std::size_t from,
                     std::size_t plan_index) {
  for (std::size_t i = from; i < chain.rules.size(); ++i) {
    chain.rules[i].plan_rule = static_cast<uint32_t>(plan_index);
  }
}

VerdictStmt verdict(PhysicalVerdict value) { return VerdictStmt{value}; }

bool has_family_payload(const FirewallRuleCriteria &criteria) {
  return criteria.dst_set_name.has_value() || criteria.dscp.has_value() ||
         !criteria.src_addr.empty() || !criteria.dst_addr.empty() ||
         criteria.default_gateway != DefaultGatewayFamily::None;
}

PhysicalTransport transport_for(L4Proto proto) {
  switch (proto) {
  case L4Proto::Tcp:
    return PhysicalTransport::tcp;
  case L4Proto::Udp:
    return PhysicalTransport::udp;
  default:
    return PhysicalTransport::any;
  }
}

// Expands one logical classifier rule into its physical classifier forms.
class ClassifierExpander {
public:
  ClassifierExpander(const FirewallPlan &plan,
                     const FirewallLoweringContext &context)
      : context_(context) {
    for (const auto &declaration : plan.sets) {
      set_families_.emplace(declaration.name, declaration.family);
    }
  }

  std::vector<FirewallPhysicalClassifier>
  expand(const FirewallRuleInstance &rule) const {
    const FirewallRuleInstance *effective = &rule;
    FirewallRuleInstance resolved;
    if (rule.family == FirewallFamily::any &&
        rule.criteria.dst_set_name.has_value()) {
      // A set is declared for one family; the rule follows it.
      const auto it = set_families_.find(*rule.criteria.dst_set_name);
      resolved = rule;
      resolved.family = it != set_families_.end() ? it->second
                                                  : FirewallFamily::ipv4;
      effective = &resolved;
    }
    auto result = materialize_firewall_classifiers(
        *effective, context_.backend, context_.fwmark_mask);
    if (!context_.ipv6_enabled) {
      result.erase(std::remove_if(result.begin(), result.end(),
                                  [](const FirewallPhysicalClassifier &c) {
                                    return c.family == FirewallFamily::ipv6;
                                  }),
                   result.end());
    }
    if (context_.physical_set_name) {
      for (auto &classifier : result) {
        auto &set = classifier.criteria.dst_set_name;
        if (set.has_value()) {
          set = context_.physical_set_name(*set);
        }
      }
    }
    return result;
  }

private:
  const FirewallLoweringContext &context_;
  std::map<std::string, FirewallFamily> set_families_;
};

// ---------------------------------------------------------------------------
// iptables
// ---------------------------------------------------------------------------

struct IptablesChain {
  PhysicalChain chain;
  FirewallFamily family{FirewallFamily::ipv4};
  // Conntrack based prefilters and CONNMARK saving need conntrack to have run:
  // not in raw PREROUTING.
  bool conntrack{true};
  bool comments{true};
};

// iptables multiport accepts at most 15 ports; a range takes two slots.
std::vector<std::vector<PortRange>>
chunk_port_ranges(const std::vector<PortRange> &ranges) {
  std::vector<std::vector<PortRange>> result;
  std::vector<PortRange> chunk;
  std::size_t slots = 0;
  for (const auto &range : ranges) {
    const std::size_t range_slots = range.from == range.to ? 1U : 2U;
    if (!chunk.empty() && slots + range_slots > 15U) {
      result.push_back(std::move(chunk));
      chunk.clear();
      slots = 0;
    }
    chunk.push_back(range);
    slots += range_slots;
  }
  if (!chunk.empty()) {
    result.push_back(std::move(chunk));
  }
  return result;
}

// One alternative per returned fragment (OR); matches inside a fragment are
// ANDed.  A positive list longer than one multiport splits into several rules,
// a negated one into several negated matches of the same rule.
std::vector<Matches> iptables_port_side(const PortSpec &spec,
                                        PhysicalTransport transport,
                                        PhysicalDir dir, bool negate) {
  if (spec.empty()) {
    return {Matches{}};
  }
  auto ranges = spec.ranges;
  canonicalize_port_ranges(ranges);
  if (ranges.size() == 1U) {
    return {Matches{PortMatch{transport, dir, negate, std::move(ranges)}}};
  }
  auto chunks = chunk_port_ranges(ranges);
  if (negate) {
    Matches all;
    for (auto &chunk : chunks) {
      all.push_back(PortMatch{transport, dir, true, std::move(chunk)});
    }
    return {std::move(all)};
  }
  std::vector<Matches> result;
  result.reserve(chunks.size());
  for (auto &chunk : chunks) {
    result.push_back(
        Matches{PortMatch{transport, dir, false, std::move(chunk)}});
  }
  return result;
}

std::vector<Matches> iptables_port_fragments(const FirewallRuleCriteria &c,
                                             PhysicalTransport transport) {
  const auto src = iptables_port_side(c.src_port, transport, PhysicalDir::src,
                                      c.negate_src_port);
  const auto dst = iptables_port_side(c.dst_port, transport, PhysicalDir::dst,
                                      c.negate_dst_port);
  std::vector<Matches> fragments;
  fragments.reserve(src.size() * dst.size());
  for (const auto &s : src) {
    for (const auto &d : dst) {
      Matches combined = s;
      combined.insert(combined.end(), d.begin(), d.end());
      fragments.push_back(std::move(combined));
    }
  }
  return fragments;
}

void lower_iptables_prefilter(const FirewallRuleInstance &rule,
                              IptablesChain &target) {
  const auto key = physical_key(rule.key, target.comments);
  const FirewallFamily family = target.family;
  auto &rules = target.chain.rules;
  if (const auto *restore =
          std::get_if<RestoreConntrackMarkAction>(&rule.action)) {
    // raw PREROUTING runs before conntrack: nothing to restore there.
    if (!target.conntrack || restore->mask == 0) {
      return;
    }
    const uint32_t mask = restore->mask;
    rules.push_back(build_rule(
        family,
        {mark_is_not_zero(PhysicalMarkKind::conntrack, mask),
         CtDirMatch{true}},
        {CopyMarkStmt{false, mask, mask}}, key));
    rules.push_back(build_rule(
        family,
        {mark_is_not_zero(PhysicalMarkKind::packet, mask), CtDirMatch{true}},
        {verdict(PhysicalVerdict::return_)}, key));
  } else if (std::holds_alternative<SkipEstablishedOrDnatAction>(rule.action)) {
    if (!target.conntrack) {
      return;
    }
    rules.push_back(build_rule(family, {CtStateMatch{ct_dnat, false}},
                               {verdict(PhysicalVerdict::return_)}, key));
  } else if (std::holds_alternative<SkipMarkedPacketsAction>(rule.action)) {
    rules.push_back(
        build_rule(family, {mark_is_not_zero(PhysicalMarkKind::packet, kFullMask)},
                   {verdict(PhysicalVerdict::accept)}, key));
  } else if (const auto *inbound =
                 std::get_if<InboundInterfaceFilterAction>(&rule.action)) {
    // iptables cannot express a multi-value negated -i guard in one rule;
    // multi-interface allowlists become per-interface fragments of every
    // classifier rule instead.
    if (inbound->interfaces.size() != 1U) {
      return;
    }
    rules.push_back(build_rule(
        family, {IifMatch{true, {inbound->interfaces.front()}}},
        {verdict(PhysicalVerdict::return_)}, key));
  }
}

void lower_iptables_classifier(const FirewallRuleInstance &rule,
                               const FirewallPhysicalClassifier &classifier,
                               const PlanFacts &facts, uint32_t fwmark_mask,
                               IptablesChain &target) {
  const auto &criteria = classifier.criteria;
  const auto key = physical_key(rule.key, target.comments);

  Matches base;
  if (criteria.dst_set_name.has_value()) {
    base.push_back(SetMatch{*criteria.dst_set_name, PhysicalDir::dst, false});
  }
  if (!criteria.src_addr.empty()) {
    base.push_back(AddrMatch{PhysicalDir::src, criteria.negate_src_addr,
                             criteria.src_addr});
  }
  if (!criteria.dst_addr.empty()) {
    base.push_back(AddrMatch{PhysicalDir::dst, criteria.negate_dst_addr,
                             criteria.dst_addr});
  }
  if (criteria.dscp.has_value()) {
    base.push_back(DscpMatch{*criteria.dscp});
  }
  if (criteria.proto != L4Proto::Any) {
    base.push_back(ProtoMatch{criteria.proto});
  }

  std::vector<const std::string *> interfaces;
  if (facts.inbound_interfaces != nullptr &&
      facts.inbound_interfaces->size() > 1U) {
    for (const auto &name : *facts.inbound_interfaces) {
      interfaces.push_back(&name);
    }
  } else {
    interfaces.push_back(nullptr);
  }

  const auto *mark = std::get_if<MarkAction>(&classifier.action);
  const auto *verdict_action = std::get_if<VerdictAction>(&classifier.action);
  const bool save_conntrack =
      mark != nullptr && target.conntrack && facts.restore_mask != 0;

  auto &rules = target.chain.rules;
  for (const auto &ports :
       iptables_port_fragments(criteria, transport_for(criteria.proto))) {
    for (const auto *interface : interfaces) {
      Matches matches = base;
      matches.insert(matches.end(), ports.begin(), ports.end());
      if (interface != nullptr) {
        matches.push_back(IifMatch{false, {*interface}});
      }
      if (mark != nullptr) {
        rules.push_back(build_rule(
            target.family, matches,
            {set_mark(PhysicalMarkKind::packet, mark->value, fwmark_mask)},
            key));
        if (save_conntrack) {
          rules.push_back(build_rule(
              target.family, matches,
              {CopyMarkStmt{true, facts.restore_mask, facts.restore_mask}},
              key));
        }
        rules.push_back(build_rule(target.family, std::move(matches),
                                   {verdict(PhysicalVerdict::return_)}, key));
      } else {
        const bool drop = verdict_action != nullptr &&
                          *verdict_action == VerdictAction::drop;
        rules.push_back(build_rule(
            target.family, std::move(matches),
            {verdict(drop ? PhysicalVerdict::drop : PhysicalVerdict::return_)},
            key));
      }
    }
  }
}

PhysicalRuleset lower_iptables(const FirewallPlan &plan,
                               const FirewallLoweringContext &context) {
  for (const auto &rule : plan.rules) {
    if (std::holds_alternative<BalanceAction>(rule.action)) {
      throw FirewallError(
          "connection balancing requires the nftables firewall backend");
    }
    if (rule.criteria.default_gateway != DefaultGatewayFamily::None) {
      throw FirewallError(
          "default_gateway requires the nftables firewall backend");
    }
  }

  std::vector<IptablesChain> chains;
  const auto add_family = [&](FirewallFamily family) {
    const bool ipv6 = family == FirewallFamily::ipv6;
    const auto generation =
        ipv6 ? context.generation_ipv6 : context.generation_ipv4;
    const bool comments = ipv6 ? context.comments_ipv6_supported
                               : context.comments_ipv4_supported;
    if (context.raw_prerouting.uses(ipv6)) {
      // OUTPUT stays in mangle and keeps the conntrack optimizations.
      IptablesChain output;
      output.chain.id = iptables_physical_chain_id(
          iptables_output_generation_chain_name(generation),
          PhysicalTable::mangle, family);
      output.family = family;
      output.conntrack = true;
      output.comments = comments;
      chains.push_back(std::move(output));
      IptablesChain raw;
      raw.chain.id = iptables_physical_chain_id(
          iptables_prerouting_generation_chain_name(true, generation),
          PhysicalTable::raw, family);
      raw.family = family;
      raw.conntrack = false;
      raw.comments = comments;
      chains.push_back(std::move(raw));
    } else {
      IptablesChain mangle;
      mangle.chain.id = iptables_physical_chain_id(
          iptables_prerouting_generation_chain_name(false, generation),
          PhysicalTable::mangle, family);
      mangle.family = family;
      mangle.conntrack = true;
      mangle.comments = comments;
      chains.push_back(std::move(mangle));
    }
  };
  add_family(FirewallFamily::ipv4);
  if (context.ipv6_enabled) {
    add_family(FirewallFamily::ipv6);
  }

  const PlanFacts facts = collect_plan_facts(plan);
  const ClassifierExpander expander(plan, context);
  for (std::size_t index = 0; index < plan.rules.size(); ++index) {
    const auto &rule = plan.rules[index];
    std::vector<std::size_t> before;
    before.reserve(chains.size());
    for (const auto &target : chains) {
      before.push_back(target.chain.rules.size());
    }
    if (is_classifier_action(rule.action)) {
      for (const auto &classifier : expander.expand(rule)) {
        for (auto &target : chains) {
          if (target.family == classifier.family) {
            lower_iptables_classifier(rule, classifier, facts,
                                      context.fwmark_mask, target);
          }
        }
      }
    } else {
      for (auto &target : chains) {
        lower_iptables_prefilter(rule, target);
      }
    }
    for (std::size_t i = 0; i < chains.size(); ++i) {
      stamp_plan_rule(chains[i].chain, before[i], index);
    }
  }

  PhysicalRuleset result;
  result.chains.reserve(chains.size());
  for (auto &target : chains) {
    result.chains.push_back(std::move(target.chain));
  }
  return result;
}

// ---------------------------------------------------------------------------
// nftables
// ---------------------------------------------------------------------------

Matches nft_criteria_matches(const FirewallRuleCriteria &criteria) {
  Matches matches;
  if (criteria.dst_set_name.has_value()) {
    matches.push_back(SetMatch{*criteria.dst_set_name, PhysicalDir::dst, false});
  }
  if (criteria.dscp.has_value()) {
    matches.push_back(DscpMatch{*criteria.dscp});
  }
  if (!criteria.src_addr.empty()) {
    matches.push_back(AddrMatch{PhysicalDir::src, criteria.negate_src_addr,
                                criteria.src_addr});
  }
  if (!criteria.dst_addr.empty()) {
    matches.push_back(AddrMatch{PhysicalDir::dst, criteria.negate_dst_addr,
                                criteria.dst_addr});
  }
  if (criteria.default_gateway != DefaultGatewayFamily::None &&
      !criteria.default_gateway_bypass.empty()) {
    // The default gateway rule applies to everything except the bypass list.
    matches.push_back(
        AddrMatch{PhysicalDir::dst, true, criteria.default_gateway_bypass});
  }
  if (criteria.proto != L4Proto::Any) {
    matches.push_back(ProtoMatch{criteria.proto});
  }
  const PhysicalTransport transport = transport_for(criteria.proto);
  if (!criteria.src_port.empty()) {
    matches.push_back(PortMatch{transport, PhysicalDir::src,
                                criteria.negate_src_port,
                                criteria.src_port.ranges});
  }
  if (!criteria.dst_port.empty()) {
    matches.push_back(PortMatch{transport, PhysicalDir::dst,
                                criteria.negate_dst_port,
                                criteria.dst_port.ranges});
  }
  return matches;
}

// Marks that need a setter chain: the owned marks plus every mark a lowered
// classifier can install.
std::set<uint32_t>
collect_setter_marks(const FirewallPlan &plan,
                     const FirewallLoweringContext &context,
                     const ClassifierExpander &expander) {
  std::set<uint32_t> marks;
  for (const uint32_t mark : context.owned_marks) {
    if (mark != 0) marks.insert(mark);
  }
  for (const auto &rule : plan.rules) {
    if (!is_classifier_action(rule.action)) continue;
    for (const auto &classifier : expander.expand(rule)) {
      if (const auto *mark = std::get_if<MarkAction>(&classifier.action)) {
        if (mark->value != 0) marks.insert(mark->value);
      } else if (const auto *balance =
                     std::get_if<BalanceAction>(&classifier.action)) {
        for (const auto &candidate : balance->candidates) {
          marks.insert(candidate.fwmark);
        }
      }
    }
  }
  return marks;
}

PhysicalRuleset lower_nftables(const FirewallPlan &plan,
                               const FirewallLoweringContext &context) {
  const ClassifierExpander expander(plan, context);
  const PlanFacts facts = collect_plan_facts(plan);
  const std::set<uint32_t> setter_marks =
      collect_setter_marks(plan, context, expander);
  const bool jump_to_setters = facts.restore_mask != 0;

  PhysicalChain prerouting;
  prerouting.id = nft_physical_chain_id(PhysicalChainRole::nft_prerouting);
  prerouting.base = PhysicalBaseChain{PhysicalBaseChain::Type::filter,
                                      PhysicalBaseChain::Hook::prerouting, -150,
                                      true};
  PhysicalChain output;
  output.id = nft_physical_chain_id(PhysicalChainRole::nft_output);
  output.base = PhysicalBaseChain{PhysicalBaseChain::Type::route,
                                  PhysicalBaseChain::Hook::output, -150, true};

  for (std::size_t plan_index = 0; plan_index < plan.rules.size();
       ++plan_index) {
    const auto &rule = plan.rules[plan_index];
    const std::size_t prerouting_before = prerouting.rules.size();
    const std::size_t output_before = output.rules.size();
    const auto key = physical_key(rule.key, true);
    const FirewallFamily any = FirewallFamily::any;
    if (const auto *restore =
            std::get_if<RestoreConntrackMarkAction>(&rule.action)) {
      if (restore->mask == 0 || setter_marks.empty()) continue;
      // Restore the owned part of the connection mark: jump to the setter of
      // the remembered mark, and stop only when one of ours was found.
      VmapStmt vmap;
      vmap.key = PhysicalVmapKey::conntrack_mark_and;
      vmap.param = restore->mask;
      std::vector<uint32_t> known;
      for (const uint32_t mark : setter_marks) {
        vmap.entries.emplace_back(
            mark, nft_physical_chain_id(PhysicalChainRole::nft_setter, mark));
        known.push_back(mark);
      }
      for (auto *chain : {&prerouting, &output}) {
        chain->rules.push_back(build_rule(
            any,
            {mark_is_not_zero(PhysicalMarkKind::conntrack, restore->mask),
             CtDirMatch{true}},
            {vmap,
             LateMatchStmt{MarkMatch{PhysicalMarkKind::conntrack, restore->mask,
                                     false, known}},
             verdict(PhysicalVerdict::accept)},
            key));
      }
    } else if (std::holds_alternative<SkipEstablishedOrDnatAction>(
                   rule.action)) {
      prerouting.rules.push_back(build_rule(
          any, {CtStateMatch{ct_dnat, false}},
          {verdict(PhysicalVerdict::accept)}, key));
    } else if (std::holds_alternative<SkipMarkedPacketsAction>(rule.action)) {
      for (auto *chain : {&prerouting, &output}) {
        chain->rules.push_back(build_rule(
            any, {mark_is_not_zero(PhysicalMarkKind::packet, kFullMask)},
            {verdict(PhysicalVerdict::accept)}, key));
      }
    } else if (const auto *inbound =
                   std::get_if<InboundInterfaceFilterAction>(&rule.action)) {
      prerouting.rules.push_back(build_rule(
          any, {IifMatch{true, inbound->interfaces}},
          {verdict(PhysicalVerdict::accept)}, key));
    } else {
      const bool balance_origin =
          std::holds_alternative<BalanceAction>(rule.action);
      for (const auto &classifier : expander.expand(rule)) {
        const auto &criteria = classifier.criteria;
        // The family guard is only a separate `meta nfproto` match when
        // nothing in the rule implies the family; balance rules always carry
        // one because their candidates are filtered per family.
        const FirewallFamily family =
            balance_origin || has_family_payload(criteria) ? classifier.family
                                                           : any;
        Matches matches = nft_criteria_matches(criteria);
        Statements statements;
        if (const auto *mark = std::get_if<MarkAction>(&classifier.action)) {
          if (jump_to_setters) {
            statements.push_back(JumpStmt{
                nft_physical_chain_id(PhysicalChainRole::nft_setter,
                                      mark->value),
                false});
          } else {
            statements.push_back(set_mark(PhysicalMarkKind::packet, mark->value,
                                          context.fwmark_mask));
            statements.push_back(verdict(PhysicalVerdict::accept));
          }
        } else if (const auto *balance =
                       std::get_if<BalanceAction>(&classifier.action)) {
          // Only packets that are not marked yet are balanced.
          matches.push_back(
              MarkMatch{PhysicalMarkKind::packet, context.fwmark_mask, false,
                        {0}});
          VmapStmt vmap;
          vmap.key = PhysicalVmapKey::numgen_inc;
          vmap.param = static_cast<uint32_t>(balance->candidates.size());
          for (std::size_t index = 0; index < balance->candidates.size();
               ++index) {
            vmap.entries.emplace_back(
                static_cast<uint32_t>(index),
                nft_physical_chain_id(PhysicalChainRole::nft_setter,
                                      balance->candidates[index].fwmark));
          }
          statements.push_back(std::move(vmap));
          statements.push_back(verdict(PhysicalVerdict::accept));
        } else {
          const bool drop =
              std::get<VerdictAction>(classifier.action) == VerdictAction::drop;
          statements.push_back(
              verdict(drop ? PhysicalVerdict::drop : PhysicalVerdict::accept));
        }
        const bool to_output =
            criteria.apply_output || classifier.hook == FirewallHook::output;
        (to_output ? output : prerouting)
            .rules.push_back(build_rule(family, std::move(matches),
                                        std::move(statements), key));
      }
    }
    stamp_plan_rule(prerouting, prerouting_before, plan_index);
    stamp_plan_rule(output, output_before, plan_index);
  }

  PhysicalRuleset result;
  result.chains.push_back(std::move(prerouting));
  result.chains.push_back(std::move(output));
  for (const uint32_t mark : setter_marks) {
    PhysicalChain setter;
    setter.id = nft_physical_chain_id(PhysicalChainRole::nft_setter, mark);
    // Replace the owned bits in both the packet and the connection mark, then
    // stop: the packet is classified.
    setter.rules.push_back(build_rule(
        FirewallFamily::any, {},
        {set_mark(PhysicalMarkKind::packet, mark, context.fwmark_mask),
         set_mark(PhysicalMarkKind::conntrack, mark, context.fwmark_mask),
         verdict(PhysicalVerdict::accept)},
        std::nullopt));
    result.chains.push_back(std::move(setter));
  }
  return result;
}

} // namespace

PhysicalRuleset lower_firewall_plan(const FirewallPlan &plan,
                                    const FirewallLoweringContext &context) {
  return context.backend == FirewallBackend::nftables
             ? lower_nftables(plan, context)
             : lower_iptables(plan, context);
}

} // namespace keen_pbr3
