#include "firewall.hpp"
#include "../util/firewall_backend_utils.hpp"

namespace keen_pbr3 {

const char* firewall_backend_name(FirewallBackend backend) {
    switch (backend) {
        case FirewallBackend::iptables:
            return "iptables";
        case FirewallBackend::nftables:
            return "nftables";
    }

    throw FirewallError("Unexpected firewall backend value");
}

// Forward declarations - implemented in iptables.cpp and nftables.cpp
std::unique_ptr<Firewall> create_iptables_firewall();
std::unique_ptr<Firewall> create_nftables_firewall();

std::unique_ptr<Firewall> create_firewall(FirewallBackendPreference backend_pref) {
    const FirewallBackend backend = resolve_firewall_backend(backend_pref);

    switch (backend) {
        case FirewallBackend::iptables:
            return create_iptables_firewall();
        case FirewallBackend::nftables:
            return create_nftables_firewall();
    }

    // Unreachable, but silence compiler warnings
    throw FirewallError("Unexpected firewall backend value");
}

} // namespace keen_pbr3
