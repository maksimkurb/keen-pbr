#include "iptables_verifier.hpp"

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

} // namespace keen_pbr3
