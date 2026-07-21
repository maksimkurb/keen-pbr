#include "firewall_reconciler.hpp"

#include "iptables_verifier.hpp"
#include "nftables_verifier.hpp"

#include <exception>
#include <algorithm>
#include <netinet/in.h>
#include <sstream>

namespace keen_pbr3 {

namespace {

std::vector<std::string> missing_from(const std::vector<std::string>& expected,
                                      const std::vector<std::string>& actual) {
    std::vector<std::string> result;
    for (const auto& value : expected) {
        if (std::find(actual.begin(), actual.end(), value) == actual.end()) result.push_back(value);
    }
    return result;
}

std::vector<std::string> owned_extra_from(const std::vector<std::string>& actual,
                                          const std::vector<std::string>& expected) {
    std::vector<std::string> result;
    for (const auto& value : actual) {
        if (is_keen_pbr_namespace_name(value) &&
            std::find(expected.begin(), expected.end(), value) == expected.end()) {
            result.push_back(value);
        }
    }
    return result;
}

const FirewallSetSnapshot* find_set(const std::vector<FirewallSetSnapshot>& sets,
                                    const std::string& name) {
    const auto it = std::find_if(sets.begin(), sets.end(), [&](const FirewallSetSnapshot& set) {
        return set.name == name;
    });
    return it == sets.end() ? nullptr : &*it;
}

template <typename Rule>
std::string rule_identity(const Rule& rule) {
    std::ostringstream output;
    output << (rule.ipv6 ? "ip6:" : "ip4:") << rule.set_name << ':';
    if (rule.is_mark) output << "mark=" << rule.fwmark;
    else if (rule.is_drop) output << "drop";
    else if (rule.is_pass) output << "pass";
    else output << "unknown";
    return output.str();
}

} // namespace

bool is_keen_pbr_namespace_name(const std::string& name) {
    const auto separator = name.find(':');
    const std::string object = separator == std::string::npos ? name : name.substr(separator + 1);
    return object == "KeenPbrTable" || object.rfind("KeenPbrTable_", 0) == 0 ||
           object.rfind("PREROUTING->KeenPbrTable", 0) == 0 ||
           object.rfind("OUTPUT->KeenPbrTable", 0) == 0 ||
           object.rfind("kpbr4_", 0) == 0 || object.rfind("kpbr6_", 0) == 0 ||
           object.rfind("kpbr4s_", 0) == 0 || object.rfind("kpbr6s_", 0) == 0 ||
           object.rfind("kpbr4S_", 0) == 0 || object.rfind("kpbr6S_", 0) == 0 ||
           object.rfind("kpbr4d_", 0) == 0 || object.rfind("kpbr6d_", 0) == 0;
}

bool FirewallStateDiff::empty() const {
    return missing_chains.empty() && extra_chains.empty() &&
           missing_jumps.empty() && extra_jumps.empty() &&
           missing_sets.empty() && extra_sets.empty() &&
           schema_mismatches.empty() && !rules_reordered;
}

std::string FirewallStateDiff::summary() const {
    std::vector<std::string> issues;
    if (!missing_chains.empty()) issues.push_back("missing chain");
    if (!extra_chains.empty()) issues.push_back("extra chain");
    if (!missing_jumps.empty()) issues.push_back("missing jump");
    if (!extra_jumps.empty()) issues.push_back("extra jump");
    if (!missing_sets.empty()) issues.push_back("missing set");
    if (!extra_sets.empty()) issues.push_back("extra set");
    if (!schema_mismatches.empty()) issues.push_back("set schema mismatch");
    if (rules_reordered) issues.push_back("rule order mismatch");
    std::ostringstream output;
    for (size_t index = 0; index < issues.size(); ++index) {
        if (index != 0) output << "; ";
        output << issues[index];
    }
    return output.str();
}

FirewallStateDiff diff_firewall_state(const FirewallDesiredState& desired,
                                      const FirewallActualState& actual) {
    FirewallStateDiff diff;
    diff.missing_chains = missing_from(desired.chains, actual.chains);
    diff.extra_chains = owned_extra_from(actual.chains, desired.chains);
    diff.missing_jumps = missing_from(desired.jumps, actual.jumps);
    diff.extra_jumps = owned_extra_from(actual.jumps, desired.jumps);
    diff.rules_reordered = desired.ordered_rules != actual.ordered_rules;

    for (const auto& expected : desired.sets) {
        const auto* observed = find_set(actual.sets, expected.name);
        if (!observed) {
            diff.missing_sets.push_back(expected.name);
        } else if (observed->family != expected.family ||
                   observed->timeout_seconds != expected.timeout_seconds ||
                   observed->dynamic != expected.dynamic) {
            diff.schema_mismatches.push_back(expected.name);
        }
    }
    for (const auto& observed : actual.sets) {
        if (!find_set(desired.sets, observed.name) && is_keen_pbr_namespace_name(observed.name)) {
            diff.extra_sets.push_back(observed.name);
        }
    }
    return diff;
}

FirewallActualState inspect_iptables_state(const ParsedIptablesState& ipv4,
                                           const ParsedIptablesState& ipv6,
                                           const std::vector<ParsedIpset>& sets) {
    FirewallActualState state;
    state.available = true;
    if (ipv4.has_keen_pbr_chain) state.chains.push_back("ip4:KeenPbrTable");
    if (ipv6.has_keen_pbr_chain) state.chains.push_back("ip6:KeenPbrTable");
    if (ipv4.has_prerouting_jump) state.jumps.push_back("ip4:PREROUTING->KeenPbrTable");
    if (ipv6.has_prerouting_jump) state.jumps.push_back("ip6:PREROUTING->KeenPbrTable");
    for (const auto& rule : ipv4.rules) state.ordered_rules.push_back(rule_identity(rule));
    for (const auto& rule : ipv6.rules) state.ordered_rules.push_back(rule_identity(rule));
    for (const auto& set : sets) {
        state.sets.push_back({set.name, set.family, set.timeout_seconds,
                              set.timeout_seconds != 0});
    }
    return state;
}

FirewallActualState inspect_nftables_state(const ParsedNftablesState& parsed) {
    FirewallActualState state;
    state.available = parsed.has_table;
    if (parsed.has_table) state.chains.push_back("inet:KeenPbrTable");
    if (parsed.has_prerouting_chain) state.chains.push_back("inet:prerouting");
    if (parsed.has_prerouting_hook) state.jumps.push_back("inet:prerouting-hook");
    for (const auto& rule : parsed.rules) state.ordered_rules.push_back(rule_identity(rule));
    for (const auto& set : parsed.sets) {
        const int family = set.type == "ipv6_addr" ? AF_INET6 : AF_INET;
        state.sets.push_back({set.name, family, set.timeout_seconds, set.timeout_seconds != 0});
    }
    return state;
}

FirewallActualState inspect_iptables_live(const FirewallCommandRunner& run) {
    return inspect_iptables_state(
        parse_iptables_s(run({"iptables", "-t", "mangle", "-S"})),
        parse_iptables_s(run({"ip6tables", "-t", "mangle", "-S"})),
        parse_ipset_save(run({"ipset", "save"})));
}

FirewallActualState inspect_nftables_live(const FirewallCommandRunner& run) {
    return inspect_nftables_state(
        parse_nft_json(run({"nft", "-j", "list", "table", "inet", "KeenPbrTable"})));
}

FirewallReconcileResult FirewallReconciler::reconcile(const FirewallDesiredState& desired) {
    try {
        std::string probe_error;
        if (!backend_.probe(probe_error)) {
            FirewallReconcileResult result;
            result.error = probe_error.empty() ? "firewall backend is unavailable" : probe_error;
            return result;
        }

        const FirewallActualState actual = backend_.inspect();
        std::vector<FirewallOperation> plan = backend_.plan(desired, actual);
        for (const auto& operation : plan) {
            operation.apply();
        }

        std::string verification_error;
        if (!backend_.verify(desired, backend_.inspect(), verification_error)) {
            FirewallReconcileResult result;
            result.drift_detected = true;
            result.error = verification_error.empty() ? "firewall state drift detected"
                                                       : verification_error;
            result.operation_count = plan.size();
            return result;
        }
        FirewallReconcileResult result;
        result.committed = true;
        result.operation_count = plan.size();
        return result;
    } catch (const std::exception& error) {
        FirewallReconcileResult result;
        result.error = error.what();
        return result;
    } catch (...) {
        FirewallReconcileResult result;
        result.error = "unknown firewall reconciliation error";
        return result;
    }
}

} // namespace keen_pbr3
