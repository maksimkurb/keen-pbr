#pragma once

#include "../config/config.hpp"
#include "../firewall/firewall.hpp"
#include "../firewall/firewall_verifier.hpp"
#include "routing_health.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

// Non-blocking host configuration warnings for load-balancing WANs. These
// never influence overall_ok and never change sysctls or NAT.

enum class NatCoverage { covered, partial, missing };

struct NatCoverageResult {
    NatCoverage coverage{NatCoverage::missing};
    // For partial coverage: the first source/destination restriction seen
    // (for example "-s 172.17.0.0/16").
    std::string restriction;
};

// Pure parser over `iptables-save -t nat` / `iptables -t nat -S` text.
NatCoverageResult nat_coverage_for_interface(const std::string& nat_text,
                                             const std::string& interface);

struct BalanceCandidate {
    std::string interface;
    std::string outbound_tag;
};

// Member outbounds of every balance outbound that resolve to a concrete
// interface (type interface). Deduplicated by interface name.
std::vector<BalanceCandidate> balance_candidate_interfaces(const Config& config);

// Reads a file; nullopt when it cannot be read.
using ProcFileReader =
    std::function<std::optional<std::string>(const std::string& path)>;
std::optional<std::string> read_proc_file(const std::string& path);

std::vector<HealthWarning> evaluate_rp_filter_warnings(
    const std::vector<BalanceCandidate>& candidates, const ProcFileReader& reader);

std::vector<HealthWarning> evaluate_nat_warnings(
    const std::vector<BalanceCandidate>& candidates, const std::string& nat_text);

// Collects all host warnings for the config. NAT is only checked on the
// iptables backend and silently skipped when the nat table cannot be read.
std::vector<HealthWarning> collect_host_health_warnings(
    const Config& config, FirewallBackend backend, const CommandRunner& runner,
    const ProcFileReader& reader);

// Stable one-line-per-warning key list, used to log only on changes.
std::vector<std::string> health_warning_keys(const std::vector<HealthWarning>& w);

} // namespace keen_pbr3
