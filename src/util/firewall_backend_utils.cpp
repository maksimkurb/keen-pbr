#include "firewall_backend_utils.hpp"

#include "kernel_capabilities.hpp"

namespace keen_pbr3 {

namespace {

#ifdef KEEN_PBR3_TESTING
std::optional<FirewallBackend>& detected_firewall_backend_override_for_tests() {
    static std::optional<FirewallBackend> override;
    return override;
}
#endif

} // namespace

bool firewall_backend_command_exists(FirewallBackend backend) {
    // Cached PATH lookup from service start; no process is started.
    const HostTools& tools = host_tools();
    return backend == FirewallBackend::nftables ? tools.nft : tools.iptables;
}

FirewallBackend detect_firewall_backend() {
#ifdef KEEN_PBR3_TESTING
    if (detected_firewall_backend_override_for_tests().has_value()) {
        return *detected_firewall_backend_override_for_tests();
    }
#endif

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

#ifdef KEEN_PBR3_TESTING
void set_detected_firewall_backend_for_tests(std::optional<FirewallBackend> backend) {
    detected_firewall_backend_override_for_tests() = backend;
}

void reset_detected_firewall_backend_for_tests() {
    detected_firewall_backend_override_for_tests().reset();
}
#endif

} // namespace keen_pbr3
