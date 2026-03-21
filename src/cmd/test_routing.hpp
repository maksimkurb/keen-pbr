#pragma once

#include "../cache/cache_manager.hpp"
#include "../config/config.hpp"

#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

struct ListMatchInfo {
    std::string list_name;
    std::string via; // specific entry that triggered match: an IP, CIDR, or domain
};

struct TestRoutingEntry {
    std::string ip;
    std::optional<ListMatchInfo> list_match;
    std::string expected_outbound; // rule outbound tag, or "(default)"
    std::string actual_outbound;   // tag, "(default)", or "(unknown)" if kernel check unavailable
    bool ok;
};

struct RuleIpDiagnostic {
    std::string ip;
    // true/false when checked against live firewall set, null when unavailable.
    std::optional<bool> in_ipset;
};

struct RuleDiagnostic {
    int rule_index{0};
    std::string outbound;
    std::string interface_name; // "-" when unknown/not applicable
    bool target_in_lists{false};
    std::optional<ListMatchInfo> target_match;
    std::vector<RuleIpDiagnostic> ip_rows;
};

struct TestRoutingResult {
    std::string target;
    bool is_domain{false};
    std::vector<std::string> resolved_ips;
    std::vector<TestRoutingEntry> entries;
    std::vector<RuleDiagnostic> rule_diagnostics;
    bool no_matching_rule{false};
    std::optional<std::string> dns_error;
    std::vector<std::string> warnings;
};

// Compute expected (config+cache) and actual (kernel ipset/nftset) routing for target.
TestRoutingResult compute_test_routing(const Config& config,
                                        const CacheManager& cache,
                                        const std::string& target);

// Print table and return 0 if all entries match, 1 otherwise.
int run_test_routing_command(const Config& config,
                              const CacheManager& cache,
                              const std::string& target);

} // namespace keen_pbr3
