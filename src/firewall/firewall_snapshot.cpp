#include "firewall_snapshot.hpp"

#include "iptables_verifier.hpp"
#include "nftables_verifier.hpp"
#include "port_spec_util.hpp"
#include "../util/format_compat.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <netinet/in.h>
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

std::string normalize_set_name(const std::string& name) {
    if (starts_with(name, "kpbr4s_") || starts_with(name, "kpbr4S_")) {
        return "kpbr4_" + name.substr(7);
    }
    if (starts_with(name, "kpbr6s_") || starts_with(name, "kpbr6S_")) {
        return "kpbr6_" + name.substr(7);
    }
    return name;
}

bool is_ipv6_set_name(const std::string& name) {
    return starts_with(name, "kpbr6_") || starts_with(name, "kpbr6s_") ||
           starts_with(name, "kpbr6S_") || starts_with(name, "kpbr6d_");
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
        result += "set=" + normalize_set_name(*criteria.dst_set_name);
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

bool addr_equal(const std::string& left, const std::string& right) {
    const auto left_slash = left.rfind('/');
    const auto right_slash = right.rfind('/');
    if (left_slash == std::string::npos || right_slash == std::string::npos) {
        return left == right ||
               (left_slash != std::string::npos &&
                left.substr(left_slash + 1) ==
                    (left.find(':') == std::string::npos ? "32" : "128") &&
                left.substr(0, left_slash) == right) ||
               (right_slash != std::string::npos &&
                right.substr(right_slash + 1) ==
                    (right.find(':') == std::string::npos ? "32" : "128") &&
                right.substr(0, right_slash) == left);
    }
    return left == right;
}

bool addresses_equal(const std::vector<std::string>& left,
                     const std::vector<std::string>& right) {
    if (left.size() != right.size()) return false;
    return std::equal(left.begin(), left.end(), right.begin(), addr_equal);
}

bool criteria_equal(const FirewallRuleCriteria& left,
                    const FirewallRuleCriteria& right) {
    const bool sets_equal = (!left.dst_set_name.has_value() &&
                             !right.dst_set_name.has_value()) ||
        (left.dst_set_name.has_value() && right.dst_set_name.has_value() &&
         normalize_set_name(*left.dst_set_name) ==
             normalize_set_name(*right.dst_set_name));
    const bool gateway_equal =
        (left.default_gateway == right.default_gateway &&
         left.default_gateway_bypass == right.default_gateway_bypass) ||
        (left.default_gateway != DefaultGatewayFamily::None &&
         right.default_gateway == DefaultGatewayFamily::None &&
         right.negate_dst_addr &&
         addresses_equal(left.default_gateway_bypass, right.dst_addr)) ||
        (right.default_gateway != DefaultGatewayFamily::None &&
         left.default_gateway == DefaultGatewayFamily::None &&
         left.negate_dst_addr &&
         addresses_equal(right.default_gateway_bypass, left.dst_addr));
    const bool destination_equal =
        addresses_equal(left.dst_addr, right.dst_addr) ||
        (left.default_gateway != DefaultGatewayFamily::None &&
         right.default_gateway == DefaultGatewayFamily::None &&
         addresses_equal(left.default_gateway_bypass, right.dst_addr)) ||
        (right.default_gateway != DefaultGatewayFamily::None &&
         left.default_gateway == DefaultGatewayFamily::None &&
         addresses_equal(right.default_gateway_bypass, left.dst_addr));
    const bool destination_negation_equal =
        left.negate_dst_addr == right.negate_dst_addr ||
        (left.default_gateway != DefaultGatewayFamily::None &&
         right.default_gateway == DefaultGatewayFamily::None &&
         right.negate_dst_addr) ||
        (right.default_gateway != DefaultGatewayFamily::None &&
         left.default_gateway == DefaultGatewayFamily::None &&
         left.negate_dst_addr);
    return sets_equal && left.dscp == right.dscp && left.proto == right.proto &&
           left.src_port.to_config_string() == right.src_port.to_config_string() &&
           left.dst_port.to_config_string() == right.dst_port.to_config_string() &&
           addresses_equal(left.src_addr, right.src_addr) &&
           destination_equal &&
           left.negate_src_port == right.negate_src_port &&
           left.negate_dst_port == right.negate_dst_port &&
           left.negate_src_addr == right.negate_src_addr &&
           destination_negation_equal &&
           gateway_equal;
}

bool balance_details_equal(const ObservedFirewallRule& observed,
                           const BalanceAction& expected,
                           uint32_t fwmark_mask) {
    if (!observed.balance.has_value()) return false;
    const auto& actual = *observed.balance;
    if (actual.selector_mode != "inc" ||
        actual.selector_modulus != expected.candidates.size() ||
        !actual.mark_guard_present || actual.mark_guard_op != "==" ||
        actual.mark_guard_mask != fwmark_mask || actual.mark_guard_value != 0 ||
        actual.target_indices.size() != expected.candidates.size() ||
        actual.target_marks.size() != expected.candidates.size() ||
        actual.setter_actions.size() != expected.candidates.size() ||
        actual.setter_ct_actions.size() != expected.candidates.size()) {
        return false;
    }
    for (std::size_t index = 0; index < expected.candidates.size(); ++index) {
        if (actual.target_indices[index] != index ||
            actual.target_marks[index] != expected.candidates[index].fwmark ||
            !actual.setter_actions[index].has_value() ||
            actual.setter_actions[index]->value != expected.candidates[index].fwmark ||
            actual.setter_actions[index]->mask != fwmark_mask ||
            !actual.setter_ct_actions[index].has_value() ||
            actual.setter_ct_actions[index]->value != expected.candidates[index].fwmark ||
            actual.setter_ct_actions[index]->mask != fwmark_mask) {
            return false;
        }
    }
    return true;
}

bool action_equal(const FirewallRuleAction& left, const FirewallRuleAction& right) {
    if (const auto* left_mark = std::get_if<MarkAction>(&left)) {
        const auto* right_mark = std::get_if<MarkAction>(&right);
        return right_mark != nullptr && *left_mark == *right_mark;
    }
    if (const auto* left_balance = std::get_if<BalanceAction>(&left)) {
        const auto* right_balance = std::get_if<BalanceAction>(&right);
        if (right_balance == nullptr ||
            (right_balance->fallback_mark != 0 &&
             left_balance->fallback_mark != right_balance->fallback_mark) ||
            left_balance->candidates.size() != right_balance->candidates.size()) {
            return false;
        }
        // Family eligibility is applied while expanding a plan into a
        // physical classifier.  nft JSON does not retain that metadata on
        // the balance vmap, so compare the observable mark mapping only.
        return std::equal(
            left_balance->candidates.begin(), left_balance->candidates.end(),
            right_balance->candidates.begin(),
            [](const FirewallBalanceCandidate& lhs,
               const FirewallBalanceCandidate& rhs) {
                return lhs.fwmark == rhs.fwmark;
            });
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
    if (!action_equal(observed.action, expected)) return false;
    const auto* balance = std::get_if<BalanceAction>(&expected);
    return balance == nullptr || balance_details_equal(observed, *balance,
                                                        fwmark_mask);
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
    return criteria_equal(observed.criteria, criteria) &&
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
           criteria_equal(observed.criteria, criteria);
}

struct ExpectedPhysicalRule {
    FirewallFamily family{FirewallFamily::ipv4};
    FirewallHook hook{FirewallHook::prerouting};
    bool restore_conntrack_companion{false};
    std::string inbound_interface;
    FirewallRuleCriteria criteria;
    FirewallRuleAction action{MarkAction{}};
};

FirewallRuleAction action_for_family(const FirewallRuleAction& action,
                                     FirewallFamily family,
                                     uint32_t fwmark_mask) {
    const auto* balance = std::get_if<BalanceAction>(&action);
    if (balance == nullptr) return action;

    BalanceAction filtered;
    filtered.fallback_mark = balance->fallback_mark;
    for (const auto& candidate : balance->candidates) {
        if ((family == FirewallFamily::ipv4 && candidate.ipv4) ||
            (family == FirewallFamily::ipv6 && candidate.ipv6)) {
            // A physical nft rule contains only marks for its selected
            // address family.  Normalize the candidate flags to that family
            // before comparing the observed vmap with the plan.
            filtered.candidates.push_back(
                {candidate.fwmark, family == FirewallFamily::ipv4,
                 family == FirewallFamily::ipv6});
        }
    }
    if (filtered.candidates.empty()) {
        return MarkAction{filtered.fallback_mark, fwmark_mask};
    }
    if (filtered.candidates.size() == 1U) {
        return MarkAction{filtered.candidates.front().fwmark, fwmark_mask};
    }
    // The nft inspector can recover the candidate set from the numgen/vmap
    // expression, but the fallback mark is not encoded in that physical rule.
    filtered.fallback_mark = 0;
    return filtered;
}

bool needs_family_specific_rule(const FirewallRuleCriteria& criteria) {
    return criteria.dst_set_name.has_value() || criteria.dscp.has_value() ||
           !criteria.src_addr.empty() || !criteria.dst_addr.empty() ||
           !criteria.src_port.empty() || !criteria.dst_port.empty() ||
           criteria.default_gateway != DefaultGatewayFamily::None;
}

std::vector<L4Proto> expand_protocols(const FirewallRuleCriteria& criteria,
                                      FirewallBackend backend) {
    if (criteria.proto == L4Proto::TcpUdp ||
        (backend == FirewallBackend::iptables && criteria.proto == L4Proto::Any &&
         (!criteria.src_port.empty() || !criteria.dst_port.empty()))) {
        return {L4Proto::Tcp, L4Proto::Udp};
    }
    return {criteria.proto};
}

std::vector<std::string> family_addresses(const std::vector<std::string>& addresses,
                                          FirewallFamily family) {
    std::vector<std::string> result;
    for (const auto& address : addresses) {
        const bool ipv6 = address.find(':') != std::string::npos;
        if ((family == FirewallFamily::ipv6) == ipv6) result.push_back(address);
    }
    return result;
}

std::vector<FirewallFamily> expand_families(const FirewallRuleInstance& rule,
                                            FirewallBackend backend) {
    if (rule.family != FirewallFamily::any) return {rule.family};
    if (rule.criteria.dst_set_name.has_value()) {
        return {is_ipv6_set_name(*rule.criteria.dst_set_name)
                    ? FirewallFamily::ipv6
                    : FirewallFamily::ipv4};
    }
    if (rule.criteria.default_gateway == DefaultGatewayFamily::Ipv6) {
        return {FirewallFamily::ipv6};
    }
    if (rule.criteria.default_gateway == DefaultGatewayFamily::Ipv4) {
        return {FirewallFamily::ipv4};
    }
    if (backend == FirewallBackend::nftables &&
        std::holds_alternative<BalanceAction>(rule.action)) {
        // Unlike ordinary family-neutral rules, nft balance classifiers are
        // emitted for both families so each family can filter its candidates.
        return {FirewallFamily::ipv4, FirewallFamily::ipv6};
    }
    if (backend == FirewallBackend::nftables &&
        !needs_family_specific_rule(rule.criteria)) {
        return {FirewallFamily::ipv4};
    }
    return {FirewallFamily::ipv4, FirewallFamily::ipv6};
}

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
    for (const auto family : expand_families(rule, backend)) {
        if ((rule.criteria.default_gateway == DefaultGatewayFamily::Ipv4 &&
             family != FirewallFamily::ipv4) ||
            (rule.criteria.default_gateway == DefaultGatewayFamily::Ipv6 &&
             family != FirewallFamily::ipv6)) {
            continue;
        }

        const auto src = family_addresses(rule.criteria.src_addr, family);
        const auto dst = family_addresses(rule.criteria.dst_addr, family);
        if ((!rule.criteria.src_addr.empty() && src.empty()) ||
            (!rule.criteria.dst_addr.empty() && dst.empty())) {
            continue;
        }
        const auto protocols = expand_protocols(rule.criteria, backend);
        const std::vector<std::string> src_values =
            src.empty() ? std::vector<std::string>{} : src;
        const std::vector<std::string> dst_values =
            dst.empty() ? std::vector<std::string>{} : dst;
        const std::size_t src_count = backend == FirewallBackend::iptables
            ? std::max<std::size_t>(1U, src_values.size()) : 1U;
        const std::size_t dst_count = backend == FirewallBackend::iptables
            ? std::max<std::size_t>(1U, dst_values.size()) : 1U;
        for (const auto proto : protocols) {
            for (std::size_t src_index = 0; src_index < src_count; ++src_index) {
                for (std::size_t dst_index = 0; dst_index < dst_count; ++dst_index) {
                    ExpectedPhysicalRule physical;
                    physical.family = family;
                    physical.hook = rule.hook;
                    physical.criteria = rule.criteria;
                    physical.criteria.proto = proto;
                    if (!rule.criteria.src_addr.empty()) {
                        physical.criteria.src_addr =
                            backend == FirewallBackend::iptables
                                ? std::vector<std::string>{src_values[src_index]}
                                : src_values;
                    }
                    if (!rule.criteria.dst_addr.empty()) {
                        physical.criteria.dst_addr =
                            backend == FirewallBackend::iptables
                                ? std::vector<std::string>{dst_values[dst_index]}
                                : dst_values;
                    }
                    physical.action = action_for_family(rule.action, family,
                                                        fwmark_mask);
                    if (backend == FirewallBackend::iptables &&
                        inbound_interfaces != nullptr &&
                        inbound_interfaces->size() > 1U) {
                        for (const auto& interface : *inbound_interfaces) {
                            auto fragment = physical;
                            fragment.inbound_interface = interface;
                            result.push_back(std::move(fragment));
                        }
                    } else {
                        result.push_back(std::move(physical));
                    }
                }
            }
        }
    }

    // The default-gateway compatibility path deliberately installs an OUTPUT
    // classifier and a PREROUTING companion.  Both carry the same key.
    if (rule.hook == FirewallHook::output &&
        rule.criteria.default_gateway != DefaultGatewayFamily::None) {
        const auto output_rules = result;
        for (auto physical : output_rules) {
            physical.hook = FirewallHook::prerouting;
            physical.criteria.apply_output = false;
            result.push_back(std::move(physical));
        }
    }
    return result;
}

FirewallRuleCheck make_check(const FirewallRuleInstance& rule) {
    FirewallRuleCheck check;
    check.set_name = rule.criteria.dst_set_name.has_value()
        ? normalize_set_name(*rule.criteria.dst_set_name) : "<direct>";
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
    if (actual.target_indices.size() != expected.candidates.size()) {
        return "balance vmap target count mismatch";
    }
    for (std::size_t index = 0; index < expected.candidates.size(); ++index) {
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
    return "balance action mismatch";
}

std::string mismatch_detail(const FirewallRuleInstance& expected,
                            const ExpectedPhysicalRule& physical,
                            const ObservedFirewallRule& observed,
                            uint32_t fwmark_mask) {
    if (observed.hook != physical.hook) {
        return keen_pbr3::format("hook mismatch: expected {} got {}",
                                 hook_name(physical.hook), hook_name(observed.hook));
    }
    if (observed.family != FirewallFamily::any &&
        observed.family != physical.family) {
        return keen_pbr3::format("family mismatch: expected {} got {}",
                                 family_name(physical.family),
                                 family_name(observed.family));
    }
    if (!criteria_equal(observed.criteria, physical.criteria)) {
        return keen_pbr3::format("criteria mismatch: expected {} got {}",
                                 criteria_summary(physical.criteria),
                                 criteria_summary(observed.criteria));
    }
    if (const auto* balance = std::get_if<BalanceAction>(&physical.action)) {
        return balance_mismatch_detail(observed, *balance, fwmark_mask);
    }
    return keen_pbr3::format("action mismatch: expected {} got {}",
                             expected_action_detail(expected.action),
                             observed_action_detail(observed.action));
}

bool legacy_rule_usable(const ObservedFirewallRule& rule) {
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

void append_iptables_rules(FirewallSnapshot& snapshot,
                           const ParsedIptablesState& state,
                           const std::string& prerouting_chain,
                           bool selected_output,
                           std::optional<FirewallHook> hook_filter = std::nullopt,
                           FirewallFamily family = FirewallFamily::ipv4) {
    const auto active_for = [&](FirewallHook hook) -> const std::vector<std::string>& {
        return hook == FirewallHook::output ? state.active_output_chains
                                            : state.active_prerouting_chains;
    };
    const auto chain_is_active = [&](const ParsedIptablesRule& rule) {
        const auto& active = active_for(rule.hook);
        if (active.empty()) {
            // A missing/invalid dispatcher must not make every stale A/B
            // generation eligible for legacy semantic matching.  The only
            // safe fallback is a direct rule on the selected owned chain.
            return rule.chain_name == prerouting_chain;
        }
        return std::find(active.begin(), active.end(), rule.chain_name) != active.end();
    };

    for (const auto& parsed : state.rules) {
        if (hook_filter.has_value() && parsed.hook != *hook_filter) continue;
        if (!chain_is_active(parsed)) continue;
        ObservedFirewallRule observed;
        observed.hook = parsed.hook;
        observed.family = family;
        observed.criteria = parsed.criteria;
        if (!parsed.set_name.empty()) observed.criteria.dst_set_name = parsed.set_name;
        observed.raw = parsed.raw;
        observed.chain = parsed.chain_name;
        observed.order = parsed.order;
        observed.restore_conntrack_companion = parsed.is_restore_companion;
        observed.comment = parsed.comment;
        observed.legacy = !parsed.comment.has_value();
        if (parsed.comment.has_value()) {
            try {
                observed.key = FirewallRuleKey::from_comment(*parsed.comment);
            } catch (...) {
                // Unknown versions and malformed comments are deliberately not
                // eligible for legacy semantic matching or owned cleanup.
            }
        }
        if (parsed.is_restore_conntrack || parsed.is_restore_companion) {
            observed.action = RestoreConntrackMarkAction{
                parsed.conntrack_mark_mask};
        } else if (parsed.is_skip_dnat) {
            observed.action = SkipEstablishedOrDnatAction{};
        } else if (parsed.is_skip_marked) {
            observed.action = SkipMarkedPacketsAction{};
        } else if (parsed.is_inbound_filter) {
            observed.action = InboundInterfaceFilterAction{
                parsed.inbound_interfaces};
        } else if (parsed.is_mark) {
            observed.action = MarkAction{parsed.fwmark, parsed.xmark_mask};
        } else if (parsed.is_drop) {
            observed.action = VerdictAction::drop;
        } else if (parsed.is_pass) {
            observed.action = VerdictAction::pass;
        } else {
            continue;
        }
        snapshot.rules.push_back(std::move(observed));
        // Non-RAW iptables uses one generation chain for both stable
        // PREROUTING and OUTPUT dispatchers.  Keep both reachable hooks in
        // the neutral snapshot; the verifier collapses the two views back to
        // one physical logical rule by raw diagnostic.
        if (parsed.hook == FirewallHook::prerouting &&
            std::find(state.active_output_chains.begin(),
                      state.active_output_chains.end(), parsed.chain_name) !=
                state.active_output_chains.end()) {
            auto output_view = snapshot.rules.back();
            output_view.hook = FirewallHook::output;
            snapshot.rules.push_back(std::move(output_view));
        }
    }

    if (state.has_keen_pbr_chain && !selected_output) {
        snapshot.chains.push_back({prerouting_chain, FirewallHook::prerouting,
                                   family,
                                   state.has_prerouting_jump});
    }
    if (state.has_output_chain &&
        (!hook_filter.has_value() || *hook_filter == FirewallHook::output)) {
        for (const auto& name : state.output_chains) {
            snapshot.chains.push_back({name, FirewallHook::output,
                                       family,
                                       state.has_output_jump});
        }
    }
}

void append_nft_rules(FirewallSnapshot& snapshot,
                      const ParsedNftablesState& parsed) {
    for (const auto& rule : parsed.rules) {
        ObservedFirewallRule observed;
        observed.hook = rule.hook;
        observed.family = !rule.family_known
            ? FirewallFamily::any
            : (rule.ipv6 ? FirewallFamily::ipv6 : FirewallFamily::ipv4);
        observed.criteria = rule.criteria;
        if (!rule.set_name.empty()) observed.criteria.dst_set_name = rule.set_name;
        observed.comment = rule.comment;
        observed.raw = rule.raw;
        observed.order = rule.order;
        observed.chain = rule.hook == FirewallHook::output ? "output" : "prerouting";
        observed.legacy = !rule.comment.has_value();
        if (rule.comment.has_value()) {
            try {
                observed.key = FirewallRuleKey::from_comment(*rule.comment);
            } catch (...) {
            }
        }
        if (rule.is_restore_conntrack) {
            observed.action = RestoreConntrackMarkAction{
                rule.conntrack_mark_mask};
        } else if (rule.is_skip_dnat) {
            observed.action = SkipEstablishedOrDnatAction{};
        } else if (rule.is_skip_marked) {
            observed.action = SkipMarkedPacketsAction{};
        } else if (rule.is_inbound_filter) {
            observed.action = InboundInterfaceFilterAction{
                rule.inbound_interfaces};
        } else if (rule.is_balance) {
            BalanceAction balance;
            for (const auto mark : rule.balance_marks) {
                balance.candidates.push_back({mark, !rule.ipv6, rule.ipv6});
            }
            observed.action = std::move(balance);
            ObservedFirewallRule::BalanceDetails details;
            details.selector_mode = rule.balance_selector_mode;
            details.selector_modulus = rule.balance_selector_modulus;
            details.mark_guard_present = rule.balance_guard_present;
            details.mark_guard_op = rule.balance_guard_op;
            details.mark_guard_mask = rule.balance_guard_mask;
            details.mark_guard_value = rule.balance_guard_value;
            for (const auto& target : rule.balance_targets) {
                details.target_indices.push_back(target.index);
                details.target_marks.push_back(target.mark);
                details.setter_actions.push_back(target.setter);
                details.setter_ct_actions.push_back(target.setter_ct);
            }
            observed.balance = std::move(details);
        } else if (rule.is_mark) {
            observed.action = MarkAction{rule.fwmark, rule.xmark_mask};
        } else if (rule.is_drop) {
            observed.action = VerdictAction::drop;
        } else if (rule.is_pass) {
            observed.action = VerdictAction::pass;
        } else {
            continue;
        }
        snapshot.rules.push_back(std::move(observed));
    }
    if (parsed.has_prerouting_chain) {
        snapshot.chains.push_back({"prerouting", FirewallHook::prerouting,
                                   FirewallFamily::any,
                                   parsed.has_prerouting_hook});
    }
    if (parsed.has_output_chain) {
        snapshot.chains.push_back({"output", FirewallHook::output,
                                   FirewallFamily::any, parsed.has_output_hook});
    }
}

FirewallSnapshot inspect_iptables_snapshot_impl(const CommandRunner& runner,
                                                RawPreroutingMode raw_prerouting) {
    FirewallSnapshot snapshot;
    snapshot.backend = FirewallBackend::iptables;
    snapshot.raw_prerouting = raw_prerouting;

    const auto read = [&](const std::vector<std::string>& args) {
        return runner(args);
    };
    const auto v4_table = raw_prerouting.ipv4 ? "raw" : "mangle";
    const auto v6_table = raw_prerouting.ipv6 ? "raw" : "mangle";
    const auto v4 = read({"iptables", "-t", v4_table, "-S"});
    const auto v6 = read({"ip6tables", "-t", v6_table, "-S"});
    CommandResult v4_output;
    CommandResult v6_output;
    if (raw_prerouting.ipv4) {
        v4_output = read({"iptables", "-t", "mangle", "-S"});
    }
    if (raw_prerouting.ipv6) {
        v6_output = read({"ip6tables", "-t", "mangle", "-S"});
    }
    const auto sets = read({"ipset", "save"});

    const auto failed = [](const CommandResult& result) {
        return result.exit_code != 0 || result.truncated;
    };
    if (failed(v4) || failed(v6) || failed(sets) ||
        (raw_prerouting.ipv4 && failed(v4_output)) ||
        (raw_prerouting.ipv6 && failed(v6_output))) {
        snapshot.error = "failed to inspect iptables firewall state";
        return snapshot;
    }

    const auto v4_state = parse_iptables_s_family(
        v4.stdout_output, false,
        raw_prerouting.ipv4 ? "KeenPbrRaw" : "KeenPbrTable");
    const auto v6_state = parse_iptables_s_family(
        v6.stdout_output, true,
        raw_prerouting.ipv6 ? "KeenPbrRaw" : "KeenPbrTable");
    if (!raw_prerouting.ipv4) {
        append_iptables_rules(snapshot, v4_state, "KeenPbrTable", false,
                              std::nullopt, FirewallFamily::ipv4);
    }
    if (!raw_prerouting.ipv6) {
        append_iptables_rules(snapshot, v6_state, "KeenPbrTable", false,
                              std::nullopt, FirewallFamily::ipv6);
    }
    if (raw_prerouting.ipv4) {
        append_iptables_rules(snapshot, v4_state, "KeenPbrRaw", false,
                              FirewallHook::prerouting, FirewallFamily::ipv4);
        const auto output_state = parse_iptables_s_family(
            v4_output.stdout_output, false, "KeenPbrOutput");
        append_iptables_rules(snapshot, output_state, "KeenPbrOutput", true,
                              FirewallHook::output, FirewallFamily::ipv4);
    }
    if (raw_prerouting.ipv6) {
        append_iptables_rules(snapshot, v6_state, "KeenPbrRaw", false,
                              FirewallHook::prerouting, FirewallFamily::ipv6);
        const auto output_state = parse_iptables_s_family(
            v6_output.stdout_output, true, "KeenPbrOutput");
        append_iptables_rules(snapshot, output_state, "KeenPbrOutput", true,
                              FirewallHook::output, FirewallFamily::ipv6);
    }

    for (const auto& set : parse_ipset_save(sets.stdout_output)) {
        snapshot.sets.push_back({set.name,
                                 set.family == AF_INET6 ? FirewallFamily::ipv6
                                                        : FirewallFamily::ipv4,
                                 set.timeout_seconds,
                                 starts_with(set.name, "kpbr4d_") ||
                                     starts_with(set.name, "kpbr6d_")});
    }
    snapshot.available = true;
    return snapshot;
}

class IptablesSnapshotInspector final : public FirewallSnapshotInspector {
public:
    IptablesSnapshotInspector(CommandRunner runner, RawPreroutingMode mode)
        : runner_(std::move(runner)), mode_(mode) {}

    FirewallSnapshot inspect() const override {
        return inspect_iptables_snapshot_impl(runner_, mode_);
    }

private:
    CommandRunner runner_;
    RawPreroutingMode mode_;
};

class NftablesSnapshotInspector final : public FirewallSnapshotInspector {
public:
    explicit NftablesSnapshotInspector(CommandRunner runner)
        : runner_(std::move(runner)) {}

    FirewallSnapshot inspect() const override {
        FirewallSnapshot snapshot;
        snapshot.backend = FirewallBackend::nftables;
        const auto result = runner_({"nft", "-j", "list", "table", "inet",
                                     "KeenPbrTable"});
        if (result.exit_code != 0 || result.truncated || result.stdout_output.empty()) {
            snapshot.error = "failed to inspect nftables firewall state";
            return snapshot;
        }
        const auto parsed = parse_nft_json(result.stdout_output);
        if (!parsed.has_table) {
            snapshot.error = "KeenPbrTable table not found in nftables";
            return snapshot;
        }
        append_nft_rules(snapshot, parsed);
        for (const auto& set : parsed.sets) {
            snapshot.sets.push_back({set.name,
                                     set.type == "ipv6_addr"
                                         ? FirewallFamily::ipv6
                                         : FirewallFamily::ipv4,
                                     set.timeout_seconds,
                                     starts_with(set.name, "kpbr4d_") ||
                                         starts_with(set.name, "kpbr6d_")});
        }
        snapshot.available = true;
        return snapshot;
    }

private:
    CommandRunner runner_;
};

} // namespace

FirewallSnapshot inspect_iptables_snapshot(const CommandRunner& runner,
                                            RawPreroutingMode raw_prerouting) {
    return inspect_iptables_snapshot_impl(runner, raw_prerouting);
}

FirewallSnapshot inspect_nftables_snapshot(const CommandRunner& runner) {
    return NftablesSnapshotInspector(runner).inspect();
}

FirewallSnapshot inspect_iptables_snapshot(const FirewallCommandRunner& runner,
                                            RawPreroutingMode raw_prerouting) {
    return inspect_iptables_snapshot(
        CommandRunner([runner](const std::vector<std::string>& args) {
            const auto output = runner(args);
            return CommandResult{output, 0, false};
        }), raw_prerouting);
}

FirewallSnapshot inspect_nftables_snapshot(const FirewallCommandRunner& runner) {
    return inspect_nftables_snapshot(CommandRunner([runner](
        const std::vector<std::string>& args) {
            const auto output = runner(args);
            return CommandResult{output, 0, false};
        }));
}

std::unique_ptr<FirewallSnapshotInspector> create_firewall_snapshot_inspector(
    FirewallBackend backend, RawPreroutingMode raw_prerouting, CommandRunner runner) {
    if (backend == FirewallBackend::iptables) {
        return std::make_unique<IptablesSnapshotInspector>(std::move(runner),
                                                            raw_prerouting);
    }
    if (raw_prerouting.ipv4 || raw_prerouting.ipv6) {
        throw FirewallError(
            "RAW PREROUTING is supported only with the iptables firewall backend");
    }
    return std::make_unique<NftablesSnapshotInspector>(std::move(runner));
}

std::vector<FirewallRuleCheck> verify_firewall_plan(
    const FirewallPlan& plan, const FirewallSnapshot& snapshot,
    uint32_t fwmark_mask) {
    (void)fwmark_mask;
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
        std::vector<std::size_t> shape_matches;
        for (const auto& physical_rule : physical) {
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
                shape_matches.push_back(*shape);
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
                const auto mismatch_index = !shape_matches.empty()
                    ? shape_matches.front() : keyed.front();
                const auto physical_index = std::min(
                    matched.size(), physical.size() - std::size_t{1});
                check.detail = mismatch_detail(
                    expected, physical[physical_index],
                    snapshot.rules[mismatch_index], plan.fwmark_mask);
                if (const auto* mark = std::get_if<MarkAction>(
                        &snapshot.rules[mismatch_index].action)) {
                    check.actual_fwmark = mark->value;
                }
            } else {
                check.detail = keyed.empty()
                    ? "rule not found in firewall snapshot"
                    : "owned rule key present but expected physical expansion is missing";
            }
            checks.push_back(std::move(check));
            continue;
        }
        if (prefilter_errors.find(plan_index) != prefilter_errors.end()) {
            check.status = CheckStatus::mismatch;
            check.detail = "prefilter physical order mismatch";
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
                           criteria_equal(matched_rule.criteria, observed.criteria);
                });
            if (!paired_return && !shared_chain_view) duplicate = true;
        }
        if (duplicate) {
            check.status = CheckStatus::mismatch;
            check.detail = "duplicate observed rules for owned key";
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
                ? normalize_set_name(*observed.criteria.dst_set_name) : "<direct>";
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
            ? normalize_set_name(*observed.criteria.dst_set_name) : "<direct>";
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
