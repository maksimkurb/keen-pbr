#include "iptables_verifier.hpp"

#include "../util/format_compat.hpp"

#include <map>
#include <sstream>
#include <string>

namespace keen_pbr3 {

// ---------------------------------------------------------------------------
// parse_iptables_s
// ---------------------------------------------------------------------------

namespace {

std::optional<uint32_t> parse_u32(const std::string& input) {
    try {
        return static_cast<uint32_t>(std::stoul(input, nullptr, 0));
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace

ParsedIptablesState parse_iptables_s(const std::string& output) {
    ParsedIptablesState state;

    static constexpr const char* CHAIN_NAME = "KeenPbrTable";

    // Prefix for chain declaration in `iptables -S` output
    const std::string chain_decl = std::string("-N ") + CHAIN_NAME;
    // Prefix for PREROUTING jump rule
    const std::string prerouting_jump =
        std::string("-A PREROUTING -j ") + CHAIN_NAME;
    // Prefix for any rule in the KeenPbrTable chain. iptables may normalize
    // token ordering, so the set matcher is not guaranteed to be the first
    // matcher after the chain name.
    const std::string chain_rule_prefix =
        std::string("-A ") + CHAIN_NAME + " ";

    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        // Chain declaration: "-N KeenPbrTable"
        if (line == chain_decl) {
            state.has_keen_pbr_chain = true;
            continue;
        }

        // PREROUTING jump: "-A PREROUTING -j KeenPbrTable"
        if (line == prerouting_jump) {
            state.has_prerouting_jump = true;
            continue;
        }

        // Rules in KeenPbrTable chain with a set matcher somewhere in the rule:
        // "-A KeenPbrTable ... -m set --match-set <set> dst ... -j ..."
        if (line.rfind(chain_rule_prefix, 0) == 0) {
            const std::string match_set_token = "--match-set ";
            const size_t match_set_pos = line.find(match_set_token);
            if (match_set_pos == std::string::npos) continue;

            const size_t set_start = match_set_pos + match_set_token.size();
            const size_t set_end = line.find(' ', set_start);
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
            } else if (action_part == "RETURN") {
                rule.is_pass = true;
            } else if (action_part.rfind("MARK --set-mark ", 0) == 0) {
                rule.is_mark = true;
                const std::string mark_str = action_part.substr(16); // len("MARK --set-mark ") == 16
                const auto mark = parse_u32(mark_str);
                if (!mark.has_value()) continue;
                rule.fwmark = *mark;
            } else if (action_part.rfind("MARK --set-xmark ", 0) == 0) {
                const std::string xmark_str = action_part.substr(17); // len("MARK --set-xmark ") == 17
                const size_t slash_pos = xmark_str.find('/');
                if (slash_pos == std::string::npos) continue;

                const auto mark = parse_u32(xmark_str.substr(0, slash_pos));
                const auto mask = parse_u32(xmark_str.substr(slash_pos + 1));
                if (!mark.has_value() || !mask.has_value()) continue;

                rule.is_mark = true;
                rule.fwmark = *mark;
                rule.xmark_mask = *mask;
                rule.mark_is_exact = (*mask == 0xFFFFFFFFu);
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

        auto read_state = [this](const std::vector<std::string>& chain_args,
                                 const std::vector<std::string>& prerouting_args) {
            std::string combined;

            const auto chain_result = runner_(chain_args);
            if (chain_result.exit_code == 0) {
                combined += chain_result.stdout_output;
                if (!combined.empty() && combined.back() != '\n') {
                    combined.push_back('\n');
                }
            }

            const auto prerouting_result = runner_(prerouting_args);
            if (prerouting_result.exit_code == 0) {
                combined += prerouting_result.stdout_output;
            }

            return parse_iptables_s(combined);
        };

        state.v4 = read_state(
            {"iptables", "-t", "mangle", "-S", CHAIN_NAME},
            {"iptables", "-t", "mangle", "-S", "PREROUTING"});
        state.v6 = read_state(
            {"ip6tables", "-t", "mangle", "-S", CHAIN_NAME},
            {"ip6tables", "-t", "mangle", "-S", "PREROUTING"});
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
            } else if (rs.action_type == RuleActionType::Drop) {
                check.action = "drop";
            } else {
                check.action = "pass";
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
                        if (!parsed.mark_is_exact) {
                            check.status = CheckStatus::mismatch;
                            check.detail = keen_pbr3::format(
                                "live rule uses partial xmark mask: got {:#x}/{:#x}, expected exact mark {:#x}",
                                parsed.fwmark, parsed.xmark_mask, rs.fwmark);
                        } else if (parsed.fwmark == rs.fwmark) {
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
                    } else if (parsed.is_pass) {
                        check.status = CheckStatus::mismatch;
                        check.detail = "expected MARK rule but found RETURN rule";
                    } else {
                        check.status = CheckStatus::mismatch;
                        check.detail = "rule action mismatch";
                    }
                } else if (rs.action_type == RuleActionType::Drop) {
                    // Expecting DROP
                    if (parsed.is_drop) {
                        check.status = CheckStatus::ok;
                        check.detail = "ok";
                    } else if (parsed.is_mark) {
                        check.actual_fwmark = parsed.fwmark;
                        check.status = CheckStatus::mismatch;
                        check.detail = "expected DROP rule but found MARK rule";
                    } else if (parsed.is_pass) {
                        check.status = CheckStatus::mismatch;
                        check.detail = "expected DROP rule but found RETURN rule";
                    } else {
                        check.status = CheckStatus::mismatch;
                        check.detail = "rule action mismatch";
                    }
                } else {
                    // Expecting RETURN
                    if (parsed.is_pass) {
                        check.status = CheckStatus::ok;
                        check.detail = "ok";
                    } else if (parsed.is_mark) {
                        check.actual_fwmark = parsed.fwmark;
                        check.status = CheckStatus::mismatch;
                        check.detail = "expected RETURN rule but found MARK rule";
                    } else if (parsed.is_drop) {
                        check.status = CheckStatus::mismatch;
                        check.detail = "expected RETURN rule but found DROP rule";
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
