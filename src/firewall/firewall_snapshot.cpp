#include "firewall_snapshot.hpp"

#include "iptables_verifier.hpp"
#include "nftables_verifier.hpp"
#include "port_spec_util.hpp"

#include <algorithm>
#include <netinet/in.h>
#include <utility>

namespace keen_pbr3 {
namespace {

bool starts_with(const std::string& value, const char* prefix) {
    return value.rfind(prefix, 0) == 0;
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
} // namespace keen_pbr3
