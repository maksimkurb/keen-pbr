#include "../firewall_rule_modules.hpp"

#include <utility>

namespace keen_pbr3 {

namespace {
constexpr std::string_view kModuleId = "prefilter.skip_marked_packets";

// daemon.skip_marked_packets defaults to enabled, including without a config.
bool skip_marked_packets_enabled(const FirewallBuildContext& context) {
  if (context.config == nullptr || !context.config->daemon.has_value()) {
    return true;
  }
  return context.config->daemon->skip_marked_packets.value_or(true);
}
}

void register_skip_marked_packets_rules(const FirewallBuildContext& context,
                                        FirewallRuleRegistrar& registrar) {
  if (!skip_marked_packets_enabled(context)) {
    return;
  }
  FirewallRuleInstance rule;
  rule.key = FirewallRuleKey::compact(kModuleId, "all");
  rule.stage = FirewallRuleStage::global_bypass;
  rule.priority = 1;
  rule.family = FirewallFamily::any;
  rule.action = SkipMarkedPacketsAction{};
  registrar.register_rule(std::move(rule));
}

} // namespace keen_pbr3
