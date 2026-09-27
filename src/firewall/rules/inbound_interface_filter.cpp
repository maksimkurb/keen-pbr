#include "../firewall_rule_modules.hpp"

#include <string>
#include <utility>

namespace keen_pbr3 {

void InboundInterfaceFilterRuleModule::register_rules(
    const FirewallBuildContext& context, FirewallRuleRegistrar& registrar) const {
  if (context.inbound_interfaces.empty()) {
    return;
  }
  FirewallRuleInstance rule;
  std::string semantic_instance;
  for (const auto& interface : context.inbound_interfaces) {
    semantic_instance += interface;
    semantic_instance.push_back(';');
  }
  rule.key = FirewallRuleKey::compact(id(), semantic_instance);
  rule.stage = FirewallRuleStage::global_bypass;
  rule.priority = 2;
  rule.family = FirewallFamily::any;
  rule.action = InboundInterfaceFilterAction{context.inbound_interfaces};
  registrar.register_rule(std::move(rule));
}

} // namespace keen_pbr3
