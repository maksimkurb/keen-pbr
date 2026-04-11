#include "firewall.hpp"
#include "../util/safe_exec.hpp"

#include <string>

namespace keen_pbr3 {

namespace {

// Check if a command exists and is executable.
bool command_exists(const char* cmd) {
    return safe_exec({"which", cmd}, /*suppress_output=*/true) == 0;
}

} // anonymous namespace

FirewallBackend detect_firewall_backend() {
    if (command_exists("nft")) {
        return FirewallBackend::nftables;
    }
    if (command_exists("iptables")) {
        return FirewallBackend::iptables;
    }
    throw FirewallError("No supported firewall backend found (need nft or iptables)");
}

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
    FirewallBackend backend;

    switch (backend_pref) {
        case FirewallBackendPreference::auto_detect:
            backend = detect_firewall_backend();
            break;
        case FirewallBackendPreference::iptables:
            if (!command_exists("iptables")) {
                throw FirewallError("iptables backend requested but iptables not found");
            }
            backend = FirewallBackend::iptables;
            break;
        case FirewallBackendPreference::nftables:
            if (!command_exists("nft")) {
                throw FirewallError("nftables backend requested but nft not found");
            }
            backend = FirewallBackend::nftables;
            break;
    }

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
