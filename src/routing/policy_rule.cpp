#include "policy_rule.hpp"

#include <algorithm>

namespace keen_pbr3 {

namespace {

bool rules_equal(const RuleSpec& a, const RuleSpec& b) {
    return a.fwmark == b.fwmark &&
           a.fwmask == b.fwmask &&
           a.table == b.table &&
           a.priority == b.priority &&
           a.family == b.family;
}

} // anonymous namespace

PolicyRuleManager::PolicyRuleManager(NetlinkManager& netlink)
    : netlink_(netlink) {}

PolicyRuleManager::~PolicyRuleManager() {
    // Best-effort cleanup on destruction
    try {
        clear();
    } catch (...) {
        // Suppress exceptions in destructor
    }
}

bool PolicyRuleManager::is_tracked(const RuleSpec& spec) const {
    return std::any_of(rules_.begin(), rules_.end(),
                       [&](const RuleSpec& r) { return rules_equal(r, spec); });
}

void PolicyRuleManager::add(const RuleSpec& spec) {
    if (is_tracked(spec)) {
        return;
    }
    netlink_.add_rule(spec);
    rules_.push_back(spec);
}

void PolicyRuleManager::remove(const RuleSpec& spec) {
    auto it = std::find_if(rules_.begin(), rules_.end(),
                           [&](const RuleSpec& r) { return rules_equal(r, spec); });
    if (it == rules_.end()) {
        return;
    }
    netlink_.delete_rule(spec);
    rules_.erase(it);
}

void PolicyRuleManager::clear() {
    // Remove in reverse order (last added first)
    for (auto it = rules_.rbegin(); it != rules_.rend(); ++it) {
        try {
            netlink_.delete_rule(*it);
        } catch (...) {
            // Best effort: continue removing remaining rules
        }
    }
    rules_.clear();
}

} // namespace keen_pbr3
