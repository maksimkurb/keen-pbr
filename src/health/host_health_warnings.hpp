#pragma once

#include "../config/config.hpp"
#include "../firewall/firewall.hpp"
#include "../firewall/firewall_verifier.hpp"
#include "../routing/netlink.hpp"
#include "../routing/routing_reconciler.hpp"
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

// A foreign `ip rule` with a fwmark selector, as reported in the two ip rule
// warnings below.
struct ForeignPolicyRule {
    std::uint32_t priority{0};
    std::uint32_t fwmark{0};
    std::uint32_t fwmask{0};
    bool invert{false};  // "not fwmark ..."
    std::uint32_t table{0};
    RuleAction action{RuleAction::lookup};
    bool nop{false};
    std::optional<std::uint32_t> goto_target;  // jump target of a goto rule
    // fwmask & keen-pbr's mask: the mark bits of ours the rule looks at.
    std::uint32_t overlap{0};
    // Outbounds whose mark the rule matches (see matches below).
    std::vector<std::string> matched_outbounds;
    // Whether the rule matches every mark keen-pbr assigns.
    bool matches_all_outbounds{false};
};

// Pure. `outbound_marks` are the marks keen-pbr assigns (outbound tag -> mark,
// inside `fwmark_mask`).  A rule matches a packet marked K when
// ((K & fwmask) == (fwmark & fwmask)) XOR invert; bits of the packet outside
// `fwmark_mask` are taken as 0, as on a packet before keen-pbr marks it.
// Rules matching one of `own_rules` (priority, fwmark, mask, table) are
// keen-pbr's and skipped, as are rules inside `ns` (stale keen-pbr rules, see
// find_stale_policy_rules) and rules without a fwmark selector.

// Rules that disagree with keen-pbr about the mark bits themselves: they
// expect their own values inside our bits (fwmark & fwmask & mask != 0), or
// they treat some of our outbound marks differently from the others.  Moving
// fwmark.start/fwmark.mask helps here.
std::vector<ForeignPolicyRule> find_foreign_policy_rule_overlaps(
    const std::vector<DumpedRule>& dumped, const std::vector<RuleSpec>& own_rules,
    std::uint32_t fwmark_mask, const RuleNamespace& ns = {},
    const OutboundMarkMap& outbound_marks = {});

// Rules evaluated before keen-pbr's own rules (lower priority number than the
// lowest of `own_rules`) that send a packet keen-pbr marked somewhere else,
// whatever bits they look at: a "not fwmark X" rule catches every marked
// packet.  Moving bits does not help here; the rule priorities do.
// Per outbound mark the rule is harmless when keen-pbr has no rule for that
// mark, or when it does the same as keen-pbr's rule (same action and, for a
// lookup, same table); `matched_outbounds` lists only the others.  `nop` rules
// and `goto` rules that jump to the lowest own priority or before it are
// skipped; a goto past keen-pbr's rules diverts.  prohibit reads as unreachable.
// Without `own_rules` there is nothing to compare with and nothing is reported.
std::vector<ForeignPolicyRule> find_foreign_policy_rule_precedence(
    const std::vector<DumpedRule>& dumped, const std::vector<RuleSpec>& own_rules,
    std::uint32_t fwmark_mask, const RuleNamespace& ns = {},
    const OutboundMarkMap& outbound_marks = {});

// Pure: rules inside keen-pbr's namespace that match none of `own_rules`, one
// entry per (priority, fwmark, mask, table, action) across both families.
// The apply removes them, so any hit means the cleanup failed.
std::vector<DumpedRule> find_stale_policy_rules(const std::vector<DumpedRule>& dumped,
                                                const std::vector<RuleSpec>& own_rules,
                                                const RuleNamespace& ns);

// Builds fwmark_mask_conflict and foreign_rule_precedence warnings.
// `iptables_texts` pairs a label such as "mangle" with its `iptables -S` text;
// identical sources fold into one warning.  A foreign ip rule can raise both.
std::vector<HealthWarning> evaluate_fwmark_conflict_warnings(
    const std::vector<std::pair<std::string, std::string>>& iptables_texts,
    const std::vector<DumpedRule>& dumped_rules,
    const std::vector<RuleSpec>& own_rules, std::uint32_t fwmark_mask,
    const RuleNamespace& ns = {}, const OutboundMarkMap& outbound_marks = {});

// Builds stale_policy_rule warnings from find_stale_policy_rules.
std::vector<HealthWarning> evaluate_stale_policy_rule_warnings(
    const std::vector<DumpedRule>& dumped_rules,
    const std::vector<RuleSpec>& own_rules, const RuleNamespace& ns);

struct HostHealthInputs {
    std::vector<BalanceCandidate> candidates;
    std::vector<std::string> inbound_interfaces;
    // Configured fwmark.mask; 0 when it is invalid (overlap check skipped).
    std::uint32_t fwmark_mask{0};
    // keen-pbr's policy-rule namespace; empty when fwmark is invalid.
    RuleNamespace rule_namespace;
    // Marks keen-pbr assigns to outbounds; empty when fwmark is invalid.
    OutboundMarkMap outbound_marks;
};

// The fwmark overlap check is off on Keenetic (its firmware chains set marks
// with full masks, which would warn permanently) and when the mask is invalid.
// When false, no mangle/raw reads happen.
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
