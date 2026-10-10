#pragma once

#include "../cache/cache_manager.hpp"
#include "../config/config.hpp"
#include "../routing/firewall_state.hpp"

#include <functional>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

struct ListMatchInfo {
    std::string list_name;
    std::string via; // specific entry that triggered match: an IP, CIDR, or domain
};

// Packet fields supplied by the routing-test API. An empty object preserves
// the legacy target-only/list-only diagnostic behavior.
struct TestRoutingCriteria {
    std::optional<std::string> proto; // tcp, udp, or other
    std::optional<uint16_t> dest_port;
    std::optional<std::string> src_addr;
    std::optional<uint16_t> src_port;
    std::optional<uint8_t> dscp;

    bool has_any() const {
        return proto.has_value() || dest_port.has_value() || src_addr.has_value() ||
               src_port.has_value() || dscp.has_value();
    }
};

enum class SetWriteEvidenceStatus : uint8_t {
    Recorded,
    NoRecord,
    NotTracked,
};

struct SetWriteEvidence {
    SetWriteEvidenceStatus status{SetWriteEvidenceStatus::NotTracked};
    std::optional<uint64_t> age_seconds;
};

// Returns evidence for one realized dynamic set and address. The callback is
// optional so the standalone CLI keeps its target-only behavior.
using SetWriteEvidenceLookup =
    std::function<SetWriteEvidence(const std::string& set_name, const std::string& ip)>;

struct TestRoutingEntry {
    std::string ip;
    std::optional<ListMatchInfo> list_match;
    std::string expected_outbound; // rule outbound tag, or "(default)"
    std::string actual_outbound;   // tag, "(default)", or "(unknown)" if live state/criteria unavailable
    bool ok;
    std::optional<int> matched_rule_index;
    // Whether the selected rule's packet criteria matched. null when packet
    // criteria were omitted or the result cannot be determined.
    std::optional<bool> criteria_match;
};

struct RuleIpDiagnostic {
    std::string ip;
    // Whether this resolved IP (or its source domain) matches one of the rule's lists.
    bool in_lists{false};
    std::optional<ListMatchInfo> list_match;
    // true/false when checked against live firewall set, null when unavailable.
    std::optional<bool> in_ipset;
    // Whether this resolved IP matches the rule's packet criteria. null means
    // that a required packet field was not supplied or directness is unknown.
    std::optional<bool> criteria_match;
    // Evidence that keen-pbr successfully operated on this exact dynamic set
    // and address. This says nothing about DNS provenance or current kernel
    // membership, which is reported separately by in_ipset.
    std::optional<SetWriteEvidence> set_write_evidence;
};

struct RuleDiagnostic {
    int rule_index{0};
    RouteRule rule;
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

// Test hook: replaces the system resolver used to resolve a domain target.
// Pass an empty function to restore the default behavior.
void set_domain_resolver_for_tests(
    std::function<std::vector<std::string>(const std::string& domain)> resolver);

// Compute expected (config+cache) and actual (kernel ipset/nftset) routing for target.
// When available, realized_rule_states must be the states returned by the live
// firewall apply. They contain backend-specific physical set names (notably the
// iptables A/B generation names) that cannot be reconstructed from config alone.
TestRoutingResult compute_test_routing(const Config& config,
                                        const CacheManager& cache,
                                        const std::string& target,
                                        const std::vector<RuleState>* realized_rule_states = nullptr);

TestRoutingResult compute_test_routing(const Config& config,
                                        const CacheManager& cache,
                                        const std::string& target,
                                        const TestRoutingCriteria& criteria,
                                        const std::vector<RuleState>* realized_rule_states = nullptr,
                                        SetWriteEvidenceLookup set_write_evidence_lookup = {});

// Print table and return 0 if all entries match, 1 otherwise.
int run_test_routing_command(const Config& config,
                              const CacheManager& cache,
                              const std::string& target);
int run_test_routing_command(const Config& config,
                             const CacheManager& cache,
                             const std::string& target,
                             const std::vector<RuleState>& realized_rule_states);

// Render a test-routing response obtained from the daemon control socket.
int run_test_routing_command(const nlohmann::json& response);

} // namespace keen_pbr3
