#include "firewall.hpp"
#include "../util/firewall_backend_utils.hpp"

namespace keen_pbr3 {

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
