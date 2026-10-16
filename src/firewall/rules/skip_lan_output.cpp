#include "../firewall_rule_modules.hpp"

#include <string>
#include <utility>
#include <vector>

namespace keen_pbr3 {

namespace {
constexpr std::string_view kModuleId = "prefilter.skip_lan_output";

// Missing or empty route.inbound_interfaces leaves the oif skip disabled.
const std::vector<std::string>* configured_inbound_interfaces(
    const FirewallBuildContext& context) {
  if (context.config == nullptr || !context.config->route.has_value()) {
    return nullptr;
  }
  const auto& interfaces = context.config->route->inbound_interfaces;
  return interfaces.has_value() && !interfaces->empty() ? &*interfaces
                                                        : nullptr;
}

void register_skip(FirewallRuleRegistrar& registrar, std::string_view instance,
                   SkipLanOutputAction action) {
  FirewallRuleInstance rule;
  rule.key = FirewallRuleKey::compact(kModuleId, instance);
  rule.stage = FirewallRuleStage::global_bypass;
  // Same priority as skip_local_replies; registered right after it, so the
  // order is restore_conntrack_mark, skip_local_replies, skip_lan_output.
  rule.priority = -1;
  rule.hook = FirewallHook::output;
  rule.family = FirewallFamily::any;
  rule.action = std::move(action);
  registrar.register_rule(std::move(rule));
}
}

void register_skip_lan_output_rules(const FirewallBuildContext& context,
                                    FirewallRuleRegistrar& registrar) {
  // Router-originated traffic is policy-routed in mangle OUTPUT, but some of
  // it is not a conntrack reply and already leaves via the LAN (a DHCP reply
  // to a client that has no address yet, RA/NDP, mDNS, SSDP, unicast to a LAN
  // host).  The output device is chosen before mangle OUTPUT, so skipping
  // packets whose oif is a LAN interface keeps them on the main table.
  // Broadcast and multicast are link-local and never policy-routed, with or
  // without route.inbound_interfaces.  OUTPUT only (never PREROUTING).
  if (const auto* interfaces = configured_inbound_interfaces(context)) {
    SkipLanOutputAction action;
    action.kind = SkipLanOutputAction::Kind::lan_oif;
    action.interfaces = *interfaces;
    register_skip(registrar, "lan_oif", std::move(action));
  }
  register_skip(registrar, "bcast",
                SkipLanOutputAction{SkipLanOutputAction::Kind::broadcast, {}});
  register_skip(registrar, "mcast",
                SkipLanOutputAction{SkipLanOutputAction::Kind::multicast, {}});
}

} // namespace keen_pbr3
