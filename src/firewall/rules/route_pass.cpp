#include "../firewall_rule_modules.hpp"

#include "../../routing/target.hpp"

namespace keen_pbr3 {

namespace {
constexpr std::string_view kModuleId = "route.pass";
}

void register_route_pass_rules(const FirewallBuildContext& context,
                               FirewallRuleRegistrar& registrar) {
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
        context, registrar, kModuleId, rule_index,
        FirewallRuleAction{VerdictAction::pass});
  }
}

} // namespace keen_pbr3
