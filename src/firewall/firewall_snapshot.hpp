#pragma once

#include "firewall_plan.hpp"
#include "firewall_reconciler.hpp"
#include "firewall_verifier.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

// One physical rule observed in an owned firewall chain.  A missing key is
// either an old rule without comments (legacy=true) or an explicitly foreign
// or unknown comment (legacy=false); the distinction keeps fallback bounded.
struct ObservedFirewallRule {
    std::optional<FirewallRuleKey> key;
    std::optional<std::string> comment;
    FirewallHook hook{FirewallHook::prerouting};
    FirewallFamily family{FirewallFamily::ipv4};
    FirewallRuleCriteria criteria;
    FirewallRuleAction action{MarkAction{}};
    std::string raw;
    std::string chain;
    bool legacy{false};
    bool restore_conntrack_companion{false};
    std::size_t order{0};
    struct BalanceDetails {
        std::string selector_mode;
        uint32_t selector_modulus{0};
        bool mark_guard_present{false};
        std::string mark_guard_op;
        uint32_t mark_guard_mask{0};
        uint32_t mark_guard_value{0};
        std::vector<uint32_t> target_indices;
        std::vector<uint32_t> target_marks;
        std::vector<std::optional<MarkAction>> setter_actions;
        std::vector<std::optional<MarkAction>> setter_ct_actions;
    };
    std::optional<BalanceDetails> balance;
};

struct ObservedFirewallSet {
    std::string name;
    FirewallFamily family{FirewallFamily::ipv4};
    uint32_t timeout_seconds{0};
    bool dynamic{false};
};

struct ObservedFirewallChain {
    std::string name;
    FirewallHook hook{FirewallHook::prerouting};
    FirewallFamily family{FirewallFamily::ipv4};
    bool hook_present{false};
};

// FirewallActualState remains the ordered lifecycle/reconciliation view.  This
// snapshot intentionally owns only one read's semantic rule observations, so
// rule verification does not couple to chain/set transaction state.
struct FirewallSnapshot {
    FirewallBackend backend{FirewallBackend::iptables};
    RawPreroutingMode raw_prerouting{};
    bool available{false};
    std::string error;
    std::vector<ObservedFirewallRule> rules;
    std::vector<ObservedFirewallSet> sets;
    std::vector<ObservedFirewallChain> chains;
};

class FirewallSnapshotInspector {
public:
    virtual ~FirewallSnapshotInspector() = default;
    virtual FirewallSnapshot inspect() const = 0;
};

FirewallSnapshot inspect_iptables_snapshot(
    const CommandRunner& runner, RawPreroutingMode raw_prerouting = {});
FirewallSnapshot inspect_nftables_snapshot(const CommandRunner& runner);

// Convenience overloads for the string-only runner used by reconciler tests
// and existing inspection seams.
FirewallSnapshot inspect_iptables_snapshot(
    const FirewallCommandRunner& runner, RawPreroutingMode raw_prerouting = {});
FirewallSnapshot inspect_nftables_snapshot(const FirewallCommandRunner& runner);

std::unique_ptr<FirewallSnapshotInspector> create_firewall_snapshot_inspector(
    FirewallBackend backend, RawPreroutingMode raw_prerouting = {},
    CommandRunner runner = run_command_capture);

// Compare canonical desired rules with one backend-neutral observation.  The
// plan is expanded only to the physical forms the selected backend emits;
// repeated comments from MARK/CONNMARK/RETURN or family/protocol expansion are
// treated as one logical bundle.
std::vector<FirewallRuleCheck> verify_firewall_plan(
    const FirewallPlan& plan, const FirewallSnapshot& snapshot,
    uint32_t fwmark_mask = 0xFFFFFFFFu);

} // namespace keen_pbr3
