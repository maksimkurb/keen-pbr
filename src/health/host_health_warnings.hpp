#pragma once

#include "../config/config.hpp"
#include "../firewall/firewall.hpp"
#include "../firewall/firewall_verifier.hpp"
#include "../routing/netlink.hpp"
#include "routing_health.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

// Non-blocking host configuration warnings for load-balancing WANs. These
// never influence overall_ok and never change sysctls or NAT.

enum class NatCoverage { covered, partial, missing };

struct NatCoverageResult {
    NatCoverage coverage{NatCoverage::missing};
    // For partial coverage: the first restriction seen
    // (for example "-m mark --mark 0x40000/0xff0000" or "-s 172.17.0.0/16").
    std::string restriction;
};

// An IPv4 network (host byte order).
struct Ipv4Net {
    uint32_t addr{0};
    uint32_t mask{0};
};

std::optional<Ipv4Net> parse_ipv4_net(const std::string& text);

// IPv4 networks of the named interfaces (from netlink address dumps).
std::vector<Ipv4Net> interface_subnets(
    const std::vector<DumpedInterface>& interfaces,
    const std::vector<std::string>& names);

// Pure parser over `iptables-save -t nat` / `iptables -t nat -S` text.
// Only rules in chains reachable from POSTROUTING count. Rules with any match
// other than -o/-s/-d (or with a positive -d) are restrictions and yield at
// best a partial result. `-s` rules are judged against `lan_subnets`: empty
// means unknown, in which case -s rules count as covering.
NatCoverageResult nat_coverage_for_interface(
    const std::string& nat_text, const std::string& interface,
    const std::vector<Ipv4Net>& lan_subnets = {});

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
    const std::vector<BalanceCandidate>& candidates, const std::string& nat_text,
    const std::vector<Ipv4Net>& lan_subnets = {});

// Small inputs copied out of the config so the health worker does not need a
// full Config copy.
// A foreign iptables rule that writes or matches mark bits overlapping the
// keen-pbr fwmark mask.
struct ForeignMarkRule {
    std::string rule;           // the `-A ...` line as printed by iptables -S
    std::uint32_t overlap{0};   // overlapping bits (affected bits & mask)
};

// Pure parser over `iptables -t <table> -S` text. Examines MARK and CONNMARK
// targets (--set-xmark, --set-mark, --and-mark, --or-mark, --xor-mark,
// --save-mark, --restore-mark with --nfmask/--ctmask/--mask) and mark /
// connmark matches (--mark V[/M]; a match without a mask contributes only the
// bits set in V). keen-pbr's own rules (chains KeenPbr*, comments kpbr:v1:)
// are skipped.
std::vector<ForeignMarkRule> find_foreign_mark_overlaps(
    const std::string& iptables_text, std::uint32_t fwmark_mask);

// A foreign `ip rule` whose fwmark mask overlaps the keen-pbr fwmark mask.
struct ForeignPolicyRule {
    std::uint32_t priority{0};
    std::uint32_t fwmark{0};
    std::uint32_t fwmask{0};
    std::uint32_t overlap{0};
};

// Pure: rules matching one of `own_rules` (priority, fwmark, mask, table) are
// keen-pbr's and skipped; rules without a fwmark match are ignored.
std::vector<ForeignPolicyRule> find_foreign_policy_rule_overlaps(
    const std::vector<DumpedRule>& dumped, const std::vector<RuleSpec>& own_rules,
    std::uint32_t fwmark_mask);

// Builds fwmark_mask_conflict warnings. `iptables_texts` pairs a label such as
// "mangle" with its `iptables -S` text; identical sources fold into one warning.
std::vector<HealthWarning> evaluate_fwmark_conflict_warnings(
    const std::vector<std::pair<std::string, std::string>>& iptables_texts,
    const std::vector<DumpedRule>& dumped_rules,
    const std::vector<RuleSpec>& own_rules, std::uint32_t fwmark_mask);

struct HostHealthInputs {
    std::vector<BalanceCandidate> candidates;
    std::vector<std::string> inbound_interfaces;
    // Configured fwmark.mask; 0 when it is invalid (overlap check skipped).
    std::uint32_t fwmark_mask{0};
};

// The fwmark overlap check is off on Keenetic (its firmware chains set marks
// with full masks, which would warn permanently) and when the mask is invalid.
// When false, callers skip the policy-rule dump and no mangle/raw reads happen.
inline bool fwmark_conflict_check_enabled(const HostHealthInputs& inputs) {
#ifdef KEEN_PBR_PLATFORM_KEENETIC
    (void)inputs;
    return false;
#else
    return inputs.fwmark_mask != 0;
#endif
}

// Live policy rules and keen-pbr's tracked ones, gathered by the caller the
// same way as the interface dump.
struct PolicyRuleInputs {
    std::vector<DumpedRule> dumped;
    std::vector<RuleSpec> own;
};
HostHealthInputs host_health_inputs(const Config& config);

// Time-limited, thread-safe cache of `<iptables|ip6tables> -t <table> -S`
// output (nat, mangle, raw), keyed by command and table.
class IptablesTableCache {
public:
    using Clock = std::chrono::steady_clock;
    explicit IptablesTableCache(std::chrono::seconds lifetime = std::chrono::seconds{30})
        : lifetime_(lifetime) {}
    // Returns cached text or reads it through `runner`; nullopt when unreadable
    // (failures are not cached).
    std::optional<std::string> get(const CommandRunner& runner,
                                   const std::string& command,
                                   const std::string& table);
    void invalidate();

private:
    struct Entry {
        std::string text;
        Clock::time_point read_at{};
    };
    std::chrono::seconds lifetime_;
    std::mutex mutex_;
    std::map<std::string, Entry> entries_;
};

// Collects all host warnings. NAT is only checked on the iptables backend and
// silently skipped when the nat table cannot be read. The fwmark overlap check
// runs regardless of balancing (its iptables part on the iptables backend only).
std::vector<HealthWarning> collect_host_health_warnings(
    const HostHealthInputs& inputs,
    const std::vector<DumpedInterface>& interfaces, FirewallBackend backend,
    const PolicyRuleInputs& policy_rules, IptablesTableCache& table_cache,
    const CommandRunner& runner, const ProcFileReader& reader);

// Stable one-line-per-warning key list, used to log only on changes.
std::vector<std::string> health_warning_keys(const std::vector<HealthWarning>& w);

} // namespace keen_pbr3
