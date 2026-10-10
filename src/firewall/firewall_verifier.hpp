#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace keen_pbr3 {

struct CommandResult {
    std::string stdout_output;
    int exit_code{-1};
    bool truncated{false};
};

// Type alias for a function that runs a command and returns its captured output metadata.
// Default implementation uses fork()+execvp(). Can be injected for inspectors/tests.
using CommandRunner = std::function<CommandResult(const std::vector<std::string>& args)>;

// Run a command and capture its stdout output.
// Returns the captured output, or empty string on error.
constexpr size_t DEFAULT_FIREWALL_VERIFY_CAPTURE_MAX_BYTES = 262144;

void set_firewall_verifier_capture_max_bytes(std::size_t max_bytes);
std::size_t get_firewall_verifier_capture_max_bytes();

CommandResult run_command_capture(const std::vector<std::string>& args);

} // namespace keen_pbr3
