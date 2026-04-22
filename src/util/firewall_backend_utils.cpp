#include "firewall_backend_utils.hpp"

#include "safe_exec.hpp"

namespace keen_pbr3 {

namespace {

const char* firewall_backend_command(FirewallBackend backend) {
    switch (backend) {
        case FirewallBackend::iptables:
            return "iptables";
        case FirewallBackend::nftables:
            return "nft";
    }

    throw FirewallError("Unexpected firewall backend value");
}

} // namespace

bool firewall_backend_command_exists(FirewallBackend backend) {
    return safe_exec({"which", firewall_backend_command(backend)}, /*suppress_output=*/true) == 0;
}

FirewallBackend detect_firewall_backend() {
    if (firewall_backend_command_exists(FirewallBackend::nftables)) {
        return FirewallBackend::nftables;
    }
    if (firewall_backend_command_exists(FirewallBackend::iptables)) {
        return FirewallBackend::iptables;
    }

    throw FirewallError("No supported firewall backend found (need nft or iptables)");
}

FirewallBackend resolve_firewall_backend(FirewallBackendPreference backend_pref) {
    switch (backend_pref) {
        case FirewallBackendPreference::auto_detect:
            return detect_firewall_backend();
        case FirewallBackendPreference::iptables:
            if (!firewall_backend_command_exists(FirewallBackend::iptables)) {
                throw FirewallError("iptables backend requested but iptables not found");
            }
            return FirewallBackend::iptables;
        case FirewallBackendPreference::nftables:
            if (!firewall_backend_command_exists(FirewallBackend::nftables)) {
                throw FirewallError("nftables backend requested but nft not found");
            }
            return FirewallBackend::nftables;
    }

    throw FirewallError("Unexpected firewall backend value");
}

} // namespace keen_pbr3
