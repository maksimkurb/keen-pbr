#include "firewall.hpp"

#include <cstdlib>
#include <array>
#include <cstdio>
#include <string>

namespace keen_pbr3 {

namespace {

// Check if a command exists and is executable.
bool command_exists(const char* cmd) {
    std::string check = "command -v ";
    check += cmd;
    check += " >/dev/null 2>&1";
    return std::system(check.c_str()) == 0;
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

// Forward declarations - implemented in iptables.cpp and nftables.cpp
std::unique_ptr<Firewall> create_iptables_firewall();
std::unique_ptr<Firewall> create_nftables_firewall();

std::unique_ptr<Firewall> create_firewall(const std::string& backend_pref) {
    FirewallBackend backend;

    if (backend_pref == "auto") {
        backend = detect_firewall_backend();
    } else if (backend_pref == "iptables") {
        if (!command_exists("iptables")) {
            throw FirewallError("iptables backend requested but iptables not found");
        }
        backend = FirewallBackend::iptables;
    } else if (backend_pref == "nftables") {
        if (!command_exists("nft")) {
            throw FirewallError("nftables backend requested but nft not found");
        }
        backend = FirewallBackend::nftables;
    } else {
        throw FirewallError("Unknown firewall backend: " + backend_pref);
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
