#include "nftables_verifier.hpp"

#include "../util/format_compat.hpp"

#include <map>
#include <string>
#include <nlohmann/json.hpp>

namespace keen_pbr3 {

namespace {

std::string format_nft_read_failure(const std::string& subject,
                                    const CommandResult& result) {
    if (result.truncated) {
        return keen_pbr3::format(
            "nft verification output exceeded capture limit while reading {}",
            subject);
    }
    if (result.exit_code == 127) {
        return keen_pbr3::format("nft command not found while reading {}", subject);
    }
    return {};
}

std::optional<std::string> validate_nft_json_root(const std::string& json_output,
                                                  const std::string& subject) {
    if (json_output.empty()) {
        return keen_pbr3::format("nft returned empty JSON while reading {}", subject);
    }

    try {
        const auto j = nlohmann::json::parse(json_output);
        if (!j.contains("nftables") || !j["nftables"].is_array()) {
            return keen_pbr3::format(
                "failed to parse nftables JSON while reading {}", subject);
        }
    } catch (...) {
        return keen_pbr3::format(
            "failed to parse nftables JSON while reading {}", subject);
    }

    return std::nullopt;
}

} // namespace

// ---------------------------------------------------------------------------
// parse_nft_json
// ---------------------------------------------------------------------------

ParsedNftablesState parse_nft_json(const std::string& json_output) {
    ParsedNftablesState state;

    if (json_output.empty()) return state;

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_output);
    } catch (...) {
        // Invalid JSON -> return default empty state
        return state;
    }

    if (!j.contains("nftables") || !j["nftables"].is_array()) {
        return state;
    }

    static constexpr const char* TABLE_NAME = "KeenPbrTable";
    static constexpr const char* CHAIN_NAME = "prerouting";
    static constexpr const char* FAMILY = "inet";

    for (const auto& elem : j["nftables"]) {
        if (!elem.is_object()) continue;

        // --- Table entry ---
        if (elem.contains("table")) {
            const auto& tbl = elem["table"];
            if (tbl.is_object()
                && tbl.value("family", "") == FAMILY
                && tbl.value("name", "") == TABLE_NAME) {
                state.has_table = true;
            }
            continue;
        }

        // --- Chain entry ---
        if (elem.contains("chain")) {
            const auto& ch = elem["chain"];
            if (ch.is_object()
                && ch.value("table", "") == TABLE_NAME
                && ch.value("name", "") == CHAIN_NAME) {
                state.has_prerouting_chain = true;
                if (ch.value("hook", "") == "prerouting") {
                    state.has_prerouting_hook = true;
                }
            }
            continue;
        }

        // --- Rule entry ---
        if (!elem.contains("rule")) continue;
        const auto& rule = elem["rule"];
        if (!rule.is_object()) continue;
        if (rule.value("table", "") != TABLE_NAME
            || rule.value("chain", "") != CHAIN_NAME) {
            continue;
        }

        if (!rule.contains("expr") || !rule["expr"].is_array()) continue;

        ParsedNftRule nr;
        bool has_set_ref = false;

        for (const auto& expr : rule["expr"]) {
            if (!expr.is_object()) continue;

            // Match expression: look for set reference in right
            if (expr.contains("match")) {
                const auto& match = expr["match"];
                if (!match.is_object()) continue;

                if (match.contains("right")) {
                    const auto& right = match["right"];
                    std::string set_ref;

                    if (right.is_string()) {
                        set_ref = right.get<std::string>();
                    } else if (right.is_object() && right.contains("set")) {
                        // Some nftables versions use {"set": "@setname"}
                        const auto& s = right["set"];
                        if (s.is_string()) {
                            set_ref = s.get<std::string>();
                        }
                    }

                    if (!set_ref.empty() && set_ref[0] == '@') {
                        nr.set_name = set_ref.substr(1); // strip '@' prefix
                        has_set_ref = true;
                    }
                }

                // Detect IPv6 from payload protocol in left
                if (match.contains("left") && match["left"].is_object()) {
                    const auto& left = match["left"];
                    if (left.contains("payload") && left["payload"].is_object()) {
                        if (left["payload"].value("protocol", "") == "ip6") {
                            nr.ipv6 = true;
                        }
                    }
                }
                continue;
            }

            // Mangle expression: meta mark set <value>
            if (expr.contains("mangle")) {
                const auto& mangle = expr["mangle"];
                if (!mangle.is_object()) continue;

                bool is_mark_key = false;
                if (mangle.contains("key") && mangle["key"].is_object()) {
                    const auto& key = mangle["key"];
                    if (key.contains("meta") && key["meta"].is_object()) {
                        if (key["meta"].value("key", "") == "mark") {
                            is_mark_key = true;
                        }
                    }
                }

                if (is_mark_key && mangle.contains("value")) {
                    const auto& val = mangle["value"];
                    if (val.is_number_unsigned()) {
                        nr.fwmark = val.get<uint32_t>();
                        nr.is_mark = true;
                    } else if (val.is_number_integer()) {
                        // Negative integer unlikely for fwmark, but handle gracefully
                        nr.fwmark = static_cast<uint32_t>(val.get<int64_t>());
                        nr.is_mark = true;
                    }
                }
                continue;
            }

            // Drop verdict
            if (expr.contains("drop")) {
                nr.is_drop = true;
                continue;
            }
        }

        if (has_set_ref && (nr.is_mark || nr.is_drop)) {
            state.rules.push_back(std::move(nr));
        }
    }

    return state;
}

