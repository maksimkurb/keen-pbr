#include "../firewall_rule_modules.hpp"

#include <utility>

namespace keen_pbr3 {

namespace {
constexpr std::string_view kModuleId = "dns.intercept_hold";

void register_hold(FirewallRuleRegistrar& registrar, std::string_view instance,
                   L4Proto proto, uint16_t queue_num) {
  FirewallRuleInstance rule;
  rule.key = FirewallRuleKey{std::string(kModuleId), std::string(instance)};
  rule.stage = FirewallRuleStage::interception;
  rule.hook = FirewallHook::postrouting;
  rule.family = FirewallFamily::any;
  rule.criteria.proto = proto;
  rule.criteria.src_port = PortSpec("53");
  rule.criteria.ct_established_reply = true;
  rule.action = QueueAction{queue_num, /*bypass=*/true};
  registrar.register_rule(std::move(rule));
}
} // namespace

// Hold DNS responses (UDP and TCP, source port 53) in an NFQUEUE so the
// daemon can learn the answer before the client sees it.  Disabled unless
// interception settings enable it explicitly.
void register_intercept_dns_hold_rules(const FirewallBuildContext& context,
                                       FirewallRuleRegistrar& registrar) {
  if (!context.intercept.has_value() || !context.intercept->dns_hold) {
    return;
  }
  const uint16_t queue_num = context.intercept->queue_num;
  register_hold(registrar, "udp", L4Proto::Udp, queue_num);
  register_hold(registrar, "tcp", L4Proto::Tcp, queue_num);
}

} // namespace keen_pbr3
