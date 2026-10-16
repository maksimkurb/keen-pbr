#include "../firewall_rule_modules.hpp"

namespace keen_pbr3 {
namespace {

constexpr std::string_view kModuleId = "prefilter.dhcp_bypass";

FirewallRuleInstance dhcp_rule(FirewallFamily family, const char* instance,
                               const char* ports) {
  FirewallRuleInstance rule;
  rule.key = FirewallRuleKey::compact(kModuleId, instance);
  // This pre-restore priority keeps DHCP out of both connmark restoration and
  // route marking while reusing the existing pass/RETURN lowering.
  rule.stage = FirewallRuleStage::restore_conntrack;
  rule.priority = -1;
  rule.family = family;
  // Keep the family explicit on nftables, where UDP ports alone are neutral.
  rule.criteria.src_addr = {family == FirewallFamily::ipv4 ? "0.0.0.0/0" : "::/0"};
  rule.criteria.proto = L4Proto::Udp;
  rule.criteria.src_port = ports;
  rule.criteria.dst_port = ports;
  rule.action = VerdictAction::pass;
  return rule;
}

} // namespace

void register_dhcp_bypass_rules(const FirewallBuildContext& context,
                                FirewallRuleRegistrar& registrar) {
  (void)context;
  registrar.register_rule(dhcp_rule(FirewallFamily::ipv4, "ipv4", "67-68"));
  registrar.register_rule(dhcp_rule(FirewallFamily::ipv6, "ipv6", "546-547"));
}

} // namespace keen_pbr3
