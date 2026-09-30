#include "../firewall_rule_modules.hpp"

#include "../../routing/target.hpp"

namespace keen_pbr3 {

void RoutePassRuleModule::register_rules(
    const FirewallBuildContext& context, FirewallRuleRegistrar& registrar) const {
  for (std::size_t rule_index = 0; rule_index < context.route_rules.size();
       ++rule_index) {
    const auto& route_rule = context.route_rules[rule_index];
    if (!route_rule_enabled(route_rule)) {
      continue;
    }
    const auto decision =
        resolve_route_action(route_rule.outbound, context.outbounds);
    if (!decision.is_passthrough) {
      continue;
    }
    register_route_rule_targets(
        context, registrar, id(), rule_index,
        FirewallRuleAction{VerdictAction::pass});
  }
}

} // namespace keen_pbr3
