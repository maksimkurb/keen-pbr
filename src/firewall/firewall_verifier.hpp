#pragma once

#include "../health/routing_health.hpp"
#include "../routing/firewall_state.hpp"
#include "firewall.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace keen_pbr3 {

// Type alias for a function that runs a command and returns its stdout output.
// Default implementation uses fork()+execvp(). Can be injected for testing.
using CommandRunner = std::function<std::string(const std::vector<std::string>& args)>;

// Run a command and capture its stdout output.
// Returns the captured output, or empty string on error.
constexpr size_t DEFAULT_FIREWALL_VERIFY_CAPTURE_MAX_BYTES = 262144;

void set_firewall_verifier_capture_max_bytes(size_t max_bytes);
size_t get_firewall_verifier_capture_max_bytes();

std::string run_command_capture(const std::vector<std::string>& args);

// Abstract interface for verifying live firewall state against expected configuration.
class FirewallVerifier {
public:
    virtual ~FirewallVerifier() = default;

    // Verify that the KeenPbrTable chain exists and is hooked into PREROUTING.
    virtual FirewallChainCheck verify_chain() = 0;

    // Verify that firewall rules match the expected RuleState list.
    // Returns one FirewallRuleCheck per (RuleState, set_name) pair where
    // action_type != Skip.
    virtual std::vector<FirewallRuleCheck> verify_rules(
        const std::vector<RuleState>& expected) = 0;

    // Non-copyable
    FirewallVerifier(const FirewallVerifier&) = delete;
    FirewallVerifier& operator=(const FirewallVerifier&) = delete;

protected:
    FirewallVerifier() = default;
};

// Factory: create a verifier for the given backend.
// runner defaults to run_command_capture; override for testing.
std::unique_ptr<FirewallVerifier> create_firewall_verifier(
    FirewallBackend backend,
    CommandRunner runner = run_command_capture);

} // namespace keen_pbr3
