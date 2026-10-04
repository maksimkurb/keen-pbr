#include "../firewall_rule_modules.hpp"

#include <utility>

namespace keen_pbr3 {

namespace {
constexpr std::string_view kModuleId = "prefilter.skip_local_replies";
}

void register_skip_local_replies_rules(const FirewallBuildContext& context,
                                       FirewallRuleRegistrar& registrar) {
  // Always enabled. Reply-direction packets are never classified:
  //  * OUTPUT: route rules apply to router-originated traffic, but the
  //    answers of local services (dnsmasq, uhttpd, sshd, our API) to inbound
  //    connections must leave through the interface they came from;
  //  * PREROUTING: forwarded replies (a WAN server answering a LAN client)
  //    must not be re-marked by a catch-all rule when
  //    route.inbound_interfaces is empty.
  // Needs conntrack, so it is absent from raw PREROUTING.
  (void)context;
  FirewallRuleInstance rule;
  rule.key = FirewallRuleKey::compact(kModuleId, "reply");
  rule.stage = FirewallRuleStage::global_bypass;
  rule.priority = -1;
  rule.hook = FirewallHook::prerouting;
  rule.family = FirewallFamily::any;
  rule.action = SkipLocalRepliesAction{};
  registrar.register_rule(std::move(rule));
}

} // namespace keen_pbr3
