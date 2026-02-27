#include "firewall_verifier.hpp"
#include "iptables_verifier.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

namespace keen_pbr3 {

std::string run_command_capture(const std::string& cmd) {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return {};
    }
    std::string result;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
        result += buf;
    }
    pclose(pipe);
    return result;
}

std::unique_ptr<FirewallVerifier> create_firewall_verifier(
    FirewallBackend backend,
    CommandRunner runner) {
    switch (backend) {
        case FirewallBackend::iptables:
            return create_iptables_verifier(std::move(runner));
        case FirewallBackend::nftables:
            // NftablesFirewallVerifier is implemented in US-063
            throw FirewallError("nftables verifier not yet implemented");
    }
    throw FirewallError("unknown firewall backend");
}

} // namespace keen_pbr3
