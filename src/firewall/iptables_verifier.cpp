#include "iptables_verifier.hpp"

#include "port_spec_util.hpp"
#include "../util/format_compat.hpp"

#include <arpa/inet.h>
#include <algorithm>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace keen_pbr3 {

namespace {

std::optional<uint32_t> parse_u32(const std::string& input) {
    try {
        return static_cast<uint32_t>(std::stoul(input, nullptr, 0));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<L4Proto> parse_proto_token(const std::string& token) {
    if (token == "tcp") return L4Proto::Tcp;
    if (token == "udp") return L4Proto::Udp;
    return std::nullopt;
}

std::vector<std::string> split_ws(const std::string& line) {
    std::istringstream stream(line);
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

bool is_ipv6_addr(const std::string& addr) {
    return addr.find(':') != std::string::npos;
}

std::vector<std::string> filter_addrs_by_family(const std::vector<std::string>& addrs,
                                                bool ipv6) {
    std::vector<std::string> filtered;
    for (const auto& addr : addrs) {
        if (is_ipv6_addr(addr) == ipv6) {
            filtered.push_back(addr);
        }
    }
    return filtered;
}

std::vector<L4Proto> expand_l4_protos_for_iptables(
    const FirewallRuleCriteria& criteria) {
    if (criteria.proto == L4Proto::TcpUdp) {
        return {L4Proto::Tcp, L4Proto::Udp};
    }
    if (criteria.proto == L4Proto::Any &&
        (!criteria.src_port.empty() || !criteria.dst_port.empty())) {
        return {L4Proto::Tcp, L4Proto::Udp};
    }
    return {criteria.proto};
}

std::string normalize_iptables_port_spec(const std::string& spec) {
    if (spec.empty()) return {};
    return parse_port_spec(spec).to_iptables_string();
}

std::string normalize_addr_value(const std::string& addr) {
    const auto slash = addr.find('/');
    if (slash == std::string::npos) {
        return addr;
    }

    const std::string base = addr.substr(0, slash);
    const std::string prefix = addr.substr(slash + 1);
    if ((base.find(':') == std::string::npos && prefix == "32") ||
        (base.find(':') != std::string::npos && prefix == "128")) {
        return base;
    }
    return addr;
}

std::vector<std::string> normalize_addr_list(const std::vector<std::string>& addrs) {
    std::vector<std::string> out;
    out.reserve(addrs.size());
    for (const auto& addr : addrs) {
        out.push_back(normalize_addr_value(addr));
    }
    return out;
}

bool criteria_equal(const FirewallRuleCriteria& lhs,
                    const FirewallRuleCriteria& rhs) {
    return lhs.proto == rhs.proto &&
           lhs.src_port.to_iptables_string() == rhs.src_port.to_iptables_string() &&
           lhs.dst_port.to_iptables_string() == rhs.dst_port.to_iptables_string() &&
           normalize_addr_list(lhs.src_addr) == normalize_addr_list(rhs.src_addr) &&
           normalize_addr_list(lhs.dst_addr) == normalize_addr_list(rhs.dst_addr) &&
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
                                          spec.to_iptables_string()));
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

std::optional<bool> ipv6_from_set_name(const std::string& set_name) {
    if (set_name.rfind("kpbr6_", 0) == 0 || set_name.rfind("kpbr6d_", 0) == 0) {
        return true;
    }
    if (set_name.rfind("kpbr4_", 0) == 0 || set_name.rfind("kpbr4d_", 0) == 0) {
        return false;
    }
    return std::nullopt;
}

struct ExpectedIptablesRule {
    std::string set_name;
    bool ipv6{false};
    RuleActionType action_type{RuleActionType::Skip};
    uint32_t fwmark{0};
    FirewallRuleCriteria criteria;
};

std::vector<ExpectedIptablesRule> expand_expected_rule_states(
    const std::vector<RuleState>& expected) {
    std::vector<ExpectedIptablesRule> expanded;
    const std::vector<std::string> any_addr{""};

    for (const auto& rs : expected) {
        if (rs.action_type == RuleActionType::Skip) continue;

        std::vector<std::pair<std::string, bool>> targets;
        if (!rs.set_names.empty()) {
            for (const auto& set_name : rs.set_names) {
                const auto ipv6 = ipv6_from_set_name(set_name).value_or(false);
                targets.push_back({set_name, ipv6});
            }
        } else if (rs.criteria.has_rule_selector()) {
            targets.push_back({"", false});
            targets.push_back({"", true});
        } else {
            continue;
        }

        for (const auto& [set_name, ipv6] : targets) {
            const auto filtered_src = rs.criteria.src_addr.empty()
                ? any_addr
                : filter_addrs_by_family(rs.criteria.src_addr, ipv6);
            const auto filtered_dst = rs.criteria.dst_addr.empty()
                ? any_addr
                : filter_addrs_by_family(rs.criteria.dst_addr, ipv6);

            if ((!rs.criteria.src_addr.empty() && filtered_src.empty()) ||
                (!rs.criteria.dst_addr.empty() && filtered_dst.empty())) {
                continue;
            }

            for (const auto proto : expand_l4_protos_for_iptables(rs.criteria)) {
                for (const auto& src : filtered_src) {
                    for (const auto& dst : filtered_dst) {
                        ExpectedIptablesRule exp;
                        exp.set_name = set_name;
                        exp.ipv6 = ipv6;
                        exp.action_type = rs.action_type;
                        exp.fwmark = rs.fwmark;
                        exp.criteria = rs.criteria;
                        exp.criteria.proto = proto;
                        exp.criteria.src_addr = src.empty()
                            ? std::vector<std::string>{}
                            : std::vector<std::string>{src};
                        exp.criteria.dst_addr = dst.empty()
                            ? std::vector<std::string>{}
                            : std::vector<std::string>{dst};
                        expanded.push_back(std::move(exp));
                    }
                }
            }
        }
    }

    return expanded;
}

bool action_matches(const ParsedIptablesRule& actual,
                    const ExpectedIptablesRule& expected) {
    if (expected.action_type == RuleActionType::Mark) {
        return actual.is_mark && actual.mark_is_exact && actual.fwmark == expected.fwmark;
    }
    if (expected.action_type == RuleActionType::Drop) {
        return actual.is_drop;
    }
    return actual.is_pass;
}

bool rule_matches(const ParsedIptablesRule& actual,
                  const ExpectedIptablesRule& expected) {
    return actual.ipv6 == expected.ipv6 &&
           actual.set_name == expected.set_name &&
           action_matches(actual, expected) &&
           criteria_equal(actual.criteria, expected.criteria);
}

ParsedIptablesState parse_iptables_s_for_family(const std::string& output,
                                                bool ipv6) {
    ParsedIptablesState state;

    static constexpr const char* CHAIN_NAME = "KeenPbrTable";
    const std::string chain_decl = std::string("-N ") + CHAIN_NAME;
    const std::string prerouting_jump =
        std::string("-A PREROUTING -j ") + CHAIN_NAME;
    const std::string chain_rule_prefix =
        std::string("-A ") + CHAIN_NAME + " ";

    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        if (line == chain_decl) {
            state.has_keen_pbr_chain = true;
            continue;
        }
        if (line == prerouting_jump) {
            state.has_prerouting_jump = true;
            continue;
        }
        if (line.rfind(chain_rule_prefix, 0) != 0) {
            continue;
        }

        ParsedIptablesRule rule;
        rule.ipv6 = ipv6;

        const auto tokens = split_ws(line);
        bool negate_next = false;

        for (size_t i = 0; i < tokens.size(); ++i) {
            const auto& tok = tokens[i];

            if (tok == "!") {
                negate_next = true;
                continue;
            }
            if (tok == "-m" && i + 1 < tokens.size()) {
                ++i;
                continue;
            }
            if (tok == "--match-set" && i + 2 < tokens.size()) {
                rule.set_name = tokens[i + 1];
                i += 2;
                negate_next = false;
                continue;
            }
            if (tok == "-s" && i + 1 < tokens.size()) {
                rule.criteria.src_addr = {tokens[i + 1]};
                rule.criteria.negate_src_addr = negate_next;
                ++i;
                negate_next = false;
                continue;
            }
            if (tok == "-d" && i + 1 < tokens.size()) {
                rule.criteria.dst_addr = {tokens[i + 1]};
                rule.criteria.negate_dst_addr = negate_next;
                ++i;
                negate_next = false;
                continue;
            }
            if (tok == "-p" && i + 1 < tokens.size()) {
                rule.criteria.proto = parse_proto_token(tokens[i + 1]).value_or(L4Proto::Any);
                ++i;
                negate_next = false;
                continue;
            }
            if ((tok == "--sport" || tok == "--sports") && i + 1 < tokens.size()) {
                rule.criteria.src_port = tokens[i + 1];
                rule.criteria.negate_src_port = negate_next;
                ++i;
                negate_next = false;
                continue;
            }
            if ((tok == "--dport" || tok == "--dports") && i + 1 < tokens.size()) {
                rule.criteria.dst_port = tokens[i + 1];
                rule.criteria.negate_dst_port = negate_next;
                ++i;
                negate_next = false;
                continue;
            }
            if (tok == "-j" && i + 1 < tokens.size()) {
                const auto& action = tokens[i + 1];
                if (action == "DROP") {
                    rule.is_drop = true;
                } else if (action == "RETURN") {
                    rule.is_pass = true;
                } else if (action == "MARK" && i + 3 < tokens.size()) {
                    if (tokens[i + 2] == "--set-mark") {
                        const auto mark = parse_u32(tokens[i + 3]);
                        if (!mark.has_value()) break;
                        rule.is_mark = true;
                        rule.fwmark = *mark;
                    } else if (tokens[i + 2] == "--set-xmark") {
                        const auto value = tokens[i + 3];
                        const auto slash = value.find('/');
                        if (slash == std::string::npos) break;
                        const auto mark = parse_u32(value.substr(0, slash));
                        const auto mask = parse_u32(value.substr(slash + 1));
                        if (!mark.has_value() || !mask.has_value()) break;
                        rule.is_mark = true;
                        rule.fwmark = *mark;
                        rule.xmark_mask = *mask;
                        rule.mark_is_exact = (*mask == 0xFFFFFFFFu);
                    }
                }
                break;
            }

            negate_next = false;
        }

        if (!rule.is_mark && !rule.is_drop && !rule.is_pass) {
            continue;
        }
        if (rule.set_name.empty() && rule.criteria.empty()) {
            continue;
        }

        state.rules.push_back(std::move(rule));
    }

    return state;
}

} // namespace

ParsedIptablesState parse_iptables_s(const std::string& output) {
    return parse_iptables_s_for_family(output, false);
}

IptablesFirewallVerifier::IptablesFirewallVerifier(CommandRunner runner)
    : runner_(std::move(runner)) {}

const IptablesFirewallVerifier::CachedState& IptablesFirewallVerifier::get_state() const {
    if (!cached_state_.has_value()) {
        CachedState state;

        auto read_state = [this](const std::vector<std::string>& chain_args,
                                 const std::vector<std::string>& prerouting_args,
                                 bool ipv6) {
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

            return parse_iptables_s_for_family(combined, ipv6);
        };

        state.v4 = read_state(
            {"iptables", "-t", "mangle", "-S", CHAIN_NAME},
            {"iptables", "-t", "mangle", "-S", "PREROUTING"},
            false);
        state.v6 = read_state(
            {"ip6tables", "-t", "mangle", "-S", CHAIN_NAME},
            {"ip6tables", "-t", "mangle", "-S", "PREROUTING"},
            true);
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

    std::vector<ParsedIptablesRule> actual_rules = v4.rules;
    actual_rules.insert(actual_rules.end(), v6.rules.begin(), v6.rules.end());
    std::vector<bool> used(actual_rules.size(), false);

    std::vector<FirewallRuleCheck> checks;
    for (const auto& exp : expand_expected_rule_states(expected)) {
        FirewallRuleCheck check;
        check.set_name = exp.set_name.empty() ? "<direct>" : exp.set_name;
        check.action = exp.action_type == RuleActionType::Mark
            ? "mark"
            : (exp.action_type == RuleActionType::Drop ? "drop" : "pass");
        if (exp.action_type == RuleActionType::Mark) {
            check.expected_fwmark = exp.fwmark;
        }

        auto it = std::find_if(actual_rules.begin(), actual_rules.end(),
                               [&](const ParsedIptablesRule& actual) {
                                   const size_t index =
                                       static_cast<size_t>(&actual - actual_rules.data());
                                   return !used[index] && rule_matches(actual, exp);
                               });

        if (it != actual_rules.end()) {
            const size_t index = static_cast<size_t>(it - actual_rules.begin());
            used[index] = true;
            if (it->is_mark) {
                check.actual_fwmark = it->fwmark;
            }
            check.status = CheckStatus::ok;
            check.detail = "ok";
            checks.push_back(std::move(check));
            continue;
        }

        auto same_shape = std::find_if(actual_rules.begin(), actual_rules.end(),
                                       [&](const ParsedIptablesRule& actual) {
                                           const size_t index =
                                               static_cast<size_t>(&actual - actual_rules.data());
                                           return !used[index] &&
                                                  actual.ipv6 == exp.ipv6 &&
                                                  actual.set_name == exp.set_name &&
                                                  criteria_equal(actual.criteria, exp.criteria);
                                       });

        if (same_shape == actual_rules.end()) {
            check.status = CheckStatus::missing;
            check.detail = keen_pbr3::format(
                "rule not found in iptables mangle table (family={} criteria={})",
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
                check.detail = !same_shape->mark_is_exact
                    ? keen_pbr3::format(
                          "live rule uses partial xmark mask: got {:#x}/{:#x}, expected exact mark {:#x}",
                          same_shape->fwmark, same_shape->xmark_mask, exp.fwmark)
                    : keen_pbr3::format(
                          "fwmark mismatch: expected {:#x} got {:#x}",
                          exp.fwmark, same_shape->fwmark);
            } else if (same_shape->is_drop) {
                check.status = CheckStatus::mismatch;
                check.detail = "expected MARK rule but found DROP rule";
            } else {
                check.status = CheckStatus::mismatch;
                check.detail = "expected MARK rule but found RETURN rule";
            }
        } else if (exp.action_type == RuleActionType::Drop) {
            check.status = CheckStatus::mismatch;
            check.detail = same_shape->is_mark
                ? "expected DROP rule but found MARK rule"
                : "expected DROP rule but found RETURN rule";
        } else {
            check.status = CheckStatus::mismatch;
            check.detail = same_shape->is_mark
                ? "expected RETURN rule but found MARK rule"
                : "expected RETURN rule but found DROP rule";
        }

        checks.push_back(std::move(check));
    }

    return checks;
}

std::unique_ptr<FirewallVerifier> create_iptables_verifier(CommandRunner runner) {
    return std::make_unique<IptablesFirewallVerifier>(std::move(runner));
}

} // namespace keen_pbr3
