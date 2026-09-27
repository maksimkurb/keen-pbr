#include "firewall_plan_verifier.hpp"

#include "../util/format_compat.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace keen_pbr3 {
namespace {

bool starts_with(const std::string& value, const char* prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool has_iptables_interface(const std::string& raw,
                            const std::string& expected) {
    std::istringstream stream(raw);
    std::string previous;
    std::string token;
    while (stream >> token) {
        if (previous == "-i" && token == expected) return true;
        previous = std::move(token);
    }
    return false;
}

bool is_owned_module(const std::string& module) {
    return starts_with(module, "route.") || starts_with(module, "dns.") ||
           starts_with(module, "prefilter.");
}

std::string family_name(FirewallFamily family) {
    switch (family) {
    case FirewallFamily::ipv4: return "ipv4";
    case FirewallFamily::ipv6: return "ipv6";
    case FirewallFamily::any: return "any";
    }
    return "unknown";
}

std::string hook_name(FirewallHook hook) {
    return hook == FirewallHook::output ? "OUTPUT" : "PREROUTING";
}

std::string action_name(const FirewallRuleAction& action) {
    if (std::holds_alternative<MarkAction>(action)) return "mark";
    if (std::holds_alternative<BalanceAction>(action)) return "balance";
    if (const auto* verdict = std::get_if<VerdictAction>(&action)) {
        return *verdict == VerdictAction::drop ? "drop" : "pass";
    }
    if (std::holds_alternative<RestoreConntrackMarkAction>(action)) {
        return "restore_conntrack_mark";
    }
    if (std::holds_alternative<SkipEstablishedOrDnatAction>(action)) {
        return "skip_established_or_dnat";
    }
    if (std::holds_alternative<SkipMarkedPacketsAction>(action)) {
        return "skip_marked_packets";
    }
    return "inbound_interface";
}

std::string criteria_summary(const FirewallRuleCriteria& criteria) {
    std::string result;
    if (criteria.dst_set_name.has_value()) {
        result += "set=" + normalize_firewall_set_name(*criteria.dst_set_name);
    }
    if (criteria.proto != L4Proto::Any) {
        if (!result.empty()) result += ' ';
        result += "proto=" + std::string(l4_proto_name(criteria.proto));
    }
    if (criteria.dscp.has_value()) {
        if (!result.empty()) result += ' ';
        result += "dscp=" + std::to_string(static_cast<unsigned>(*criteria.dscp));
    }
    if (!criteria.src_port.empty()) {
        if (!result.empty()) result += ' ';
        result += "sport=" + criteria.src_port.to_config_string();
    }
    if (!criteria.dst_port.empty()) {
        if (!result.empty()) result += ' ';
        result += "dport=" + criteria.dst_port.to_config_string();
    }
    if (!criteria.src_addr.empty()) {
        if (!result.empty()) result += ' ';
        result += "src=" + criteria.src_addr.front();
    }
    if (!criteria.dst_addr.empty()) {
        if (!result.empty()) result += ' ';
        result += "dst=" + criteria.dst_addr.front();
    }
    return result.empty() ? "any" : result;
}

std::string rule_key_text(const FirewallRuleKey& key) {
    return key.module_id + ":" + key.instance_id;
}

std::string keyed_detail(const FirewallRuleInstance& rule,
                         std::string detail) {
    return keen_pbr3::format("key={} {}", rule_key_text(rule.key), detail);
}

std::string balance_mismatch_detail(const ObservedFirewallRule& observed,
                                    const BalanceAction& expected,
                                    uint32_t fwmark_mask);

bool action_equal(const FirewallRuleAction& left, const FirewallRuleAction& right) {
    if (const auto* left_mark = std::get_if<MarkAction>(&left)) {
        const auto* right_mark = std::get_if<MarkAction>(&right);
        return right_mark != nullptr && *left_mark == *right_mark;
    }
    if (const auto* left_verdict = std::get_if<VerdictAction>(&left)) {
        const auto* right_verdict = std::get_if<VerdictAction>(&right);
        return right_verdict != nullptr && *left_verdict == *right_verdict;
    }
    if (const auto* left_restore =
            std::get_if<RestoreConntrackMarkAction>(&left)) {
        const auto* right_restore =
            std::get_if<RestoreConntrackMarkAction>(&right);
        return right_restore != nullptr && *left_restore == *right_restore;
    }
    if (const auto* left_skip =
            std::get_if<SkipEstablishedOrDnatAction>(&left)) {
        return std::holds_alternative<SkipEstablishedOrDnatAction>(right) &&
               *left_skip == std::get<SkipEstablishedOrDnatAction>(right);
    }
    if (const auto* left_skip = std::get_if<SkipMarkedPacketsAction>(&left)) {
        return std::holds_alternative<SkipMarkedPacketsAction>(right) &&
               *left_skip == std::get<SkipMarkedPacketsAction>(right);
    }
    const auto* left_inbound =
        std::get_if<InboundInterfaceFilterAction>(&left);
    const auto* right_inbound =
        std::get_if<InboundInterfaceFilterAction>(&right);
    return left_inbound != nullptr && right_inbound != nullptr &&
           *left_inbound == *right_inbound;
}

bool action_equal(const ObservedFirewallRule& observed,
                  const FirewallRuleAction& expected,
                  uint32_t fwmark_mask) {
    if (const auto* balance = std::get_if<BalanceAction>(&expected)) {
        return balance_mismatch_detail(observed, *balance, fwmark_mask).empty();
    }
    return action_equal(observed.action, expected);
}

bool rule_equal(const ObservedFirewallRule& observed,
                const FirewallRuleInstance& expected,
                FirewallHook hook,
                FirewallFamily family,
                const FirewallRuleCriteria& criteria,
                const FirewallRuleAction& action,
                bool restore_conntrack_companion,
                const std::string& inbound_interface,
                uint32_t fwmark_mask) {
    const bool family_matches = observed.family == family ||
        (observed.family == FirewallFamily::any &&
         !std::holds_alternative<BalanceAction>(expected.action));
    if (observed.hook != hook || !family_matches) {
        return false;
    }
    if (std::holds_alternative<RestoreConntrackMarkAction>(expected.action) &&
        observed.restore_conntrack_companion != restore_conntrack_companion) {
        return false;
    }
    if (!inbound_interface.empty() &&
        (observed.raw.empty() ||
         !has_iptables_interface(observed.raw, inbound_interface))) {
        return false;
    }
    return firewall_rule_criteria_equal(observed.criteria, criteria) &&
           action_equal(observed, action, fwmark_mask);
}

bool same_shape(const ObservedFirewallRule& observed,
                const FirewallRuleInstance& expected,
                FirewallHook hook,
                FirewallFamily family,
                const FirewallRuleCriteria& criteria,
                bool restore_conntrack_companion,
                const std::string& inbound_interface) {
    const bool family_matches = observed.family == family ||
        (observed.family == FirewallFamily::any &&
         !std::holds_alternative<BalanceAction>(expected.action));
    return observed.hook == hook &&
           family_matches &&
           (!std::holds_alternative<RestoreConntrackMarkAction>(expected.action) ||
            observed.restore_conntrack_companion == restore_conntrack_companion) &&
           (inbound_interface.empty() ||
            (!observed.raw.empty() &&
             has_iptables_interface(observed.raw, inbound_interface))) &&
           firewall_rule_criteria_equal(observed.criteria, criteria);
}

struct ExpectedPhysicalRule {
    FirewallFamily family{FirewallFamily::ipv4};
    FirewallHook hook{FirewallHook::prerouting};
    bool restore_conntrack_companion{false};
    std::string inbound_interface;
    FirewallRuleCriteria criteria;
    FirewallRuleAction action{MarkAction{}};
};

std::vector<ExpectedPhysicalRule> expand_expected_rule(
    const FirewallRuleInstance& rule, FirewallBackend backend,
    uint32_t fwmark_mask, RawPreroutingMode raw_prerouting = {},
    const std::vector<std::string>* inbound_interfaces = nullptr) {
    std::vector<ExpectedPhysicalRule> result;

    const auto add_prefilter = [&](FirewallHook hook, FirewallFamily family,
                                   bool restore_companion = false) {
        ExpectedPhysicalRule physical;
        physical.family = family;
        physical.hook = hook;
        physical.restore_conntrack_companion = restore_companion;
        physical.action = rule.action;
        result.push_back(std::move(physical));
    };
    if (std::holds_alternative<RestoreConntrackMarkAction>(rule.action)) {
        if (backend == FirewallBackend::nftables) {
            add_prefilter(FirewallHook::prerouting, FirewallFamily::any);
            add_prefilter(FirewallHook::output, FirewallFamily::any);
        } else {
            for (const auto family : {FirewallFamily::ipv4,
                                      FirewallFamily::ipv6}) {
                if (!raw_prerouting.uses(family == FirewallFamily::ipv6)) {
                    add_prefilter(FirewallHook::prerouting, family);
                    add_prefilter(FirewallHook::prerouting, family, true);
                }
                add_prefilter(FirewallHook::output, family);
                add_prefilter(FirewallHook::output, family, true);
            }
        }
        return result;
    }
    if (std::holds_alternative<SkipEstablishedOrDnatAction>(rule.action)) {
        if (backend == FirewallBackend::iptables) {
            for (const auto family : {FirewallFamily::ipv4,
                                      FirewallFamily::ipv6}) {
                add_prefilter(FirewallHook::output, family);
                if (!raw_prerouting.uses(family == FirewallFamily::ipv6)) {
                    add_prefilter(FirewallHook::prerouting, family);
                }
            }
        } else if (backend == FirewallBackend::nftables) {
            add_prefilter(FirewallHook::prerouting, FirewallFamily::any);
        } else {
            add_prefilter(FirewallHook::prerouting, FirewallFamily::ipv4);
            add_prefilter(FirewallHook::output, FirewallFamily::ipv4);
            add_prefilter(FirewallHook::prerouting, FirewallFamily::ipv6);
            add_prefilter(FirewallHook::output, FirewallFamily::ipv6);
        }
        return result;
    }
    if (std::holds_alternative<SkipMarkedPacketsAction>(rule.action)) {
        if (backend == FirewallBackend::nftables) {
            add_prefilter(FirewallHook::prerouting, FirewallFamily::any);
            add_prefilter(FirewallHook::output, FirewallFamily::any);
        } else {
            for (const auto family : {FirewallFamily::ipv4,
                                      FirewallFamily::ipv6}) {
                add_prefilter(FirewallHook::prerouting, family);
                add_prefilter(FirewallHook::output, family);
            }
        }
        return result;
    }
    if (const auto* inbound =
            std::get_if<InboundInterfaceFilterAction>(&rule.action)) {
        if (inbound->interfaces.size() > 1U) {
            return result;
        }
        if (backend == FirewallBackend::nftables) {
            add_prefilter(FirewallHook::prerouting, FirewallFamily::any);
        } else {
            for (const auto family : {FirewallFamily::ipv4,
                                      FirewallFamily::ipv6}) {
                add_prefilter(FirewallHook::prerouting, family);
                add_prefilter(FirewallHook::output, family);
            }
        }
        return result;
    }
    for (const auto& materialized : materialize_firewall_classifiers(
             rule, backend, fwmark_mask, inbound_interfaces)) {
        ExpectedPhysicalRule physical;
        physical.family = materialized.family;
        physical.hook = materialized.hook;
        physical.criteria = materialized.criteria;
        physical.action = materialized.action;
        physical.inbound_interface = materialized.inbound_interface;
        result.push_back(std::move(physical));
    }
    return result;
}

FirewallRuleCheck make_check(const FirewallRuleInstance& rule) {
    FirewallRuleCheck check;
    check.set_name = rule.criteria.dst_set_name.has_value()
        ? normalize_firewall_set_name(*rule.criteria.dst_set_name) : "<direct>";
    check.action = action_name(rule.action);
    if (const auto* mark = std::get_if<MarkAction>(&rule.action)) {
        check.expected_fwmark = mark->value;
    } else if (const auto* balance = std::get_if<BalanceAction>(&rule.action)) {
        check.expected_fwmark = balance->fallback_mark;
    }
    return check;
}

std::string expected_action_detail(const FirewallRuleAction& action) {
    if (const auto* mark = std::get_if<MarkAction>(&action)) {
        return keen_pbr3::format("mark {:#x}/{:#x}", mark->value, mark->mask);
    }
    return action_name(action);
}

std::string observed_action_detail(const FirewallRuleAction& action) {
    if (const auto* mark = std::get_if<MarkAction>(&action)) {
        return keen_pbr3::format("mark {:#x}/{:#x}", mark->value, mark->mask);
    }
    return action_name(action);
}

bool is_prefilter_action(const FirewallRuleAction& action) {
    return std::holds_alternative<RestoreConntrackMarkAction>(action) ||
           std::holds_alternative<SkipEstablishedOrDnatAction>(action) ||
           std::holds_alternative<SkipMarkedPacketsAction>(action) ||
           std::holds_alternative<InboundInterfaceFilterAction>(action);
}

bool inbound_filter_present(const FirewallSnapshot& snapshot,
                            const InboundInterfaceFilterAction& expected,
                            bool require_route_fragments) {
    for (const auto& observed : snapshot.rules) {
        if (const auto* actual =
                std::get_if<InboundInterfaceFilterAction>(&observed.action)) {
            if (*actual == expected) return true;
        }
    }
    if (expected.interfaces.size() <= 1U) return false;
    if (!require_route_fragments) return false;
    bool route_rule_seen = false;
    std::set<std::string> materialized_interfaces;
    for (const auto& observed : snapshot.rules) {
        if (is_prefilter_action(observed.action) || observed.raw.empty()) {
            continue;
        }
        route_rule_seen = true;
        bool has_allowed_interface = false;
        for (const auto& interface : expected.interfaces) {
            if (has_iptables_interface(observed.raw, interface)) {
                materialized_interfaces.insert(interface);
                has_allowed_interface = true;
            }
        }
        if (!has_allowed_interface) return false;
    }
    return route_rule_seen &&
           materialized_interfaces.size() == expected.interfaces.size();
}

bool legacy_rule_usable(const ObservedFirewallRule& rule);

bool legacy_prefilter_rule_usable(const ObservedFirewallRule& rule,
                                  FirewallBackend backend);

struct PrefilterLocation {
    std::size_t plan_index{0};
    std::size_t physical_index{0};
    std::size_t observed_index{0};
};

struct ShapeMatch {
    std::size_t physical_index{0};
    std::size_t observed_index{0};
};

std::set<std::size_t> prefilter_order_errors(
    const FirewallPlan& plan, const FirewallSnapshot& snapshot) {
    std::set<std::size_t> errors;
    std::vector<bool> used(snapshot.rules.size(), false);
    std::map<std::string, std::vector<PrefilterLocation>> groups;

    const auto group_name = [](const ObservedFirewallRule& rule) {
        return std::to_string(static_cast<int>(rule.hook)) + ":" +
               std::to_string(static_cast<int>(rule.family)) + ":" +
               rule.chain;
    };
    for (std::size_t plan_index = 0; plan_index < plan.rules.size();
         ++plan_index) {
        const auto& expected = plan.rules[plan_index];
        if (!is_prefilter_action(expected.action)) continue;
        const auto physical = expand_expected_rule(
            expected, snapshot.backend, plan.fwmark_mask,
            snapshot.raw_prerouting);
        std::vector<std::size_t> candidates;
        for (std::size_t index = 0; index < snapshot.rules.size(); ++index) {
            if (snapshot.rules[index].key.has_value() &&
                *snapshot.rules[index].key == expected.key) {
                candidates.push_back(index);
            }
        }
        for (std::size_t index = 0; index < snapshot.rules.size(); ++index) {
            if (legacy_prefilter_rule_usable(snapshot.rules[index],
                                             snapshot.backend)) {
                candidates.push_back(index);
            }
        }
        for (std::size_t physical_index = 0;
             physical_index < physical.size(); ++physical_index) {
            const auto& physical_rule = physical[physical_index];
            const auto match = std::find_if(
                candidates.begin(), candidates.end(), [&](std::size_t index) {
                    return !used[index] &&
                        rule_equal(snapshot.rules[index], expected,
                                   physical_rule.hook, physical_rule.family,
                                   physical_rule.criteria, physical_rule.action,
                                   physical_rule.restore_conntrack_companion,
                                   physical_rule.inbound_interface,
                                   plan.fwmark_mask);
                });
            if (match == candidates.end()) continue;
            used[*match] = true;
            groups[group_name(snapshot.rules[*match])].push_back(
                {plan_index, physical_index, *match});
        }
    }

    for (auto& [group, locations] : groups) {
        (void)group;
        const bool has_order = std::all_of(
            locations.begin(), locations.end(), [&](const PrefilterLocation& location) {
                return !snapshot.rules[location.observed_index].raw.empty();
            });
        if (has_order) {
            for (std::size_t index = 1; index < locations.size(); ++index) {
                const auto& previous = snapshot.rules[locations[index - 1].observed_index];
                const auto& current = snapshot.rules[locations[index].observed_index];
                if (current.order <= previous.order) {
                    errors.insert(locations[index].plan_index);
                }
            }
            for (const auto& location : locations) {
                const auto& prefilter = snapshot.rules[location.observed_index];
                for (const auto& observed : snapshot.rules) {
                    if (observed.chain != prefilter.chain ||
                        observed.hook != prefilter.hook ||
                        observed.family != prefilter.family ||
                        is_prefilter_action(observed.action) ||
                        observed.raw.empty()) {
                        continue;
                    }
                    if (observed.order < prefilter.order) {
                        errors.insert(location.plan_index);
                        break;
                    }
                }
            }
        }

        for (const auto& location : locations) {
            const auto& expected = plan.rules[location.plan_index];
            if (snapshot.backend != FirewallBackend::iptables ||
                !std::holds_alternative<RestoreConntrackMarkAction>(expected.action) ||
                location.physical_index + 1U >=
                    expand_expected_rule(expected, snapshot.backend,
                                         plan.fwmark_mask,
                                         snapshot.raw_prerouting).size() ||
                location.physical_index % 2U != 0U) {
                continue;
            }
            const auto next = std::find_if(
                locations.begin(), locations.end(), [&](const PrefilterLocation& candidate) {
                    return candidate.plan_index == location.plan_index &&
                           candidate.physical_index == location.physical_index + 1U;
                });
            if (next == locations.end() ||
                snapshot.rules[next->observed_index].chain !=
                    snapshot.rules[location.observed_index].chain ||
                snapshot.rules[next->observed_index].hook !=
                    snapshot.rules[location.observed_index].hook ||
                snapshot.rules[next->observed_index].family !=
                    snapshot.rules[location.observed_index].family ||
                snapshot.rules[next->observed_index].order !=
                    snapshot.rules[location.observed_index].order + 1U) {
                errors.insert(location.plan_index);
            }
        }
    }
    return errors;
}

std::string balance_mismatch_detail(const ObservedFirewallRule& observed,
                                    const BalanceAction& expected,
                                    uint32_t fwmark_mask) {
    const auto* actual_action = std::get_if<BalanceAction>(&observed.action);
    if (actual_action == nullptr) {
        return keen_pbr3::format("action mismatch: expected balance got {}",
                                 observed_action_detail(observed.action));
    }
    if ((actual_action->fallback_mark != 0 &&
         actual_action->fallback_mark != expected.fallback_mark) ||
        actual_action->candidates.size() != expected.candidates.size() ||
        !std::equal(
            actual_action->candidates.begin(), actual_action->candidates.end(),
            expected.candidates.begin(),
            [](const FirewallBalanceCandidate& lhs,
               const FirewallBalanceCandidate& rhs) {
                return lhs.fwmark == rhs.fwmark;
            })) {
        return "balance candidate mapping mismatch";
    }
    if (!observed.balance.has_value()) {
        return "balance details missing from nft snapshot";
    }
    const auto& actual = *observed.balance;
    if (actual.selector_mode != "inc") {
        return keen_pbr3::format("balance selector mode mismatch: expected inc got {}",
                                 actual.selector_mode.empty() ? "<missing>"
                                                               : actual.selector_mode);
    }
    if (actual.selector_modulus != expected.candidates.size()) {
        return keen_pbr3::format(
            "balance selector modulus mismatch: expected {} got {}",
            expected.candidates.size(), actual.selector_modulus);
    }
    if (!actual.mark_guard_present) {
        return "balance owned-mark-empty guard missing";
    }
    if (actual.mark_guard_op != "==" || actual.mark_guard_value != 0) {
        return "balance owned-mark-empty guard operator/value mismatch";
    }
    if (actual.mark_guard_mask != fwmark_mask) {
        return keen_pbr3::format(
            "balance owned-mark-empty guard mask mismatch: expected {:#x} got {:#x}",
            fwmark_mask, actual.mark_guard_mask);
    }
    const auto candidate_count = expected.candidates.size();
    if (actual.target_indices.size() != candidate_count) {
        return "balance vmap target count mismatch";
    }
    if (actual.target_marks.size() != candidate_count) {
        return "balance vmap mark count mismatch";
    }
    if (actual.setter_actions.size() != candidate_count) {
        return "balance setter count mismatch";
    }
    if (actual.setter_ct_actions.size() != candidate_count) {
        return "balance conntrack setter count mismatch";
    }
    for (std::size_t index = 0; index < candidate_count; ++index) {
        if (actual.target_indices[index] != index) {
            return keen_pbr3::format(
                "balance vmap index/order mismatch at position {}", index);
        }
        if (actual.target_marks[index] != expected.candidates[index].fwmark) {
            return keen_pbr3::format(
                "balance vmap mark mismatch at index {}: expected {:#x} got {:#x}",
                index, expected.candidates[index].fwmark,
                actual.target_marks[index]);
        }
        if (!actual.setter_actions[index].has_value()) {
            return keen_pbr3::format("balance setter missing at index {}", index);
        }
        if (actual.setter_actions[index]->value != expected.candidates[index].fwmark) {
            return keen_pbr3::format(
                "balance setter value mismatch at index {}: expected {:#x} got {:#x}",
                index, expected.candidates[index].fwmark,
                actual.setter_actions[index]->value);
        }
        if (actual.setter_actions[index]->mask != fwmark_mask) {
            return keen_pbr3::format(
                "balance setter mask mismatch at index {}: expected {:#x} got {:#x}",
                index, fwmark_mask, actual.setter_actions[index]->mask);
        }
        if (!actual.setter_ct_actions[index].has_value()) {
            return keen_pbr3::format("balance conntrack setter missing at index {}", index);
        }
        if (actual.setter_ct_actions[index]->value != expected.candidates[index].fwmark) {
            return keen_pbr3::format(
                "balance conntrack setter value mismatch at index {}: expected {:#x} got {:#x}",
                index, expected.candidates[index].fwmark,
                actual.setter_ct_actions[index]->value);
        }
        if (actual.setter_ct_actions[index]->mask != fwmark_mask) {
            return keen_pbr3::format(
                "balance conntrack setter mask mismatch at index {}: expected {:#x} got {:#x}",
                index, fwmark_mask, actual.setter_ct_actions[index]->mask);
        }
    }
    return {};
}

std::string mismatch_detail(const FirewallRuleInstance& expected,
                            const ExpectedPhysicalRule& physical,
                            const ObservedFirewallRule& observed,
                            uint32_t fwmark_mask) {
    if (observed.hook != physical.hook) {
        return keyed_detail(expected, keen_pbr3::format(
            "hook mismatch: expected {} got {}", hook_name(physical.hook),
            hook_name(observed.hook)));
    }
    if (observed.family != FirewallFamily::any &&
        observed.family != physical.family) {
        return keyed_detail(expected, keen_pbr3::format(
            "family mismatch: expected {} got {}", family_name(physical.family),
            family_name(observed.family)));
    }
    if (!firewall_rule_criteria_equal(observed.criteria, physical.criteria)) {
        return keyed_detail(expected, keen_pbr3::format(
            "criteria mismatch: expected {} got {}",
            criteria_summary(physical.criteria),
            criteria_summary(observed.criteria)));
    }
    if (const auto* balance = std::get_if<BalanceAction>(&physical.action)) {
        return keyed_detail(expected,
                            balance_mismatch_detail(observed, *balance, fwmark_mask));
    }
    return keyed_detail(expected, keen_pbr3::format(
        "action mismatch: expected {} got {}",
        expected_action_detail(expected.action),
        observed_action_detail(observed.action)));
}

bool legacy_rule_usable(const ObservedFirewallRule& rule) {
    // Keep the PR4-era no-comment upgrade window bounded to observations that
    // inspectors already restricted to owned active chains. Unknown comments
    // never enter this fallback and therefore cannot widen cleanup scope.
    return rule.legacy && !rule.key.has_value();
}

bool legacy_prefilter_rule_usable(const ObservedFirewallRule& rule,
                                  FirewallBackend backend) {
    if (!legacy_rule_usable(rule) || !is_prefilter_action(rule.action)) {
        return false;
    }
    if (rule.chain.empty()) return true;
    if (backend == FirewallBackend::nftables) {
        return rule.chain == "prerouting" || rule.chain == "output";
    }
    return rule.chain == "KeenPbrTable" || rule.chain == "KeenPbrTable_A" ||
           rule.chain == "KeenPbrTable_B" || rule.chain == "KeenPbrTable_OUTPUT" ||
           rule.chain == "KeenPbrRaw" || rule.chain == "KeenPbrRaw_A" ||
           rule.chain == "KeenPbrRaw_B" || rule.chain == "KeenPbrOutput" ||
           rule.chain == "KeenPbrOutput_A" || rule.chain == "KeenPbrOutput_B";
}

} // namespace

std::vector<FirewallRuleCheck> verify_firewall_plan(
    const FirewallPlan& plan, const FirewallSnapshot& snapshot) {
    std::vector<FirewallRuleCheck> checks;
    if (!snapshot.error.empty() || !snapshot.available) {
        for (const auto& rule : plan.rules) {
            auto check = make_check(rule);
            check.status = CheckStatus::missing;
            check.detail = snapshot.error.empty() ? "firewall snapshot unavailable"
                                                  : snapshot.error;
            checks.push_back(std::move(check));
        }
        return checks;
    }

    std::vector<bool> used(snapshot.rules.size(), false);
    const bool has_materialized_route = std::any_of(
        plan.rules.begin(), plan.rules.end(), [&](const FirewallRuleInstance& rule) {
            return rule.source_rule_index != std::numeric_limits<std::size_t>::max() &&
                   !is_prefilter_action(rule.action) &&
                   !expand_expected_rule(rule, snapshot.backend, plan.fwmark_mask,
                                         snapshot.raw_prerouting).empty();
        });
    const auto prefilter_errors = prefilter_order_errors(plan, snapshot);
    std::set<std::string> desired_keys;
    for (const auto& rule : plan.rules) {
        const auto physical =
            expand_expected_rule(rule, snapshot.backend, plan.fwmark_mask,
                                 snapshot.raw_prerouting);
        bool materialized = !physical.empty();
        if (const auto* inbound =
                std::get_if<InboundInterfaceFilterAction>(&rule.action)) {
            materialized = inbound->interfaces.size() <= 1U ||
                           snapshot.backend == FirewallBackend::nftables ||
                           has_materialized_route;
        }
        if (materialized) {
            desired_keys.insert(rule.key.module_id + ":" + rule.key.instance_id);
        }
    }

    const std::vector<std::string>* inbound_interfaces = nullptr;
    for (const auto& rule : plan.rules) {
        if (const auto* inbound =
                std::get_if<InboundInterfaceFilterAction>(&rule.action);
            inbound != nullptr && inbound->interfaces.size() > 1U) {
            inbound_interfaces = &inbound->interfaces;
            break;
        }
    }

    for (std::size_t plan_index = 0; plan_index < plan.rules.size(); ++plan_index) {
        const auto& expected = plan.rules[plan_index];
        auto check = make_check(expected);
        const auto physical = expand_expected_rule(expected, snapshot.backend,
                                                   plan.fwmark_mask,
                                                   snapshot.raw_prerouting,
                                                   is_prefilter_action(expected.action)
                                                       ? nullptr
                                                       : inbound_interfaces);
        std::vector<std::size_t> keyed;
        for (std::size_t index = 0; index < snapshot.rules.size(); ++index) {
            if (snapshot.rules[index].key.has_value() &&
                *snapshot.rules[index].key == expected.key) {
                keyed.push_back(index);
            }
        }

        std::vector<std::size_t> candidates = keyed;
        if (is_prefilter_action(expected.action)) {
            for (std::size_t index = 0; index < snapshot.rules.size(); ++index) {
                if (legacy_prefilter_rule_usable(snapshot.rules[index],
                                                 snapshot.backend)) {
                    candidates.push_back(index);
                }
            }
        } else if (candidates.empty()) {
            for (std::size_t index = 0; index < snapshot.rules.size(); ++index) {
                if (!used[index] && legacy_rule_usable(snapshot.rules[index])) {
                    candidates.push_back(index);
                }
            }
        }

        std::vector<std::size_t> matched;
        std::vector<ShapeMatch> shape_matches;
        std::vector<bool> matched_physical(physical.size(), false);
        for (std::size_t physical_index = 0;
             physical_index < physical.size(); ++physical_index) {
            const auto& physical_rule = physical[physical_index];
            auto match = std::find_if(candidates.begin(), candidates.end(),
                                      [&](std::size_t index) {
                return !used[index] &&
                       rule_equal(snapshot.rules[index], expected,
                                  physical_rule.hook,
                                  physical_rule.family, physical_rule.criteria,
                                  physical_rule.action,
                                  physical_rule.restore_conntrack_companion,
                                  physical_rule.inbound_interface,
                                  plan.fwmark_mask);
            });
            if (match != candidates.end()) {
                used[*match] = true;
                matched.push_back(*match);
                matched_physical[physical_index] = true;
                continue;
            }
            auto shape = std::find_if(candidates.begin(), candidates.end(),
                                      [&](std::size_t index) {
                return !used[index] &&
                       same_shape(snapshot.rules[index], expected,
                                  physical_rule.hook,
                                  physical_rule.family, physical_rule.criteria,
                                  physical_rule.restore_conntrack_companion,
                                  physical_rule.inbound_interface);
            });
            if (shape != candidates.end()) {
                shape_matches.push_back({physical_index, *shape});
            }
        }

        if (physical.empty()) {
            if (const auto* inbound =
                    std::get_if<InboundInterfaceFilterAction>(&expected.action)) {
                const bool can_materialize = inbound->interfaces.size() <= 1U ||
                    snapshot.backend == FirewallBackend::nftables ||
                    has_materialized_route;
                if (can_materialize) {
                    check.status = inbound_filter_present(
                        snapshot, *inbound, has_materialized_route)
                        ? CheckStatus::ok : CheckStatus::missing;
                    check.detail = check.status == CheckStatus::ok
                        ? "ok" : "inbound interface filter not found";
                    checks.push_back(std::move(check));
                } else {
                    check.status = CheckStatus::ok;
                    check.detail = "inbound interface filter has no materialized route rules";
                    checks.push_back(std::move(check));
                }
            }
            // A family-specific set can be paired with criteria that only
            // contain the other address family.  The compatibility backends
            // omit that impossible physical rule; preserve the legacy
            // projection by not treating it as a missing live rule.
            continue;
        }
        if (matched.size() != physical.size()) {
            check.status = (!shape_matches.empty() || !keyed.empty())
                ? CheckStatus::mismatch : CheckStatus::missing;
            if (!shape_matches.empty() || !keyed.empty()) {
                // A logical rule may expand to OUTPUT and PREROUTING physical
                // companions, so the number of exact matches is not their
                // physical index.
                const auto mismatch_index = !shape_matches.empty()
                    ? shape_matches.front().observed_index : keyed.front();
                const auto physical_it = std::find(
                    matched_physical.begin(), matched_physical.end(), false);
                const auto physical_index = !shape_matches.empty()
                    ? shape_matches.front().physical_index
                    : (physical_it == matched_physical.end()
                           ? physical.size() - std::size_t{1}
                           : static_cast<std::size_t>(
                                 std::distance(matched_physical.begin(), physical_it)));
                check.detail = mismatch_detail(
                    expected, physical[physical_index],
                    snapshot.rules[mismatch_index], plan.fwmark_mask);
                if (const auto* mark = std::get_if<MarkAction>(
                        &snapshot.rules[mismatch_index].action)) {
                    check.actual_fwmark = mark->value;
                }
            } else {
                check.detail = keyed_detail(
                    expected, keyed.empty()
                        ? "rule not found in firewall snapshot"
                        : "owned rule key present but expected physical expansion is missing");
            }
            checks.push_back(std::move(check));
            continue;
        }
        if (prefilter_errors.find(plan_index) != prefilter_errors.end()) {
            check.status = CheckStatus::mismatch;
            check.detail = keyed_detail(expected, "prefilter physical order mismatch");
            checks.push_back(std::move(check));
            continue;
        }

        // MARK/CONNMARK/RETURN can repeat one comment for one physical
        // classifier.  RETURN is the iptables continuation of MARK and is not
        // a second logical rule; identical extra keyed actions are duplicates.
        bool duplicate = false;
        for (const auto index : keyed) {
            if (used[index]) continue;
            const auto& observed = snapshot.rules[index];
            const auto mark_is_consumed = [&](std::size_t candidate_index) {
                const auto& candidate = snapshot.rules[candidate_index];
                if (!std::holds_alternative<MarkAction>(candidate.action)) {
                    return false;
                }
                if (std::find(matched.begin(), matched.end(), candidate_index) !=
                    matched.end()) {
                    return true;
                }
                return !candidate.raw.empty() &&
                    std::any_of(matched.begin(), matched.end(), [&](std::size_t matched_index) {
                        const auto& matched_rule = snapshot.rules[matched_index];
                        return matched_rule.raw == candidate.raw &&
                               matched_rule.hook != candidate.hook;
                    });
            };
            const bool shared_chain_view = !observed.raw.empty() &&
                std::any_of(matched.begin(), matched.end(), [&](std::size_t matched_index) {
                    const auto& matched_rule = snapshot.rules[matched_index];
                    return matched_rule.raw == observed.raw &&
                           matched_rule.hook != observed.hook;
                });
            const bool paired_return =
                std::holds_alternative<MarkAction>(expected.action) &&
                std::holds_alternative<VerdictAction>(observed.action) &&
                std::get<VerdictAction>(observed.action) == VerdictAction::pass &&
                std::any_of(keyed.begin(), keyed.end(), [&](std::size_t matched_index) {
                    const auto& matched_rule = snapshot.rules[matched_index];
                    return mark_is_consumed(matched_index) &&
                           matched_rule.hook == observed.hook &&
                           matched_rule.family == observed.family &&
                           matched_rule.chain == observed.chain &&
                           firewall_rule_criteria_equal(matched_rule.criteria,
                                                        observed.criteria);
                });
            if (!paired_return && !shared_chain_view) duplicate = true;
        }
        if (duplicate) {
            check.status = CheckStatus::mismatch;
            check.detail = keyed_detail(expected, "duplicate observed rules for owned key");
        } else {
            check.status = CheckStatus::ok;
            check.detail = "ok";
        }
        for (const auto index : matched) {
            if (const auto* mark = std::get_if<MarkAction>(&snapshot.rules[index].action)) {
                check.actual_fwmark = mark->value;
                break;
            }
        }
        checks.push_back(std::move(check));
    }

    std::set<std::string> reported_extras;
    std::set<std::string> reported_legacy_prefilters;
    for (std::size_t index = 0; index < snapshot.rules.size(); ++index) {
        const auto& observed = snapshot.rules[index];
        if (legacy_prefilter_rule_usable(observed, snapshot.backend) &&
            !used[index]) {
            // iptables snapshots expose one physical shared-chain rule through
            // both hook views.  Collapse that compatibility projection while
            // retaining separate rules when their raw backend records differ.
            const std::string identity = observed.raw.empty()
                ? std::to_string(static_cast<int>(observed.family)) + ":" +
                      std::to_string(static_cast<int>(observed.hook)) + ":" +
                      observed.chain
                : std::to_string(static_cast<int>(observed.family)) + ":" +
                      observed.raw;
            if (!reported_legacy_prefilters.insert(identity).second) {
                continue;
            }
            auto check = FirewallRuleCheck{};
            check.set_name = observed.criteria.dst_set_name.has_value()
                ? normalize_firewall_set_name(*observed.criteria.dst_set_name) : "<direct>";
            check.action = action_name(observed.action);
            check.status = CheckStatus::mismatch;
            check.detail = "extra legacy prefilter rule";
            checks.push_back(std::move(check));
            continue;
        }
        if (!observed.key.has_value() || !is_owned_module(observed.key->module_id)) {
            continue;
        }
        const std::string key = observed.key->module_id + ":" +
                                observed.key->instance_id;
        if (desired_keys.find(key) != desired_keys.end() ||
            !reported_extras.insert(key).second) {
            continue;
        }
        auto check = FirewallRuleCheck{};
        check.set_name = observed.criteria.dst_set_name.has_value()
            ? normalize_firewall_set_name(*observed.criteria.dst_set_name) : "<direct>";
        check.action = action_name(observed.action);
        if (const auto* mark = std::get_if<MarkAction>(&observed.action)) {
            check.actual_fwmark = mark->value;
        }
        check.status = CheckStatus::mismatch;
        check.detail = "extra owned firewall rule key " + key;
        checks.push_back(std::move(check));
    }
    return checks;
}

} // namespace keen_pbr3
