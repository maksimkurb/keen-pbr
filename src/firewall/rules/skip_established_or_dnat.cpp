#include "../firewall_rule_modules.hpp"

#include <utility>

namespace keen_pbr3 {

namespace {
constexpr std::string_view kModuleId = "prefilter.skip_established_or_dnat";
}

void register_skip_established_or_dnat_rules(const FirewallBuildContext& context,
                                             FirewallRuleRegistrar& registrar) {
  // Always enabled: DNATed and established flows keep their existing path.
  (void)context;
  FirewallRuleInstance rule;
  rule.key = FirewallRuleKey::compact(kModuleId, "dnat");
  rule.stage = FirewallRuleStage::global_bypass;
  rule.priority = 0;
  rule.family = FirewallFamily::any;
  rule.action = SkipEstablishedOrDnatAction{};
  registrar.register_rule(std::move(rule));
}

} // namespace keen_pbr3
