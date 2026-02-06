#pragma once

#include <vector>

#include "netlink.hpp"

namespace keen_pbr3 {

// Manages installed ip policy rules, tracking them for duplicate avoidance and cleanup.
// Uses NetlinkManager for actual kernel operations.
class PolicyRuleManager {
public:
    explicit PolicyRuleManager(NetlinkManager& netlink);
    ~PolicyRuleManager();

    // Non-copyable
    PolicyRuleManager(const PolicyRuleManager&) = delete;
    PolicyRuleManager& operator=(const PolicyRuleManager&) = delete;

    // Add a policy rule. If an identical rule is already tracked, this is a no-op.
    void add(const RuleSpec& spec);

    // Remove a specific policy rule. If not tracked, this is a no-op.
    void remove(const RuleSpec& spec);

    // Remove all installed policy rules (shutdown cleanup).
    void clear();

    // Number of currently tracked rules.
    size_t size() const { return rules_.size(); }

private:
    NetlinkManager& netlink_;
    std::vector<RuleSpec> rules_;

    // Check if an identical rule is already tracked.
    bool is_tracked(const RuleSpec& spec) const;
};

} // namespace keen_pbr3
