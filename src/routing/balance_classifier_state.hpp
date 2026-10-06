#pragma once

#include "../firewall/firewall.hpp"

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

} // namespace keen_pbr3
