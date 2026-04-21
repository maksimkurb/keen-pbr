#include "nftables_verifier.hpp"

#include "port_spec_util.hpp"
#include "../util/format_compat.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>
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

std::optional<L4Proto> parse_l4proto(const std::string& token) {
    if (token == "tcp") return L4Proto::Tcp;
    if (token == "udp") return L4Proto::Udp;
    return std::nullopt;
}

PortSpec parse_nft_port_spec(const nlohmann::json& rhs) {
    if (rhs.is_number_integer() || rhs.is_number_unsigned()) {
        return PortSpec(std::to_string(rhs.get<int64_t>()));
    }
    if (rhs.is_object() && rhs.contains("range") && rhs["range"].is_array() &&
        rhs["range"].size() == 2) {
        return PortSpec(keen_pbr3::format("{}-{}", rhs["range"][0].get<int64_t>(),
                                          rhs["range"][1].get<int64_t>()));
    }
    if (rhs.is_object() && rhs.contains("set") && rhs["set"].is_array()) {
        std::string out;
        for (size_t i = 0; i < rhs["set"].size(); ++i) {
            if (i != 0) out += ",";
            out += parse_nft_port_spec(rhs["set"][i]).to_config_string();
        }
        return PortSpec(out);
    }
    return {};
}

std::vector<std::string> parse_nft_addr_list(const nlohmann::json& rhs) {
    auto parse_one = [](const nlohmann::json& item) -> std::optional<std::string> {
        if (item.is_string()) {
            return item.get<std::string>();
        }
        if (item.is_object() && item.contains("prefix") && item["prefix"].is_object()) {
            const auto& prefix = item["prefix"];
            if (!prefix.contains("addr") || !prefix.contains("len")) {
                return std::nullopt;
            }
            const std::string addr = prefix["addr"].get<std::string>();
            const int len = prefix["len"].get<int>();
            if ((addr.find(':') == std::string::npos && len == 32) ||
                (addr.find(':') != std::string::npos && len == 128)) {
                return addr;
            }
            return keen_pbr3::format("{}/{}", addr, len);
        }
        return std::nullopt;
    };

    if (rhs.is_string()) {
        return {rhs.get<std::string>()};
    }
    if (rhs.is_object() && rhs.contains("prefix")) {
        const auto parsed = parse_one(rhs);
        return parsed.has_value() ? std::vector<std::string>{*parsed}
                                  : std::vector<std::string>{};
    }
    if (rhs.is_object() && rhs.contains("set") && rhs["set"].is_array()) {
        std::vector<std::string> out;
        for (const auto& item : rhs["set"]) {
            const auto parsed = parse_one(item);
            if (parsed.has_value()) {
                out.push_back(*parsed);
            }
        }
        return out;
    }
    return {};
}

bool criteria_equal(const FirewallRuleCriteria& lhs,
                    const FirewallRuleCriteria& rhs) {
    return lhs.proto == rhs.proto &&
           lhs.src_port == rhs.src_port &&
           lhs.dst_port == rhs.dst_port &&
           lhs.src_addr == rhs.src_addr &&
           lhs.dst_addr == rhs.dst_addr &&
           lhs.negate_src_port == rhs.negate_src_port &&
           lhs.negate_dst_port == rhs.negate_dst_port &&
           lhs.negate_src_addr == rhs.negate_src_addr &&
           lhs.negate_dst_addr == rhs.negate_dst_addr;
}

std::string criteria_summary(const FirewallRuleCriteria& criteria) {
    std::vector<std::string> parts;

    auto append_addr = [&parts](const char* label,
                                const std::vector<std::string>& addrs,
                                bool negated) {
        if (addrs.empty()) return;
        std::string value = addrs.front();
        for (size_t i = 1; i < addrs.size(); ++i) {
            value += ",";
            value += addrs[i];
        }
        parts.push_back(keen_pbr3::format("{}={}{}", label, negated ? "!" : "", value));
    };

    auto append_port = [&parts](const char* label,
                                const PortSpec& spec,
                                bool negated) {
        if (spec.empty()) return;
        parts.push_back(keen_pbr3::format("{}={}{}", label, negated ? "!" : "",
                                          spec.to_config_string()));
    };

    if (criteria.proto != L4Proto::Any) {
        parts.push_back(keen_pbr3::format("proto={}", l4_proto_name(criteria.proto)));
    }
    append_port("sport", criteria.src_port, criteria.negate_src_port);
    append_port("dport", criteria.dst_port, criteria.negate_dst_port);
    append_addr("src", criteria.src_addr, criteria.negate_src_addr);
    append_addr("dst", criteria.dst_addr, criteria.negate_dst_addr);

    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) out += " ";
        out += parts[i];
    }
    return out.empty() ? "any" : out;
}

