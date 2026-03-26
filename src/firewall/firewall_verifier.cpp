#include "firewall_verifier.hpp"
#include "iptables_verifier.hpp"
#include "nftables_verifier.hpp"
#include "../util/safe_exec.hpp"

#include <sstream>
#include <string>

namespace keen_pbr3 {

// Split a shell command string into args, stripping shell redirects.
static std::vector<std::string> split_shell_command(const std::string& cmd) {
    std::vector<std::string> args;
    std::istringstream iss(cmd);
    std::string token;
    while (iss >> token) {
        if (token == "2>/dev/null" || token == ">/dev/null" ||
            token == "2>&1" || token[0] == '>') {
            continue;
        }
        args.push_back(token);
    }
    return args;
}

std::string run_command_capture(const std::string& cmd) {
    auto args = split_shell_command(cmd);
    if (args.empty()) return {};
    return safe_exec_capture(args, /*suppress_stderr=*/true);
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
