#include "firewall_verifier.hpp"
#include "iptables_verifier.hpp"
#include "nftables_verifier.hpp"
#include "../util/safe_exec.hpp"

#include <atomic>

namespace keen_pbr3 {

namespace {
std::atomic_size_t g_firewall_verify_capture_max_bytes{DEFAULT_FIREWALL_VERIFY_CAPTURE_MAX_BYTES};
}


void set_firewall_verifier_capture_max_bytes(size_t max_bytes) {
    g_firewall_verify_capture_max_bytes.store(max_bytes, std::memory_order_relaxed);
}

size_t get_firewall_verifier_capture_max_bytes() {
    return g_firewall_verify_capture_max_bytes.load(std::memory_order_relaxed);
}

std::string run_command_capture(const std::vector<std::string>& args) {
    if (args.empty()) return {};
    return safe_exec_capture(args, /*suppress_stderr=*/true,
                             /*max_bytes=*/get_firewall_verifier_capture_max_bytes());
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