std::vector<std::string> filter_addrs_by_family(const std::vector<std::string>& addrs,
                                                bool ipv6) {
    std::vector<std::string> filtered;
    for (const auto& addr : addrs) {
        if ((addr.find(':') != std::string::npos) == ipv6) {
            filtered.push_back(addr);
        }
    }
    return filtered;
}

bool needs_family_specific_rule(const FirewallRuleCriteria& criteria) {
    return criteria.dst_set_name.has_value() ||
           !criteria.src_addr.empty() ||
           !criteria.dst_addr.empty();
}

std::vector<L4Proto> expand_l4_protos(const FirewallRuleCriteria& criteria) {
    if (criteria.proto == L4Proto::TcpUdp) {
        return {L4Proto::Tcp, L4Proto::Udp};
    }
    return {criteria.proto};
}

std::optional<bool> ipv6_from_set_name(const std::string& set_name) {
    if (set_name.rfind("kpbr6_", 0) == 0 || set_name.rfind("kpbr6d_", 0) == 0) {
        return true;
    }
    if (set_name.rfind("kpbr4_", 0) == 0 || set_name.rfind("kpbr4d_", 0) == 0) {
        return false;
    }
    return std::nullopt;
}

struct ExpectedNftRule {
    std::string set_name;
    bool ipv6{false};
    RuleActionType action_type{RuleActionType::Skip};
    uint32_t fwmark{0};
    FirewallRuleCriteria criteria;
};

std::vector<ExpectedNftRule> expand_expected_rule_states(
    const std::vector<RuleState>& expected) {
    std::vector<ExpectedNftRule> expanded;

    for (const auto& rs : expected) {
        if (rs.action_type == RuleActionType::Skip) continue;

        std::vector<std::pair<std::string, bool>> targets;
        if (!rs.set_names.empty()) {
            for (const auto& set_name : rs.set_names) {
                targets.push_back({set_name, ipv6_from_set_name(set_name).value_or(false)});
            }
        } else if (rs.criteria.has_rule_selector()) {
            if (!needs_family_specific_rule(rs.criteria)) {
                targets.push_back({"", false});
            } else {
                targets.push_back({"", false});
                targets.push_back({"", true});
            }
        } else {
            continue;
        }

        for (const auto& [set_name, ipv6] : targets) {
            const auto filtered_src = rs.criteria.src_addr.empty()
                ? std::vector<std::string>{}
                : filter_addrs_by_family(rs.criteria.src_addr, ipv6);
            const auto filtered_dst = rs.criteria.dst_addr.empty()
                ? std::vector<std::string>{}
                : filter_addrs_by_family(rs.criteria.dst_addr, ipv6);

            if ((!rs.criteria.src_addr.empty() && filtered_src.empty()) ||
                (!rs.criteria.dst_addr.empty() && filtered_dst.empty())) {
                continue;
            }

            for (const auto proto : expand_l4_protos(rs.criteria)) {
                ExpectedNftRule exp;
                exp.set_name = set_name;
                exp.ipv6 = ipv6;
                exp.action_type = rs.action_type;
                exp.fwmark = rs.fwmark;
                exp.criteria = rs.criteria;
                exp.criteria.proto = proto;
                if (!rs.criteria.src_addr.empty()) exp.criteria.src_addr = filtered_src;
                if (!rs.criteria.dst_addr.empty()) exp.criteria.dst_addr = filtered_dst;
                expanded.push_back(std::move(exp));
            }
        }
    }

    return expanded;
}

bool action_matches(const ParsedNftRule& actual,
                    const ExpectedNftRule& expected) {
    if (expected.action_type == RuleActionType::Mark) {
        return actual.is_mark && actual.fwmark == expected.fwmark;
    }
    if (expected.action_type == RuleActionType::Drop) {
        return actual.is_drop;
    }
    return actual.is_pass;
}

bool rule_matches(const ParsedNftRule& actual,
                  const ExpectedNftRule& expected) {
    return actual.ipv6 == expected.ipv6 &&
           actual.set_name == expected.set_name &&
           action_matches(actual, expected) &&
           criteria_equal(actual.criteria, expected.criteria);
}

} // namespace

