#include "policy_rule.hpp"

#include <algorithm>
#include <netinet/in.h>

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

PolicyRuleManager::PolicyRuleManager(RuleNetlinkOperations& netlink, bool dry_run)
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
        const int families[] = {AF_INET, AF_INET6};
        const int* begin = families;
        const int* end = families + 2;
        int single_family = spec.family;
        if (spec.family != 0) {
            begin = &single_family;
            end = begin + 1;
        }
        std::vector<RuleSpec> newly_owned;
        try {
            for (auto family = begin; family != end; ++family) {
                if (netlink_.add_rule_for_family(spec, *family) == RuleAddResult::Created) {
                    RuleSpec concrete = spec;
                    concrete.family = *family;
                    newly_owned.push_back(concrete);
                }
            }
        } catch (...) {
            for (auto it = newly_owned.rbegin(); it != newly_owned.rend(); ++it) {
                try {
                    netlink_.delete_rule_for_family(*it, it->family);
                } catch (const std::exception& e) {
                    Logger::instance().error("Failed to roll back policy rule family {}: {}",
                                             it->family, e.what());
                }
            }
            throw;
        }
        owned_rules_.insert(owned_rules_.end(), newly_owned.begin(), newly_owned.end());
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
        for (auto owned = owned_rules_.begin(); owned != owned_rules_.end();) {
            if (rules_equal(*owned, RuleSpec{spec.fwmark, spec.fwmask, spec.table,
                                             spec.priority, owned->family}) &&
                (spec.family == 0 || spec.family == owned->family)) {
                netlink_.delete_rule_for_family(*owned, owned->family);
                owned = owned_rules_.erase(owned);
            } else {
                ++owned;
            }
        }
    }
    rules_.erase(it);
}

void PolicyRuleManager::clear() {
    // Remove in reverse order (last added first)
    for (auto it = owned_rules_.rbegin(); it != owned_rules_.rend(); ++it) {
        try {
            if (!dry_run_) {
                netlink_.delete_rule_for_family(*it, it->family);
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
    owned_rules_.clear();
    rules_.clear();
}

} // namespace keen_pbr3
