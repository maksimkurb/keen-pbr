#pragma once

#include <vector>

#include "netlink.hpp"

namespace keen_pbr3 {

// Manages installed ip policy rules, tracking them for duplicate avoidance and cleanup.
// Uses NetlinkManager for actual kernel operations.
class PolicyRuleManager {
public:
    // If dry_run is true, add()/clear() only track specs and skip netlink ops.
    explicit PolicyRuleManager(RuleNetlinkOperations& netlink, bool dry_run = false);
    ~PolicyRuleManager();

    // Non-copyable
    PolicyRuleManager(const PolicyRuleManager&) = delete;
    PolicyRuleManager& operator=(const PolicyRuleManager&) = delete;

    // Add a policy rule. If an identical rule is already tracked, this is a no-op.
    void add(const RuleSpec& spec);

    // Remove a specific policy rule. If not tracked, this is a no-op.
    void remove(const RuleSpec& spec);

    // Install missing rules before deleting obsolete rules owned by this process.
    void reconcile(const std::vector<RuleSpec>& desired);

    void add_missing(const std::vector<RuleSpec>& desired);
    void remove_obsolete(const std::vector<RuleSpec>& desired);

    // Remove all installed policy rules (shutdown cleanup).
    void clear();

    // Number of currently tracked rules.
    size_t size() const { return rules_.size(); }

    // Read-only access to the tracked rules.
    const std::vector<RuleSpec>& get_rules() const { return rules_; }

private:
    RuleNetlinkOperations& netlink_;
    bool dry_run_{false};
    // Complete desired state, including identical rules that predated us.
    std::vector<RuleSpec> rules_;
    // Concrete-family rules created by this process and safe to delete.
    std::vector<RuleSpec> owned_rules_;

    // Check if an identical rule is already tracked.
    bool is_tracked(const RuleSpec& spec) const;
};

} // namespace keen_pbr3
