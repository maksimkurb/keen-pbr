#include "../firewall_rule_modules.hpp"

#include <utility>

namespace keen_pbr3 {

void SkipEstablishedOrDnatRuleModule::register_rules(
    const FirewallBuildContext& context, FirewallRuleRegistrar& registrar) const {
  if (!context.skip_established_or_dnat) {
    return;
  }
  FirewallRuleInstance rule;
  rule.key = FirewallRuleKey::compact(id(), "dnat");
  rule.stage = FirewallRuleStage::global_bypass;
  rule.priority = 0;
  rule.family = FirewallFamily::any;
  rule.action = SkipEstablishedOrDnatAction{};
  registrar.register_rule(std::move(rule));
}

} // namespace keen_pbr3
