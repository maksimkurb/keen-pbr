#include "firewall_verifier.hpp"
#include "iptables_verifier.hpp"
#include "nftables_verifier.hpp"
#include "../util/safe_exec.hpp"


namespace keen_pbr3 {

std::string run_command_capture(const std::vector<std::string>& args) {
    if (args.empty()) return {};
    return safe_exec_capture(args, /*suppress_stderr=*/true, /*max_bytes=*/262144);
}

std::unique_ptr<FirewallVerifier> create_firewall_verifier(
    FirewallBackend backend,
    CommandRunner runner) {
    switch (backend) {
        case FirewallBackend::iptables:
            return create_iptables_verifier(std::move(runner));
        case FirewallBackend::nftables:
            return create_nftables_verifier(std::move(runner));
    }
    throw FirewallError("unknown firewall backend");
}

} // namespace keen_pbr3
