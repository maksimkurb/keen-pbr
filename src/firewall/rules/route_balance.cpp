#include "../firewall_rule_modules.hpp"

#include "../../routing/target.hpp"

namespace keen_pbr3 {

namespace {
constexpr std::string_view kModuleId = "route.balance";
}

void register_route_balance_rules(const FirewallBuildContext& context,
                                  FirewallRuleRegistrar& registrar) {
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

    // Preserve the prepared vector; lowering owns zero/one/many candidate
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
        context, registrar, kModuleId, rule_index,
        FirewallRuleAction{BalanceAction{mark_it->second, candidates}});
  }
}

} // namespace keen_pbr3
