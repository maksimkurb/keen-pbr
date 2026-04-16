#include "policy_rule.hpp"

#include <algorithm>

#include "../log/logger.hpp"

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

PolicyRuleManager::PolicyRuleManager(NetlinkManager& netlink, bool dry_run)
    : netlink_(netlink),
      dry_run_(dry_run) {}

PolicyRuleManager::~PolicyRuleManager() {
    // Best-effort cleanup on destruction
    try {
        clear();
    } catch (const std::exception& e) {
        Logger::instance().error("PolicyRuleManager cleanup failed during destruction: {}",
                                 e.what());
    } catch (...) {
        Logger::instance().error(
            "PolicyRuleManager cleanup failed during destruction: unknown error");
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
    if (!dry_run_) {
        netlink_.add_rule(spec);
    }
    rules_.push_back(spec);
}

void PolicyRuleManager::remove(const RuleSpec& spec) {
    auto it = std::find_if(rules_.begin(), rules_.end(),
                           [&](const RuleSpec& r) { return rules_equal(r, spec); });
    if (it == rules_.end()) {
        return;
    }
    if (!dry_run_) {
        netlink_.delete_rule(spec);
    }
    rules_.erase(it);
}

void PolicyRuleManager::clear() {
    // Remove in reverse order (last added first)
    for (auto it = rules_.rbegin(); it != rules_.rend(); ++it) {
        try {
            if (!dry_run_) {
                netlink_.delete_rule(*it);
            }
        } catch (const std::exception& e) {
            Logger::instance().error(
                "Failed to delete policy rule during clear() (table={}, fwmark={}, mask={}, priority={}, family={}): {}",
                it->table,
                it->fwmark,
                it->fwmask,
                it->priority,
                it->family,
                e.what());
        } catch (...) {
            Logger::instance().error(
                "Failed to delete policy rule during clear() (table={}, fwmark={}, mask={}, priority={}, family={}): unknown error",
                it->table,
                it->fwmark,
                it->fwmask,
                it->priority,
                it->family);
        }
    }
    rules_.clear();
}

} // namespace keen_pbr3
