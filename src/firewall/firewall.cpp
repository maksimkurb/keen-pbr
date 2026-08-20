#include "firewall.hpp"
#include "../util/firewall_backend_utils.hpp"

#include <algorithm>
#include <limits>

namespace keen_pbr3 {

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
create_iptables_firewall(bool use_raw_prerouting = false);
std::unique_ptr<Firewall> create_nftables_firewall();

std::unique_ptr<Firewall>
create_firewall(FirewallBackendPreference backend_pref,
                bool use_raw_prerouting) {
  const FirewallBackend backend = resolve_firewall_backend(backend_pref);

  switch (backend) {
  case FirewallBackend::iptables:
    return create_iptables_firewall(use_raw_prerouting);
  case FirewallBackend::nftables:
    if (use_raw_prerouting) {
      throw FirewallError("--use-raw-prerouting is supported only with the "
                          "iptables firewall backend");
    }
    return create_nftables_firewall();
  }

  // Unreachable, but silence compiler warnings
  throw FirewallError("Unexpected firewall backend value");
}

} // namespace keen_pbr3
