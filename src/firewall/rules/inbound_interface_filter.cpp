#include "../firewall_rule_modules.hpp"

#include <string>
#include <vector>
#include <utility>

namespace keen_pbr3 {

namespace {
constexpr std::string_view kModuleId = "prefilter.inbound_interface";

// Missing or empty route.inbound_interfaces leaves the restriction disabled.
const std::vector<std::string>* configured_inbound_interfaces(
    const FirewallBuildContext& context) {
  if (context.config == nullptr || !context.config->route.has_value()) {
    return nullptr;
  }
  const auto& interfaces = context.config->route->inbound_interfaces;
  return interfaces.has_value() && !interfaces->empty() ? &*interfaces
                                                        : nullptr;
}
}

void register_inbound_interface_filter_rules(const FirewallBuildContext& context,
                                             FirewallRuleRegistrar& registrar) {
  const auto* interfaces = configured_inbound_interfaces(context);
  if (interfaces == nullptr) {
    return;
  }
  FirewallRuleInstance rule;
  std::string semantic_instance;
  for (const auto& interface : *interfaces) {
    semantic_instance += interface;
    semantic_instance.push_back(';');
  }
  rule.key = FirewallRuleKey::compact(kModuleId, semantic_instance);
  rule.stage = FirewallRuleStage::global_bypass;
  rule.priority = 2;
  rule.family = FirewallFamily::any;
  rule.action = InboundInterfaceFilterAction{*interfaces};
  registrar.register_rule(std::move(rule));
}

} // namespace keen_pbr3
