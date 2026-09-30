#include "firewall_rule_modules.hpp"

namespace keen_pbr3 {
namespace {

void register_mark_rules(const FirewallBuildContext& context,
                         FirewallRuleRegistrar& registrar) {
  RouteMarkRuleModule{}.register_rules(context, registrar);
}

void register_drop_rules(const FirewallBuildContext& context,
                         FirewallRuleRegistrar& registrar) {
  RouteDropRuleModule{}.register_rules(context, registrar);
}

void register_pass_rules(const FirewallBuildContext& context,
                         FirewallRuleRegistrar& registrar) {
  RoutePassRuleModule{}.register_rules(context, registrar);
}

void register_balance_rules(const FirewallBuildContext& context,
                            FirewallRuleRegistrar& registrar) {
  RouteBalanceRuleModule{}.register_rules(context, registrar);
}

} // namespace

namespace {

void register_dns_detour_rules(const FirewallBuildContext& context,
                               FirewallRuleRegistrar& registrar) {
  DnsDetourRuleModule{}.register_rules(context, registrar);
}

void register_restore_conntrack_mark_rules(const FirewallBuildContext& context,
                                           FirewallRuleRegistrar& registrar) {
  RestoreConntrackMarkRuleModule{}.register_rules(context, registrar);
}

void register_skip_established_or_dnat_rules(
    const FirewallBuildContext& context, FirewallRuleRegistrar& registrar) {
  SkipEstablishedOrDnatRuleModule{}.register_rules(context, registrar);
}

void register_skip_marked_packets_rules(const FirewallBuildContext& context,
                                        FirewallRuleRegistrar& registrar) {
  SkipMarkedPacketsRuleModule{}.register_rules(context, registrar);
}

void register_inbound_interface_filter_rules(
    const FirewallBuildContext& context, FirewallRuleRegistrar& registrar) {
  InboundInterfaceFilterRuleModule{}.register_rules(context, registrar);
}

} // namespace

std::array<RouteRuleModuleRegistration, 9> route_rule_module_manifest() {
  return {register_restore_conntrack_mark_rules,
          register_skip_established_or_dnat_rules,
          register_skip_marked_packets_rules,
          register_inbound_interface_filter_rules,
          register_mark_rules,
          register_drop_rules,
          register_pass_rules,
          register_balance_rules,
          register_dns_detour_rules};
}

} // namespace keen_pbr3
