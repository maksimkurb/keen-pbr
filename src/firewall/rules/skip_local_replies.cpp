#include "../firewall_rule_modules.hpp"

#include <utility>

namespace keen_pbr3 {

namespace {
constexpr std::string_view kModuleId = "prefilter.skip_local_replies";
}

void register_skip_local_replies_rules(const FirewallBuildContext& context,
                                       FirewallRuleRegistrar& registrar) {
  // Always enabled. Route rules apply to router-originated traffic, but the
  // answers of local services (dnsmasq, uhttpd, sshd, our API) to inbound
  // connections must leave through the interface they came from.
  (void)context;
  FirewallRuleInstance rule;
  rule.key = FirewallRuleKey::compact(kModuleId, "reply");
  rule.stage = FirewallRuleStage::global_bypass;
  rule.priority = -1;
  rule.hook = FirewallHook::output;
  rule.family = FirewallFamily::any;
  rule.action = SkipLocalRepliesAction{};
  registrar.register_rule(std::move(rule));
}

} // namespace keen_pbr3
