#include "../firewall_rule_modules.hpp"

#include <utility>

namespace keen_pbr3 {

void SkipMarkedPacketsRuleModule::register_rules(
    const FirewallBuildContext& context, FirewallRuleRegistrar& registrar) const {
  if (!context.skip_marked_packets) {
    return;
  }
  FirewallRuleInstance rule;
  rule.key = FirewallRuleKey::compact(id(), "all");
  rule.stage = FirewallRuleStage::global_bypass;
  rule.priority = 1;
  rule.family = FirewallFamily::any;
  rule.action = SkipMarkedPacketsAction{};
  registrar.register_rule(std::move(rule));
}

} // namespace keen_pbr3
