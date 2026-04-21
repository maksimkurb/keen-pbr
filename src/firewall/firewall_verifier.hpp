#pragma once

#include "../health/routing_health.hpp"
#include "../routing/firewall_state.hpp"
#include "firewall.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace keen_pbr3 {

struct CommandResult {
    std::string stdout_output;
    int exit_code{-1};
    bool truncated{false};
};

// Type alias for a function that runs a command and returns its captured output metadata.
// Default implementation uses fork()+execvp(). Can be injected for testing.
using CommandRunner = std::function<CommandResult(const std::vector<std::string>& args)>;

// Run a command and capture its stdout output.
// Returns the captured output, or empty string on error.
constexpr size_t DEFAULT_FIREWALL_VERIFY_CAPTURE_MAX_BYTES = 262144;

void set_firewall_verifier_capture_max_bytes(size_t max_bytes);
size_t get_firewall_verifier_capture_max_bytes();

CommandResult run_command_capture(const std::vector<std::string>& args);

// Abstract interface for verifying live firewall state against expected configuration.
class FirewallVerifier {
public:
    virtual ~FirewallVerifier() = default;

    // Verify that the KeenPbrTable chain exists and is hooked into PREROUTING.
    virtual FirewallChainCheck verify_chain() = 0;

    // Verify live firewall rules against realized rule state, including direct
    // selectors and per-family/per-proto expansions.
    virtual std::vector<FirewallRuleCheck> verify_rules(
        const std::vector<RuleState>& expected) = 0;

    void set_expected_fwmark_mask(uint32_t fwmark_mask) {
        expected_fwmark_mask_ = fwmark_mask;
    }

    // Non-copyable
    FirewallVerifier(const FirewallVerifier&) = delete;
    FirewallVerifier& operator=(const FirewallVerifier&) = delete;

protected:
    FirewallVerifier() = default;

    uint32_t expected_fwmark_mask_{0xFFFFFFFFu};
};

// Factory: create a verifier for the given backend.
// runner defaults to run_command_capture; override for testing.
std::unique_ptr<FirewallVerifier> create_firewall_verifier(
    FirewallBackend backend,
    CommandRunner runner = run_command_capture);

} // namespace keen_pbr3
