#include "../firewall_rule_modules.hpp"
#include "../firewall_snapshot.hpp"

#include "../../routing/target.hpp"
#include "../../util/format_compat.hpp"

#include <algorithm>
#include <string>

namespace keen_pbr3 {

std::string balance_rule_mismatch_detail(const ObservedFirewallRule& observed,
                                         const BalanceAction& expected,
                                         uint32_t fwmark_mask) {
  const auto* actual_action = std::get_if<BalanceAction>(&observed.action);
  if (actual_action == nullptr) {
    return "balance action type mismatch";
  }
  if ((actual_action->fallback_mark != 0 &&
       actual_action->fallback_mark != expected.fallback_mark) ||
      actual_action->candidates.size() != expected.candidates.size() ||
      !std::equal(
          actual_action->candidates.begin(), actual_action->candidates.end(),
          expected.candidates.begin(),
          [](const FirewallBalanceCandidate& lhs,
             const FirewallBalanceCandidate& rhs) {
            return lhs.fwmark == rhs.fwmark;
          })) {
    return "balance candidate mapping mismatch";
  }
  if (!observed.balance.has_value()) {
    return "balance details missing from nft snapshot";
  }
  const auto& actual = *observed.balance;
  if (actual.selector_mode != "inc") {
    return keen_pbr3::format("balance selector mode mismatch: expected inc got {}",
                             actual.selector_mode.empty() ? "<missing>"
                                                           : actual.selector_mode);
  }
  if (actual.selector_modulus != expected.candidates.size()) {
    return keen_pbr3::format(
        "balance selector modulus mismatch: expected {} got {}",
        expected.candidates.size(), actual.selector_modulus);
  }
  if (!actual.mark_guard_present) {
    return "balance owned-mark-empty guard missing";
  }
  if (actual.mark_guard_op != "==" || actual.mark_guard_value != 0) {
    return "balance owned-mark-empty guard operator/value mismatch";
  }
  if (actual.mark_guard_mask != fwmark_mask) {
    return keen_pbr3::format(
        "balance owned-mark-empty guard mask mismatch: expected {:#x} got {:#x}",
        fwmark_mask, actual.mark_guard_mask);
  }
  const auto candidate_count = expected.candidates.size();
  if (actual.target_indices.size() != candidate_count) {
    return "balance vmap target count mismatch";
  }
  if (actual.target_marks.size() != candidate_count) {
    return "balance vmap mark count mismatch";
  }
  if (actual.setter_actions.size() != candidate_count) {
    return "balance setter count mismatch";
  }
  if (actual.setter_ct_actions.size() != candidate_count) {
    return "balance conntrack setter count mismatch";
  }
  for (std::size_t index = 0; index < candidate_count; ++index) {
    if (actual.target_indices[index] != index) {
      return keen_pbr3::format(
          "balance vmap index/order mismatch at position {}", index);
    }
    if (actual.target_marks[index] != expected.candidates[index].fwmark) {
      return keen_pbr3::format(
          "balance vmap mark mismatch at index {}: expected {:#x} got {:#x}",
          index, expected.candidates[index].fwmark, actual.target_marks[index]);
    }
    if (!actual.setter_actions[index].has_value()) {
      return keen_pbr3::format("balance setter missing at index {}", index);
    }
    if (actual.setter_actions[index]->value != expected.candidates[index].fwmark) {
      return keen_pbr3::format(
          "balance setter value mismatch at index {}: expected {:#x} got {:#x}",
          index, expected.candidates[index].fwmark,
          actual.setter_actions[index]->value);
    }
    if (actual.setter_actions[index]->mask != fwmark_mask) {
      return keen_pbr3::format(
          "balance setter mask mismatch at index {}: expected {:#x} got {:#x}",
          index, fwmark_mask, actual.setter_actions[index]->mask);
    }
    if (!actual.setter_ct_actions[index].has_value()) {
      return keen_pbr3::format("balance conntrack setter missing at index {}", index);
    }
    if (actual.setter_ct_actions[index]->value != expected.candidates[index].fwmark) {
      return keen_pbr3::format(
          "balance conntrack setter value mismatch at index {}: expected {:#x} got {:#x}",
          index, expected.candidates[index].fwmark,
          actual.setter_ct_actions[index]->value);
    }
    if (actual.setter_ct_actions[index]->mask != fwmark_mask) {
      return keen_pbr3::format(
          "balance conntrack setter mask mismatch at index {}: expected {:#x} got {:#x}",
          index, fwmark_mask, actual.setter_ct_actions[index]->mask);
    }
  }
  return {};
}

void RouteBalanceRuleModule::register_rules(
    const FirewallBuildContext& context, FirewallRuleRegistrar& registrar) const {
  for (std::size_t rule_index = 0; rule_index < context.route_rules.size();
       ++rule_index) {
    const auto& route_rule = context.route_rules[rule_index];
    if (!route_rule_enabled(route_rule)) {
      continue;
    }
    const auto decision =
        resolve_route_action(route_rule.outbound, context.outbounds);
    if (!decision.outbound.has_value() || !*decision.outbound ||
        !outbound_uses_balance(**decision.outbound) ||
        context.outbound_marks == nullptr) {
      continue;
    }
    const auto mark_it = context.outbound_marks->find((*decision.outbound)->tag);
    if (mark_it == context.outbound_marks->end() || mark_it->second == 0) {
      continue;
    }

    if (context.backend == FirewallBackend::iptables &&
        std::any_of(route_rule_lists(route_rule).begin(),
                    route_rule_lists(route_rule).end(), [&](const auto& list_name) {
                      return context.lists.find(list_name) != context.lists.end();
                    })) {
      throw FirewallError(
          "unsupported firewall construct: module_id=route.balance, rule=" +
          std::to_string(rule_index) +
          ", backend=iptables, construct=BalanceAction (requires nftables)");
    }

    // Preserve the prepared vector; nftables owns zero/one/many candidate
    // expansion, filtering, and fallback compilation.
    static const std::vector<FirewallBalanceCandidate> empty_candidates;
    const auto candidates = context.balance_candidates == nullptr
                                ? empty_candidates
                                : [&] {
                                    const auto it = context.balance_candidates->find(
                                        (*decision.outbound)->tag);
                                    return it == context.balance_candidates->end()
                                               ? empty_candidates
                                               : it->second;
                                  }();
    register_route_rule_targets(
        context, registrar, id(), rule_index,
        FirewallRuleAction{BalanceAction{mark_it->second, candidates}});
  }
}

} // namespace keen_pbr3
