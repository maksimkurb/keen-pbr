#include "../firewall_rule_modules.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace keen_pbr3 {

namespace {
constexpr std::string_view kModuleId = "dns.intercept_hold";

void register_hold(FirewallRuleRegistrar& registrar, std::string_view instance,
                   L4Proto proto, uint16_t queue_num,
                   const InterceptClientScope& scope,
                   bool process_router_traffic) {
  FirewallRuleInstance rule;
  rule.key = FirewallRuleKey{std::string(kModuleId), std::string(instance)};
  rule.stage = FirewallRuleStage::interception;
  rule.hook = FirewallHook::postrouting;
  rule.family = FirewallFamily::any;
  rule.criteria.proto = proto;
  rule.criteria.src_port = PortSpec("53");
  rule.criteria.ct_established_reply = true;
  // The reply goes back to the client, so the client interface is the output
  // interface.  Replies to router-local processes are sent through loopback:
  // leave them alone unless the router's own traffic is processed.
  rule.criteria.include_oif = scope.include;
  rule.criteria.exclude_oif = scope.exclude;
  const auto is_lo = [](const std::string& name) { return name == "lo"; };
  if (!rule.criteria.include_oif.empty()) {
    auto& include = rule.criteria.include_oif;
    include.erase(std::remove_if(include.begin(), include.end(), is_lo),
                  include.end());
    if (process_router_traffic) include.push_back("lo");
    std::sort(include.begin(), include.end());
    // Only loopback was allowed: nothing is left to hold (an empty allowlist
    // would mean "any").
    if (include.empty()) return;
  } else if (!process_router_traffic) {
    auto& exclude = rule.criteria.exclude_oif;
    exclude.push_back("lo");
    std::sort(exclude.begin(), exclude.end());
    exclude.erase(std::unique(exclude.begin(), exclude.end()), exclude.end());
  }
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
  const auto scope = intercept_client_scope(context);
  register_hold(registrar, "udp", L4Proto::Udp, queue_num, scope,
                context.process_router_traffic);
  register_hold(registrar, "tcp", L4Proto::Tcp, queue_num, scope,
                context.process_router_traffic);
}

} // namespace keen_pbr3
