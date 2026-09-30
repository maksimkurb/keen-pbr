#include "../firewall_rule_modules.hpp"

#include "../../routing/target.hpp"

#include <algorithm>
#include <string>

namespace keen_pbr3 {

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
