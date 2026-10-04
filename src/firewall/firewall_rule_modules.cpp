#include "firewall_rule_modules.hpp"

#include <iterator>

namespace keen_pbr3 {
namespace {

constexpr RouteRuleModuleRegistration kRouteRuleModules[] = {
    register_restore_conntrack_mark_rules,
    register_skip_local_replies_rules,
    register_skip_lan_output_rules,
    register_skip_established_or_dnat_rules,
    register_skip_marked_packets_rules,
    register_inbound_interface_filter_rules,
    register_route_mark_rules,
    register_route_drop_rules,
    register_route_pass_rules,
    register_route_balance_rules,
    register_dns_detour_rules,
    register_intercept_dns_hold_rules,
    register_intercept_l7_sniff_rules,
};

} // namespace

RouteRuleModuleManifest route_rule_module_manifest() {
  return {std::begin(kRouteRuleModules), std::end(kRouteRuleModules)};
}

} // namespace keen_pbr3