ParsedNftablesState parse_nft_json(const std::string& json_output) {
    ParsedNftablesState state;
    if (json_output.empty()) return state;

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_output);
    } catch (...) {
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

        if (elem.contains("table")) {
            const auto& tbl = elem["table"];
            if (tbl.is_object() &&
                tbl.value("family", "") == FAMILY &&
                tbl.value("name", "") == TABLE_NAME) {
                state.has_table = true;
            }
            continue;
        }

        if (elem.contains("chain")) {
            const auto& ch = elem["chain"];
            if (ch.is_object() &&
                ch.value("table", "") == TABLE_NAME &&
                ch.value("name", "") == CHAIN_NAME) {
                state.has_prerouting_chain = true;
                if (ch.value("hook", "") == "prerouting") {
                    state.has_prerouting_hook = true;
                }
            }
            continue;
        }

        if (!elem.contains("rule")) continue;
        const auto& rule = elem["rule"];
        if (!rule.is_object()) continue;
        if (rule.value("table", "") != TABLE_NAME ||
            rule.value("chain", "") != CHAIN_NAME ||
            !rule.contains("expr") || !rule["expr"].is_array()) {
            continue;
        }

        ParsedNftRule nr;

        for (const auto& expr : rule["expr"]) {
            if (!expr.is_object()) continue;

            if (expr.contains("match")) {
                const auto& match = expr["match"];
                if (!match.is_object()) continue;

                const std::string op = match.value("op", "==");
                if (match.contains("left") && match["left"].is_object()) {
                    const auto& left = match["left"];
                    if (left.contains("payload") && left["payload"].is_object()) {
                        const auto& payload = left["payload"];
                        const std::string protocol = payload.value("protocol", "");
                        const std::string field = payload.value("field", "");

                        if (protocol == "ip6") nr.ipv6 = true;

                        if (field == "saddr" && match.contains("right")) {
                            nr.criteria.src_addr = parse_nft_addr_list(match["right"]);
                            nr.criteria.negate_src_addr = (op == "!=");
                        } else if (field == "daddr" && match.contains("right")) {
                            const auto& right = match["right"];
                            std::string set_ref;
                            if (right.is_string()) {
                                set_ref = right.get<std::string>();
                            } else if (right.is_object() && right.contains("set") &&
                                       right["set"].is_string()) {
                                set_ref = right["set"].get<std::string>();
                            }

                            if (!set_ref.empty() && set_ref[0] == '@') {
                                nr.set_name = set_ref.substr(1);
                            } else {
                                nr.criteria.dst_addr = parse_nft_addr_list(right);
                                nr.criteria.negate_dst_addr = (op == "!=");
                            }
                        } else if (field == "sport" && match.contains("right")) {
                            if (protocol == "tcp") nr.criteria.proto = L4Proto::Tcp;
                            if (protocol == "udp") nr.criteria.proto = L4Proto::Udp;
                            nr.criteria.src_port = parse_nft_port_spec(match["right"]);
                            nr.criteria.negate_src_port = (op == "!=");
                        } else if (field == "dport" && match.contains("right")) {
                            if (protocol == "tcp") nr.criteria.proto = L4Proto::Tcp;
                            if (protocol == "udp") nr.criteria.proto = L4Proto::Udp;
                            nr.criteria.dst_port = parse_nft_port_spec(match["right"]);
                            nr.criteria.negate_dst_port = (op == "!=");
                        }
                    } else if (left.contains("meta") && left["meta"].is_object() &&
                               left["meta"].value("key", "") == "l4proto" &&
                               match.contains("right") && match["right"].is_string()) {
                        nr.criteria.proto =
                            parse_l4proto(match["right"].get<std::string>()).value_or(L4Proto::Any);
                    }
                }
                continue;
            }

            if (expr.contains("mangle")) {
                const auto& mangle = expr["mangle"];
                if (mangle.is_object() &&
                    mangle.contains("key") && mangle["key"].is_object() &&
                    mangle["key"].contains("meta") &&
                    mangle["key"]["meta"].is_object() &&
                    mangle["key"]["meta"].value("key", "") == "mark" &&
                    mangle.contains("value")) {
                    nr.is_mark = true;
                    nr.fwmark = static_cast<uint32_t>(mangle["value"].get<int64_t>());
                }
                continue;
            }

            if (expr.contains("drop")) {
                nr.is_drop = true;
                continue;
            }
            if (expr.contains("accept") || expr.contains("return")) {
                nr.is_pass = true;
                continue;
            }
        }

        if ((!nr.is_mark && !nr.is_drop && !nr.is_pass) ||
            (nr.set_name.empty() && nr.criteria.empty())) {
            continue;
        }

        state.rules.push_back(std::move(nr));
    }

    return state;
}

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

    std::vector<FirewallRuleCheck> checks;
    if (!cached.error.empty()) {
        for (const auto& exp : expand_expected_rule_states(expected)) {
            FirewallRuleCheck check;
            check.set_name = exp.set_name.empty() ? "<direct>" : exp.set_name;
            check.action = exp.action_type == RuleActionType::Mark
                ? "mark"
                : (exp.action_type == RuleActionType::Drop ? "drop" : "pass");
            if (exp.action_type == RuleActionType::Mark) {
                check.expected_fwmark = exp.fwmark;
            }
            check.status = CheckStatus::missing;
            check.detail = cached.error;
            checks.push_back(std::move(check));
        }
        return checks;
    }

    std::vector<bool> used(state.rules.size(), false);

    for (const auto& exp : expand_expected_rule_states(expected)) {
        FirewallRuleCheck check;
        check.set_name = exp.set_name.empty() ? "<direct>" : exp.set_name;
        check.action = exp.action_type == RuleActionType::Mark
            ? "mark"
            : (exp.action_type == RuleActionType::Drop ? "drop" : "pass");
        if (exp.action_type == RuleActionType::Mark) {
            check.expected_fwmark = exp.fwmark;
        }

        auto it = std::find_if(state.rules.begin(), state.rules.end(),
                               [&](const ParsedNftRule& actual) {
                                   const size_t index =
                                       static_cast<size_t>(&actual - state.rules.data());
                                   return !used[index] && rule_matches(actual, exp);
                               });

        if (it != state.rules.end()) {
            const size_t index = static_cast<size_t>(it - state.rules.begin());
            used[index] = true;
            if (it->is_mark) {
                check.actual_fwmark = it->fwmark;
            }
            check.status = CheckStatus::ok;
            check.detail = "ok";
            checks.push_back(std::move(check));
            continue;
        }

        auto same_shape = std::find_if(state.rules.begin(), state.rules.end(),
                                       [&](const ParsedNftRule& actual) {
                                           const size_t index =
                                               static_cast<size_t>(&actual - state.rules.data());
                                           return !used[index] &&
                                                  actual.ipv6 == exp.ipv6 &&
                                                  actual.set_name == exp.set_name &&
                                                  criteria_equal(actual.criteria, exp.criteria);
                                       });

        if (same_shape == state.rules.end()) {
            check.status = CheckStatus::missing;
            check.detail = keen_pbr3::format(
                "rule not found in nftables prerouting chain (family={} criteria={})",
                exp.ipv6 ? "ipv6" : "ipv4",
                criteria_summary(exp.criteria));
            checks.push_back(std::move(check));
            continue;
        }

        if (same_shape->is_mark) {
            check.actual_fwmark = same_shape->fwmark;
        }

        if (exp.action_type == RuleActionType::Mark) {
            if (same_shape->is_mark) {
                check.status = CheckStatus::mismatch;
                check.detail = keen_pbr3::format(
                    "fwmark mismatch: expected {:#x} got {:#x}",
                    exp.fwmark, same_shape->fwmark);
            } else if (same_shape->is_drop) {
                check.status = CheckStatus::mismatch;
                check.detail = "expected MARK rule but found DROP rule";
            } else {
                check.status = CheckStatus::mismatch;
                check.detail = "expected MARK rule but found ACCEPT rule";
            }
        } else if (exp.action_type == RuleActionType::Drop) {
            check.status = CheckStatus::mismatch;
            check.detail = same_shape->is_mark
                ? "expected DROP rule but found MARK rule"
                : "expected DROP rule but found ACCEPT rule";
        } else {
            check.status = CheckStatus::mismatch;
            check.detail = same_shape->is_mark
                ? "expected ACCEPT rule but found MARK rule"
                : "expected ACCEPT rule but found DROP rule";
        }

        checks.push_back(std::move(check));
    }

    return checks;
}

std::unique_ptr<FirewallVerifier> create_nftables_verifier(CommandRunner runner) {
    return std::make_unique<NftablesFirewallVerifier>(std::move(runner));
}

} // namespace keen_pbr3