// ---------------------------------------------------------------------------
// NftablesFirewallVerifier
// ---------------------------------------------------------------------------

NftablesFirewallVerifier::NftablesFirewallVerifier(CommandRunner runner)
    : runner_(std::move(runner)) {}

const NftablesFirewallVerifier::CachedState& NftablesFirewallVerifier::get_state() const {
    if (!cached_state_.has_value()) {
        CachedState cached;

        const auto chain_result = runner_(
            {"nft", "-j", "list", "chain", "inet", TABLE_NAME, CHAIN_NAME});
        if (const auto error = format_nft_read_failure("prerouting chain", chain_result);
            !error.empty()) {
            cached.error = error;
            cached_state_ = std::move(cached);
            return *cached_state_;
        }

        if (chain_result.exit_code != 0) {
            const auto table_result = runner_(
                {"nft", "-j", "-t", "list", "table", "inet", TABLE_NAME});
            if (const auto table_error = format_nft_read_failure("KeenPbrTable table",
                                                                 table_result);
                !table_error.empty()) {
                cached.error = table_error;
                cached_state_ = std::move(cached);
                return *cached_state_;
            }

            if (table_result.exit_code != 0) {
                cached_state_ = std::move(cached);
                return *cached_state_;
            }

            if (const auto parse_error =
                    validate_nft_json_root(table_result.stdout_output, "KeenPbrTable table")) {
                cached.error = *parse_error;
                cached_state_ = std::move(cached);
                return *cached_state_;
            }

            cached.state = parse_nft_json(table_result.stdout_output);
            cached.state.has_table = true;
            cached_state_ = std::move(cached);
            return *cached_state_;
        }

        if (const auto parse_error =
                validate_nft_json_root(chain_result.stdout_output, "prerouting chain")) {
            cached.error = *parse_error;
            cached_state_ = std::move(cached);
            return *cached_state_;
        }

        cached.state = parse_nft_json(chain_result.stdout_output);
        cached.state.has_table = true;
        cached.state.has_prerouting_chain = true;
        cached_state_ = std::move(cached);
    }
    return *cached_state_;
}

FirewallChainCheck NftablesFirewallVerifier::verify_chain() {
    const auto& cached = get_state();
    const auto& state = cached.state;

    FirewallChainCheck result;
    result.chain_present = state.has_prerouting_chain;
    result.prerouting_hook_present = state.has_prerouting_hook;

    if (!cached.error.empty()) {
        result.detail = cached.error;
    } else if (!state.has_table) {
        result.detail = keen_pbr3::format("{} table not found in nftables", TABLE_NAME);
    } else if (!result.chain_present) {
        result.detail = keen_pbr3::format("{} chain not found in {} table",
                                          CHAIN_NAME, TABLE_NAME);
    } else if (!result.prerouting_hook_present) {
        result.detail = keen_pbr3::format("{} chain exists but prerouting hook not configured",
                                          CHAIN_NAME);
    } else {
        result.detail = "ok";
    }

    return result;
}

std::vector<FirewallRuleCheck> NftablesFirewallVerifier::verify_rules(
    const std::vector<RuleState>& expected) {
    const auto& cached = get_state();
    const auto& state = cached.state;

    // Build lookup: set_name -> ParsedNftRule
    std::map<std::string, ParsedNftRule> rule_map;
    for (const auto& r : state.rules) {
        rule_map.emplace(r.set_name, r);
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

            if (!cached.error.empty()) {
                check.status = CheckStatus::missing;
                check.detail = cached.error;
                checks.push_back(std::move(check));
                continue;
            }

            auto it = rule_map.find(set_name);
            if (it == rule_map.end()) {
                check.status = CheckStatus::missing;
                check.detail = "rule not found in nftables prerouting chain";
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

std::unique_ptr<FirewallVerifier> create_nftables_verifier(CommandRunner runner) {
    return std::make_unique<NftablesFirewallVerifier>(std::move(runner));
}

} // namespace keen_pbr3
