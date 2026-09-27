#include "../firewall_rule_modules.hpp"

#include <string>
#include <utility>

namespace keen_pbr3 {

void RestoreConntrackMarkRuleModule::register_rules(
    const FirewallBuildContext& context, FirewallRuleRegistrar& registrar) const {
  if (!context.restore_conntrack_mark || context.fwmark_mask == 0) {
    return;
  }
  FirewallRuleInstance rule;
  rule.key = FirewallRuleKey::compact(id(), "mask=" +
                                               std::to_string(context.fwmark_mask));
  rule.stage = FirewallRuleStage::restore_conntrack;
  rule.priority = 0;
  rule.family = FirewallFamily::any;
  rule.action = RestoreConntrackMarkAction{context.fwmark_mask};
  registrar.register_rule(std::move(rule));
}

} // namespace keen_pbr3
