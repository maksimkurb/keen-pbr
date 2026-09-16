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

std::string unquote_comment(std::string value) {
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        value = value.substr(1, value.size() - 2);
    }
    return value;
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
           lhs.dscp == rhs.dscp &&
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
    if (criteria.dscp.has_value()) {
        parts.push_back(keen_pbr3::format("dscp={}", static_cast<int>(*criteria.dscp)));
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
    if (set_name.rfind("kpbr6_", 0) == 0 || set_name.rfind("kpbr6s_", 0) == 0 ||
        set_name.rfind("kpbr6S_", 0) == 0 || set_name.rfind("kpbr6d_", 0) == 0) {
        return true;
    }
    if (set_name.rfind("kpbr4_", 0) == 0 || set_name.rfind("kpbr4s_", 0) == 0 ||
        set_name.rfind("kpbr4S_", 0) == 0 || set_name.rfind("kpbr4d_", 0) == 0) {
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
                    const ExpectedIptablesRule& expected,
                    uint32_t expected_fwmark_mask) {
    if (expected.action_type == RuleActionType::Mark) {
        return actual.is_mark &&
               actual.fwmark == expected.fwmark &&
               actual.xmark_mask == expected_fwmark_mask;
    }
    if (expected.action_type == RuleActionType::Drop) {
        return actual.is_drop;
    }
    return actual.is_pass;
}

bool rule_matches(const ParsedIptablesRule& actual,
                  const ExpectedIptablesRule& expected,
                  uint32_t expected_fwmark_mask) {
    return actual.ipv6 == expected.ipv6 &&
           actual.set_name == expected.set_name &&
           action_matches(actual, expected, expected_fwmark_mask) &&
           criteria_equal(actual.criteria, expected.criteria);
}

bool is_chain_or_generation(const std::string& value,
                            const std::string& base) {
    return value == base ||
           (value.rfind(base + "_", 0) == 0 && value.size() > base.size() + 1U);
}

ParsedIptablesState parse_iptables_s_for_family(const std::string& output,
                                                bool ipv6,
                                                const std::string& chain_name,
                                                bool include_output) {
    ParsedIptablesState state;

    const std::string chain_decl = std::string("-N ") + chain_name;
    const std::string prerouting_jump =
        std::string("-A PREROUTING -j ") + chain_name;
    const std::vector<std::string> output_chains = {
        "KeenPbrOutput", "KeenPbrTable_OUTPUT"};

    std::istringstream stream(output);
    std::string line;

    std::size_t line_order = 0;
    while (std::getline(stream, line)) {
        const std::size_t current_order = line_order++;
        if (line == chain_decl) {
            state.has_keen_pbr_chain = true;
            continue;
        }
        if (line == prerouting_jump) {
            state.has_prerouting_jump = true;
            continue;
        }

        const auto tokens = split_ws(line);
        if (tokens.size() < 3 || tokens[0] != "-A") {
            continue;
        }

        if (tokens[1] == "PREROUTING") {
            for (size_t index = 2; index < tokens.size(); ++index) {
                if (index + 1 < tokens.size() &&
                    (tokens[index] == "-j" || tokens[index] == "-g" ||
                     tokens[index] == "--goto" || tokens[index] == "--jump") &&
                    tokens[index + 1] == chain_name) {
                    state.has_prerouting_jump = true;
                }
                if ((tokens[index] == "--goto=" + chain_name) ||
                    (tokens[index] == "--jump=" + chain_name) ||
                    (tokens[index] == "-g" + chain_name) ||
                    (tokens[index] == "-j" + chain_name)) {
                    state.has_prerouting_jump = true;
                }
            }
        }
        if (tokens[1] == "OUTPUT") {
            for (size_t index = 2; index < tokens.size(); ++index) {
                if (index + 1 < tokens.size() &&
                    (tokens[index] == "-j" || tokens[index] == "-g" ||
                     tokens[index] == "--goto" || tokens[index] == "--jump") &&
                    std::find(output_chains.begin(), output_chains.end(),
                              tokens[index + 1]) != output_chains.end()) {
                    state.has_output_jump = true;
                }
                if (std::any_of(output_chains.begin(), output_chains.end(),
                                [&](const std::string& chain) {
                                    return tokens[index] == "--goto=" + chain ||
                                           tokens[index] == "--jump=" + chain ||
                                           tokens[index] == "-g" + chain ||
                                           tokens[index] == "-j" + chain;
                                })) {
                    state.has_output_jump = true;
                }
            }
        }

        const std::string& source_chain = tokens[1];
        const bool prerouting_chain = is_chain_or_generation(source_chain, chain_name);
        bool output_chain = false;
        for (const auto& output_base : output_chains) {
            if (is_chain_or_generation(source_chain, output_base)) {
                output_chain = true;
                break;
            }
        }
        if (output_chain && !include_output) {
            continue;
        }
        if (!prerouting_chain && !output_chain) {
            // Dispatcher and foreign chains are only interesting when they
            // identify the active private generation below.
            continue;
        }

        std::string jump_target;
        for (size_t index = 2; index < tokens.size(); ++index) {
            if ((tokens[index] == "-j" || tokens[index] == "-g" ||
                 tokens[index] == "--goto" || tokens[index] == "--jump") &&
                index + 1 < tokens.size()) {
                jump_target = tokens[index + 1];
                break;
            }
            if (tokens[index].rfind("--goto=", 0) == 0 ||
                tokens[index].rfind("--jump=", 0) == 0) {
                jump_target = tokens[index].substr(tokens[index].find('=') + 1U);
                break;
            }
            if (tokens[index].rfind("-g", 0) == 0 && tokens[index].size() > 2U) {
                jump_target = tokens[index].substr(2U);
                break;
            }
            if (tokens[index].rfind("-j", 0) == 0 && tokens[index].size() > 2U) {
                jump_target = tokens[index].substr(2U);
                break;
            }
        }

        bool dispatcher_jump = false;
        if (!jump_target.empty()) {
            const std::string& target = jump_target;
            if (source_chain == chain_name &&
                is_chain_or_generation(target, chain_name) && target != chain_name) {
                state.active_prerouting_chains.push_back(target);
                dispatcher_jump = true;
            }
            for (const auto& output_base : output_chains) {
                if (source_chain == output_base &&
                    (is_chain_or_generation(target, output_base) ||
                     is_chain_or_generation(target, chain_name)) &&
                    target != output_base) {
                    state.active_output_chains.push_back(target);
                    dispatcher_jump = true;
                }
            }
        }

        if (dispatcher_jump) {
            // A dispatcher jump is not a packet rule.  Continue here so a
            // target chain is not accidentally represented as an action.
            continue;
        }

        ParsedIptablesRule rule;
        rule.ipv6 = ipv6;
        rule.hook = output_chain ? FirewallHook::output : FirewallHook::prerouting;
        rule.chain_name = source_chain;
        rule.raw = line;
        rule.order = current_order;

        bool negate_next = false;
        bool inbound_filter_negated = false;

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
            if (tok == "--comment" && i + 1 < tokens.size()) {
                rule.comment = unquote_comment(tokens[++i]);
                negate_next = false;
                continue;
            }
            if (tok == "--match-set" && i + 2 < tokens.size()) {
                rule.set_name = tokens[i + 1];
                i += 2;
                negate_next = false;
                continue;
            }
            if (tok == "-i" && i + 1 < tokens.size()) {
                inbound_filter_negated = negate_next;
                rule.inbound_interfaces.push_back(tokens[i + 1]);
                ++i;
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
            if (tok == "--mark" && i + 1 < tokens.size()) {
                const auto value = tokens[i + 1];
                const auto slash = value.find('/');
                if (negate_next) {
                    const auto mark = parse_u32(
                        slash == std::string::npos ? value
                                                   : value.substr(0, slash));
                    const auto mask = slash == std::string::npos
                        ? std::optional<uint32_t>{0xFFFFFFFFu}
                        : parse_u32(value.substr(slash + 1));
                    rule.has_nonzero_mark_match =
                        mark.has_value() && mask.has_value() && *mark == 0 &&
                        *mask == 0xFFFFFFFFu;
                }
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
            if (tok == "--dscp" && i + 1 < tokens.size()) {
                const auto value = parse_u32(tokens[i + 1]);
                if (value.has_value() && *value <= 63) {
                    rule.criteria.dscp = static_cast<uint8_t>(*value);
                }
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
                    rule.is_return = true;
                } else if (action == "ACCEPT") {
                    rule.is_pass = true;
                    rule.is_accept = true;
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

        const auto has_token = [&tokens](const char* value) {
            return std::find(tokens.begin(), tokens.end(), value) != tokens.end();
        };
        const auto has_jump_target = [&tokens](const char* target) {
            for (std::size_t index = 0; index + 1 < tokens.size(); ++index) {
                if ((tokens[index] == "-j" || tokens[index] == "--jump" ||
                     tokens[index] == "-g" || tokens[index] == "--goto") &&
                    tokens[index + 1] == target) {
                    return true;
                }
            }
            return false;
        };
        const bool keyed_prefilter = rule.comment.has_value() &&
            rule.comment->rfind("kpbr:v1:prefilter.", 0) == 0;
        const auto parse_mark_guard = [&tokens](const char* module)
            -> std::optional<uint32_t> {
            for (std::size_t index = 0; index + 1 < tokens.size(); ++index) {
                if (tokens[index] != "-m" || tokens[index + 1] != module) {
                    continue;
                }
                bool negated = false;
                for (std::size_t match_index = index + 2;
                     match_index + 1 < tokens.size(); ++match_index) {
                    if (tokens[match_index] == "-m" ||
                        tokens[match_index] == "-j") {
                        break;
                    }
                    if (tokens[match_index] == "!") {
                        negated = true;
                        continue;
                    }
                    if (tokens[match_index] != "--mark") continue;
                    const auto value = tokens[match_index + 1];
                    const auto slash = value.find('/');
                    if (!negated) continue;
                    const auto mark = parse_u32(
                        slash == std::string::npos ? value
                                                   : value.substr(0, slash));
                    // iptables -S omits the all-bits mask when it is the
                    // default.  Treat the compact form as the exact
                    // full-mask guard; partial owned masks remain explicit.
                    const auto mask = slash == std::string::npos
                        ? std::optional<uint32_t>{0xFFFFFFFFu}
                        : parse_u32(value.substr(slash + 1));
                    if (mark.has_value() && mask.has_value() && *mark == 0) {
                        return *mask;
                    }
                }
            }
            return std::nullopt;
        };
        const auto restore_guard_mask = parse_mark_guard("connmark");
        const auto companion_guard_mask = parse_mark_guard("mark");
        rule.mark_guard_present = companion_guard_mask.has_value();
        if (companion_guard_mask.has_value()) {
            rule.mark_guard_mask = *companion_guard_mask;
        }
        rule.restore_guard_present = restore_guard_mask.has_value();
        if (restore_guard_mask.has_value()) {
            rule.restore_guard_mask = *restore_guard_mask;
        }
        const bool original_direction = [&tokens] {
            for (std::size_t index = 0; index + 1 < tokens.size(); ++index) {
                if (tokens[index] == "--ctdir" &&
                    tokens[index + 1] == "ORIGINAL") {
                    return true;
                }
            }
            return false;
        }();
        rule.restore_target_exact = has_jump_target("CONNMARK");
        rule.is_restore_conntrack =
            rule.restore_target_exact && has_token("--restore-mark") &&
            original_direction && restore_guard_mask.has_value();
        rule.is_skip_dnat = rule.is_return && has_token("--ctstate") &&
                            std::find(tokens.begin(), tokens.end(), "DNAT") !=
                                tokens.end();
        rule.is_skip_marked = rule.is_accept && rule.mark_guard_present &&
                              rule.mark_guard_mask == 0xFFFFFFFFu;
        rule.is_inbound_filter = rule.is_return && !rule.inbound_interfaces.empty() &&
                                 inbound_filter_negated;
        rule.is_restore_companion = rule.is_return && original_direction &&
                                    companion_guard_mask.has_value();
        if (rule.is_restore_companion) {
            rule.conntrack_mark_mask = *companion_guard_mask;
        }
        if (rule.is_restore_conntrack) {
            std::optional<uint32_t> nfmask;
            std::optional<uint32_t> ctmask;
            for (std::size_t index = 0; index + 1 < tokens.size(); ++index) {
                if (tokens[index] == "--mask") {
                    const auto mask = parse_u32(tokens[index + 1]);
                    nfmask = mask;
                    ctmask = mask;
                } else if (tokens[index] == "--nfmask") {
                    nfmask = parse_u32(tokens[index + 1]);
                } else if (tokens[index] == "--ctmask") {
                    ctmask = parse_u32(tokens[index + 1]);
                }
            }
            if (nfmask.has_value() && ctmask.has_value() &&
                *nfmask != *ctmask) {
                // The canonical action has one owned mask. Preserve a
                // deliberate mismatch instead of silently accepting a rule
                // that restores different packet and conntrack bit ranges.
                rule.conntrack_mark_mask = 0;
            } else if (ctmask.has_value()) {
                rule.conntrack_mark_mask = *ctmask;
            } else if (nfmask.has_value()) {
                rule.conntrack_mark_mask = *nfmask;
            }
            rule.is_restore_conntrack =
                rule.conntrack_mark_mask != 0 &&
                rule.conntrack_mark_mask == rule.restore_guard_mask;
        }

        if (!rule.is_mark && !rule.is_drop && !rule.is_pass &&
            !rule.is_restore_conntrack && !rule.is_restore_companion &&
            !keyed_prefilter) {
            continue;
        }
        if (!rule.is_mark && !rule.is_drop && !rule.is_pass &&
            !rule.is_restore_conntrack && !rule.is_restore_companion) {
            // Preserve a keyed malformed rule as an observed generic action
            // so health reports a mismatch rather than silently downgrading
            // it to a missing rule.  Unkeyed foreign comments are still
            // excluded from ownership and legacy matching below.
            if (keyed_prefilter) rule.is_pass = true;
        }
        if (rule.set_name.empty() && rule.criteria.empty() &&
            !rule.is_restore_conntrack && !rule.is_skip_dnat &&
            !rule.is_skip_marked && !rule.is_inbound_filter &&
            !rule.is_restore_companion) {
            // Keep a commented empty rule so the canonical verifier can
            // report a malformed owned prefilter as a mismatch rather than
            // silently reducing it to a missing rule.  Uncommented dispatch
            // and foreign rules retain the legacy filtering behavior.
            if (!rule.comment.has_value()) continue;
        }

        state.rules.push_back(std::move(rule));
    }

    state.has_output_chain = false;
    for (const auto& output_base : output_chains) {
        const std::string declaration = "-N " + output_base;
        const std::string jump = "-A OUTPUT -j " + output_base;
        std::istringstream declarations(output);
        std::string candidate;
        while (std::getline(declarations, candidate)) {
            if (candidate == declaration) {
                state.has_output_chain = true;
                state.output_chains.push_back(output_base);
            }
            if (candidate == jump) state.has_output_jump = true;
        }
    }

    return state;
}

} // namespace

ParsedIptablesState parse_iptables_s(const std::string& output) {
    return parse_iptables_s_for_family(output, false, "KeenPbrTable", false);
}

ParsedIptablesState parse_iptables_s_family(const std::string& output,
                                            bool ipv6,
                                            const std::string& chain_name) {
    return parse_iptables_s_for_family(output, ipv6, chain_name, true);
}

std::vector<ParsedIpset> parse_ipset_save(const std::string& output) {
    std::vector<ParsedIpset> sets;
    std::istringstream input(output);
    std::string line;
    while (std::getline(input, line)) {
        const auto tokens = split_ws(line);
        if (tokens.size() < 5 || tokens[0] != "create" ||
            (tokens[1].rfind("kpbr4_", 0) != 0 && tokens[1].rfind("kpbr6_", 0) != 0 &&
             tokens[1].rfind("kpbr4s_", 0) != 0 && tokens[1].rfind("kpbr6s_", 0) != 0 &&
             tokens[1].rfind("kpbr4S_", 0) != 0 && tokens[1].rfind("kpbr6S_", 0) != 0 &&
             tokens[1].rfind("kpbr4d_", 0) != 0 && tokens[1].rfind("kpbr6d_", 0) != 0)) {
            continue;
        }
        ParsedIpset set;
        set.name = tokens[1];
        for (size_t index = 2; index + 1 < tokens.size(); ++index) {
            if (tokens[index] == "family") {
                set.family = tokens[index + 1] == "inet6" ? AF_INET6 : AF_INET;
            }
            if (tokens[index] == "timeout") {
                set.timeout_seconds = parse_u32(tokens[index + 1]).value_or(0);
            }
        }
        if (set.family != 0) sets.push_back(std::move(set));
    }
    return sets;
}

IptablesFirewallVerifier::IptablesFirewallVerifier(
    CommandRunner runner, RawPreroutingMode raw_prerouting)
    : runner_(std::move(runner)), raw_prerouting_(raw_prerouting) {}

const IptablesFirewallVerifier::CachedState& IptablesFirewallVerifier::get_state() const {
    if (!cached_state_.has_value()) {
        CachedState state;

        auto read_state = [this](const std::vector<std::string>& chain_args,
                                 const std::vector<std::string>& prerouting_args,
                                 bool ipv6, const std::string& chain_name) {
            std::string combined;

            const auto chain_result = runner_(chain_args);
            if (chain_result.exit_code == 0) {
                combined += chain_result.stdout_output;
                if (!combined.empty() && combined.back() != '\n') {
                    combined.push_back('\n');
                }

                // Preserve-sets applies the policy rules in a versioned private
                // chain and jumps to it from KeenPbrTable. `iptables -S
                // KeenPbrTable` does not include that child chain, so inspect
                // every direct versioned target before verifying the policy.
                std::istringstream chain_stream(chain_result.stdout_output);
                std::string chain_line;
                while (std::getline(chain_stream, chain_line)) {
                    const auto tokens = split_ws(chain_line);
                    for (size_t index = 0; index + 1 < tokens.size(); ++index) {
                        if (tokens[index] != "-j" ||
                            tokens[index + 1].rfind(chain_name + "_", 0) != 0 ||
                            tokens[index + 1] == "KeenPbrTable_OUTPUT") {
                            continue;
                        }
                        auto versioned_args = chain_args;
                        versioned_args.back() = tokens[index + 1];
                        const auto versioned_result = runner_(versioned_args);
                        if (versioned_result.exit_code == 0) {
                            combined += versioned_result.stdout_output;
                            if (!combined.empty() && combined.back() != '\n') {
                                combined.push_back('\n');
                            }
                        }
                        break;
                    }
                }
            }

            const auto prerouting_result = runner_(prerouting_args);
            if (prerouting_result.exit_code == 0) {
                combined += prerouting_result.stdout_output;
            }

            return parse_iptables_s_for_family(combined, ipv6, chain_name, false);
        };

        const std::string v4_chain = raw_prerouting_.ipv4 ? "KeenPbrRaw" : CHAIN_NAME;
        const std::string v4_table = raw_prerouting_.ipv4 ? "raw" : "mangle";
        state.v4 = read_state({"iptables", "-t", v4_table, "-S", v4_chain},
                              {"iptables", "-t", v4_table, "-S", "PREROUTING"},
                              false, v4_chain);
        const std::string v6_chain = raw_prerouting_.ipv6 ? "KeenPbrRaw" : CHAIN_NAME;
        const std::string v6_table = raw_prerouting_.ipv6 ? "raw" : "mangle";
        state.v6 = read_state(
            {"ip6tables", "-t", v6_table, "-S", v6_chain},
            {"ip6tables", "-t", v6_table, "-S", "PREROUTING"},
            true, v6_chain);
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
        result.detail = (!raw_prerouting_.ipv4 && !raw_prerouting_.ipv6)
            ? "KeenPbrTable chain not found in iptables or ip6tables mangle table"
            : "KeenPbrTable/KeenPbrRaw chain not found in the configured "
              "iptables PREROUTING tables";
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
                                   return !used[index] &&
                                          rule_matches(actual, exp, expected_fwmark_mask_);
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
                "rule not found in iptables {} table (family={} criteria={})",
                raw_prerouting_.uses(exp.ipv6) ? "raw" : "mangle",
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
                check.detail = same_shape->xmark_mask != expected_fwmark_mask_
                    ? keen_pbr3::format(
                          "fwmark mask mismatch: expected {:#x}/{:#x} got {:#x}/{:#x}",
                          exp.fwmark, expected_fwmark_mask_, same_shape->fwmark,
                          same_shape->xmark_mask)
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

std::unique_ptr<FirewallVerifier> create_iptables_verifier(CommandRunner runner,
                                                            RawPreroutingMode raw_prerouting) {
    return std::make_unique<IptablesFirewallVerifier>(std::move(runner), raw_prerouting);
}

} // namespace keen_pbr3
