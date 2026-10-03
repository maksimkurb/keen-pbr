#include "../firewall_rule_modules.hpp"

#include "../../routing/target.hpp"

namespace keen_pbr3 {

namespace {
constexpr std::string_view kModuleId = "route.mark";
}

void register_route_mark_rules(const FirewallBuildContext& context,
                               FirewallRuleRegistrar& registrar) {
  if (context.outbound_marks == nullptr) {
    return;
  }

  for (std::size_t rule_index = 0; rule_index < context.route_rules.size();
       ++rule_index) {
    const auto& route_rule = context.route_rules[rule_index];
    if (!route_rule_enabled(route_rule)) {
      continue;
    }

    const auto decision =
        resolve_route_action(route_rule.outbound, context.outbounds);
    if (decision.is_skip || decision.is_passthrough ||
        !decision.outbound.has_value() || !*decision.outbound) {
      continue;
    }

    const auto& outbound = **decision.outbound;
    if (outbound.type == OutboundType::BLACKHOLE ||
        outbound_uses_balance(outbound)) {
      continue;
    }

    const auto mark_it = context.outbound_marks->find(outbound.tag);
    const uint32_t fwmark = mark_it == context.outbound_marks->end()
                                ? 0
                                : mark_it->second;
    register_route_rule_targets(
        context, registrar, kModuleId, rule_index,
        FirewallRuleAction{MarkAction{fwmark, context.fwmark_mask}},
        fwmark != 0);
  }
}

} // namespace keen_pbr3
