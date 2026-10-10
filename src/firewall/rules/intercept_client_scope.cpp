#include "../firewall_rule_modules.hpp"

#include <algorithm>

namespace keen_pbr3 {

namespace {
void sort_unique(std::vector<std::string>& values) {
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
}
} // namespace

InterceptClientScope intercept_client_scope(
    const FirewallBuildContext& context) {
  InterceptClientScope scope;
  if (context.config != nullptr && context.config->route.has_value()) {
    const auto& inbound = context.config->route->inbound_interfaces;
    if (inbound.has_value() && !inbound->empty()) {
      scope.include = *inbound;
      sort_unique(scope.include);
      return scope;
    }
  }
  // No allowlist: learn from everything but the way out.  The set follows the
  // main routing table, so it is part of the plan and a default-route move
  // re-applies the firewall (see handle_interface_event).
  for (const auto& outbound : context.outbounds) {
    if (outbound.type == OutboundType::INTERFACE &&
        outbound.interface.has_value() && !outbound.interface->empty()) {
      scope.exclude.push_back(*outbound.interface);
    }
  }
  for (const auto& route : context.main_routes) {
    if (route.destination == "default" && route.interface.has_value() &&
        !route.interface->empty() && route.unicast && !route.blackhole &&
        !route.unreachable) {
      scope.exclude.push_back(*route.interface);
    }
  }
  sort_unique(scope.exclude);
  return scope;
}

} // namespace keen_pbr3
