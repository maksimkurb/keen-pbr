#pragma once

#include <functional>
#include <string>
#include <vector>

namespace keen_pbr3 {

struct ParsedIptablesState;
struct ParsedIpset;
struct ParsedNftablesState;

struct FirewallSetSnapshot {
    std::string name;
    int family{0};
    unsigned timeout_seconds{0};
    bool dynamic{false};
};

struct FirewallActualState {
    bool available{false};
    std::vector<std::string> chains;
    std::vector<std::string> jumps;
    std::vector<std::string> ordered_rules;
    std::vector<FirewallSetSnapshot> sets;
};

using FirewallDesiredState = FirewallActualState;

struct FirewallOperation {
    std::string description;
    std::function<void()> apply;
};

struct FirewallStateDiff {
    std::vector<std::string> missing_chains;
    std::vector<std::string> extra_chains;
    std::vector<std::string> missing_jumps;
    std::vector<std::string> extra_jumps;
    std::vector<std::string> missing_sets;
    std::vector<std::string> extra_sets;
    std::vector<std::string> schema_mismatches;
    bool rules_reordered{false};

    bool empty() const;
    std::string summary() const;
};

// Compare logical ownership state. Rules are intentionally an ordered vector:
// reordering is observable drift, not a harmless set difference.
FirewallStateDiff diff_firewall_state(const FirewallDesiredState& desired,
                                      const FirewallActualState& actual);

// Only these names may be removed by keen-pbr cleanup/reconciliation.
bool is_keen_pbr_namespace_name(const std::string& name);

FirewallActualState inspect_iptables_state(const ParsedIptablesState& ipv4,
                                           const ParsedIptablesState& ipv6,
                                           const std::vector<ParsedIpset>& sets = {});
FirewallActualState inspect_nftables_state(const ParsedNftablesState& state);

class FirewallReconcilerBackend {
public:
    virtual ~FirewallReconcilerBackend() = default;
    virtual bool probe(std::string& error) = 0;
    virtual FirewallActualState inspect() = 0;
    virtual std::vector<FirewallOperation> plan(const FirewallDesiredState& desired,
                                                const FirewallActualState& actual) = 0;
    virtual bool verify(const FirewallDesiredState& desired,
                        const FirewallActualState& actual,
                        std::string& error) = 0;
    virtual void cleanup() = 0;
};

struct FirewallReconcileResult {
    bool committed{false};
    bool drift_detected{false};
    std::string error;
    size_t operation_count{0};
};

// Backend-neutral lifecycle. Plans are local to one call and are discarded on
// every success or failure, preventing a failed backend attempt from leaking
// operations into the next reconcile.
class FirewallReconciler {
public:
    explicit FirewallReconciler(FirewallReconcilerBackend& backend) : backend_(backend) {}

    FirewallReconcileResult reconcile(const FirewallDesiredState& desired);

private:
    FirewallReconcilerBackend& backend_;
};

} // namespace keen_pbr3
