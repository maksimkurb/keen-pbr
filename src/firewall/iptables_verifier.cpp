#include "iptables_verifier.hpp"

#include "../util/format_compat.hpp"

#include <map>
#include <sstream>
#include <string>

namespace keen_pbr3 {

// ---------------------------------------------------------------------------
// parse_iptables_save
// ---------------------------------------------------------------------------

ParsedIptablesState parse_iptables_save(const std::string& output, bool /*ipv6*/) {
    ParsedIptablesState state;

    static constexpr const char* CHAIN_NAME = "KeenPbrTable";

    // Prefix for chain declaration in iptables-save output
    const std::string chain_decl = std::string(":") + CHAIN_NAME;
    // Prefix for PREROUTING jump rule
    const std::string prerouting_jump =
        std::string("-A PREROUTING -j ") + CHAIN_NAME;
    // Prefix for rules in the KeenPbrTable chain
    const std::string chain_rule_prefix =
        std::string("-A ") + CHAIN_NAME + " -m set --match-set ";

    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        // Chain declaration: ":KeenPbrTable - [0:0]"
        if (line.rfind(chain_decl, 0) == 0 &&
            (line.size() == chain_decl.size() || line[chain_decl.size()] == ' ')) {
            state.has_keen_pbr_chain = true;
            continue;
        }

        // PREROUTING jump: "-A PREROUTING -j KeenPbrTable"
        if (line == prerouting_jump) {
            state.has_prerouting_jump = true;
            continue;
        }

        // Rules in KeenPbrTable chain: "-A KeenPbrTable -m set --match-set <set> dst -j ..."
        if (line.rfind(chain_rule_prefix, 0) == 0) {
            size_t set_start = chain_rule_prefix.size();
            size_t set_end = line.find(' ', set_start);
            if (set_end == std::string::npos) continue;

            std::string set_name = line.substr(set_start, set_end - set_start);

            // Find " -j " token
            size_t j_pos = line.find(" -j ", set_end);
            if (j_pos == std::string::npos) continue;

            std::string action_part = line.substr(j_pos + 4); // skip " -j "

            ParsedIptablesRule rule;
            rule.set_name = set_name;

            if (action_part == "DROP") {
                rule.is_drop = true;
            } else if (action_part.rfind("MARK --set-mark ", 0) == 0) {
                rule.is_mark = true;
                std::string mark_str = action_part.substr(16); // len("MARK --set-mark ") == 16
                try {
                    rule.fwmark = static_cast<uint32_t>(std::stoul(mark_str, nullptr, 0));
                } catch (...) {
                    continue;
                }
            } else {
                continue; // unknown action
            }

            state.rules.push_back(std::move(rule));
        }
    }

    return state;
}

// ---------------------------------------------------------------------------
// IptablesFirewallVerifier
// ---------------------------------------------------------------------------

IptablesFirewallVerifier::IptablesFirewallVerifier(CommandRunner runner)
    : runner_(std::move(runner)) {}

const IptablesFirewallVerifier::CachedState& IptablesFirewallVerifier::get_state() const {
    if (!cached_state_.has_value()) {
        CachedState state;
        state.v4 = parse_iptables_save(
            runner_({"iptables-save", "-t", "mangle"}).stdout_output, false);
        state.v6 = parse_iptables_save(
            runner_({"ip6tables-save", "-t", "mangle"}).stdout_output, true);
        cached_state_ = std::move(state);
    }
    return *cached_state_;
}

FirewallChainCheck IptablesFirewallVerifier::verify_chain() {
    const auto& [v4, v6] = get_state();

    FirewallChainCheck result;
    result.chain_present = v4.has_keen_pbr_chain || v6.has_keen_pbr_chain;
    result.prerouting_hook_present =
        v4.has_prerouting_jump || v6.has_prerouting_jump;

    if (!result.chain_present) {
        result.detail = keen_pbr3::format(
            "{} chain not found in iptables or ip6tables mangle table", CHAIN_NAME);
    } else if (!result.prerouting_hook_present) {
        result.detail = keen_pbr3::format(
            "{} chain exists but PREROUTING jump not found", CHAIN_NAME);
    } else {
        result.detail = "ok";
    }

    return result;
}

std::vector<FirewallRuleCheck> IptablesFirewallVerifier::verify_rules(
    const std::vector<RuleState>& expected) {
    const auto& [v4, v6] = get_state();

    // Build a combined lookup: set_name -> ParsedIptablesRule
    // v4 entries are inserted first; v6 entries only added if set_name not already present
    // (in practice both v4 and v6 rules share the same set_name and fwmark)
    std::map<std::string, ParsedIptablesRule> rule_map;
    for (const auto& r : v4.rules) {
        rule_map.emplace(r.set_name, r);
    }
    for (const auto& r : v6.rules) {
        rule_map.emplace(r.set_name, r); // no-op if already present from v4
    }

    std::vector<FirewallRuleCheck> checks;

    for (const auto& rs : expected) {
        if (rs.action_type == RuleActionType::Skip) {
            continue;
        }

        for (const auto& set_name : rs.set_names) {
            FirewallRuleCheck check;
            check.set_name = set_name;

            if (rs.action_type == RuleActionType::Mark) {
                check.action = "mark";
                check.expected_fwmark = rs.fwmark;
            } else {
                check.action = "drop";
            }

            auto it = rule_map.find(set_name);
            if (it == rule_map.end()) {
                check.status = CheckStatus::missing;
                check.detail = "rule not found in iptables mangle table";
            } else {
                const auto& parsed = it->second;
                if (rs.action_type == RuleActionType::Mark) {
                    if (parsed.is_mark) {
                        check.actual_fwmark = parsed.fwmark;
                        if (parsed.fwmark == rs.fwmark) {
                            check.status = CheckStatus::ok;
                            check.detail = "ok";
                        } else {
                            check.status = CheckStatus::mismatch;
                            check.detail = keen_pbr3::format(
                                "fwmark mismatch: expected {:#x} got {:#x}",
                                rs.fwmark, parsed.fwmark);
                        }
                    } else if (parsed.is_drop) {
                        check.status = CheckStatus::mismatch;
                        check.detail = "expected MARK rule but found DROP rule";
                    } else {
                        check.status = CheckStatus::mismatch;
                        check.detail = "rule action mismatch";
                    }
                } else {
                    // Expecting DROP
                    if (parsed.is_drop) {
                        check.status = CheckStatus::ok;
                        check.detail = "ok";
                    } else if (parsed.is_mark) {
                        check.actual_fwmark = parsed.fwmark;
                        check.status = CheckStatus::mismatch;
                        check.detail = "expected DROP rule but found MARK rule";
                    } else {
                        check.status = CheckStatus::mismatch;
                        check.detail = "rule action mismatch";
                    }
                }
            }

            checks.push_back(std::move(check));
        }
    }

    return checks;
}

std::unique_ptr<FirewallVerifier> create_iptables_verifier(CommandRunner runner) {
    return std::make_unique<IptablesFirewallVerifier>(std::move(runner));
}

} // namespace keen_pbr3
