#pragma once

#include "../firewall/firewall.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace keen_pbr3 {

// What a balance group's firewall classifier was last built from. Probe
// results arrive every cycle; when none of this changes, rebuilding the
// firewall would produce the same rules.
struct BalanceClassifierState {
    std::string selected_child;
    std::vector<FirewallBalanceCandidate> candidates;
    std::set<std::string> failed_children;

    bool operator==(const BalanceClassifierState& other) const {
        return selected_child == other.selected_child && candidates == other.candidates &&
               failed_children == other.failed_children;
    }
    bool operator!=(const BalanceClassifierState& other) const { return !(*this == other); }
};

// Children whose connections must be flushed: those that failed since the
// last applied state (all failed children when nothing was applied yet).
// A child that stays failed is flushed once, not on every probe cycle.
inline std::set<std::string> newly_failed_children(const BalanceClassifierState* applied,
                                                   const std::set<std::string>& failed_now) {
    if (applied == nullptr) return failed_now;
    std::set<std::string> result;
    for (const auto& child : failed_now) {
        if (applied->failed_children.count(child) == 0) result.insert(child);
    }
    return result;
}

// Whether a balance group other than `own_group` still balances new connections
// onto `child`. `usable_by_balance_group` maps each balance group's tag to its
// currently usable members (select_test_group_usable_outbounds). Health is kept
// per group, but a member's mark and the conntrack flush are per outbound, so a
// flush is only safe once no other balance group uses the member.
inline bool member_usable_in_other_balance_group(
    const std::string& child, const std::string& own_group,
    const std::map<std::string, std::vector<std::string>>& usable_by_balance_group) {
    for (const auto& [group, usable] : usable_by_balance_group) {
        if (group == own_group) continue;
        for (const auto& tag : usable) {
            if (tag == child) return true;
        }
    }
    return false;
}

} // namespace keen_pbr3
