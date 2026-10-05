#include "firewall.hpp"
#include "firewall_lowering.hpp"
#include "firewall_physical.hpp"
#include "../util/firewall_backend_utils.hpp"

#include <algorithm>
#include <limits>

namespace keen_pbr3 {

bool FirewallApplyResult::has_physical_set(const std::string &name) const {
  return std::binary_search(physical_set_names.begin(),
                            physical_set_names.end(), name);
}

std::optional<uint32_t> normalize_ipset_hashsize(uint32_t requested) {
  constexpr uint64_t kMinimumHashsize = 64;
  const uint64_t target = std::max<uint64_t>(requested, kMinimumHashsize);
  uint64_t normalized = 1;
  while (normalized < target) {
    normalized <<= 1;
  }
  if (normalized > std::numeric_limits<uint32_t>::max()) {
    return std::nullopt;
  }
  return static_cast<uint32_t>(normalized);
}

PhysicalRuleset Firewall::expected_hook_rules() const { return {}; }

PhysicalRuleset Firewall::expected_ruleset(const FirewallPlan &plan) const {
  FirewallLoweringContext context;
  context.backend = backend();
  context.raw_prerouting = raw_prerouting_mode();
  context.ipv6_enabled = ipv6_enabled();
  context.fwmark_mask = plan.fwmark_mask;
  context.physical_set_name = [this](const std::string &name) {
    return physical_set_name(name);
  };
  PhysicalRuleset result = lower_firewall_plan(plan, context);
  append_physical_ruleset(result, expected_hook_rules());
  return result;
}

const char *firewall_backend_name(FirewallBackend backend) {
  switch (backend) {
  case FirewallBackend::iptables:
    return "iptables";
  case FirewallBackend::nftables:
    return "nftables";
  }

  throw FirewallError("Unexpected firewall backend value");
}

// Forward declarations - implemented in iptables.cpp and nftables.cpp
std::unique_ptr<Firewall>
create_iptables_firewall(RawPreroutingMode raw_prerouting = {});
#ifndef KEEN_PBR_PLATFORM_KEENETIC
std::unique_ptr<Firewall> create_nftables_firewall();
#endif

std::unique_ptr<Firewall>
create_firewall(FirewallBackendPreference backend_pref,
                RawPreroutingMode raw_prerouting) {
  const FirewallBackend backend = resolve_firewall_backend(backend_pref);

  switch (backend) {
  case FirewallBackend::iptables:
    return create_iptables_firewall(raw_prerouting);
  case FirewallBackend::nftables:
#ifdef KEEN_PBR_PLATFORM_KEENETIC
    throw FirewallError("Keenetic supports only the iptables firewall backend");
#else
    if (raw_prerouting.ipv4 || raw_prerouting.ipv6) {
      throw FirewallError(
          "RAW PREROUTING is supported only with the iptables firewall "
          "backend");
    }
    return create_nftables_firewall();
#endif
  }

  // Unreachable, but silence compiler warnings
  throw FirewallError("Unexpected firewall backend value");
}

} // namespace keen_pbr3
