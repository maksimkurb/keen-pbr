#include "host_health_warnings.hpp"

#include "../log/logger.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace keen_pbr3 {

namespace {

std::vector<std::string> tokenize_rule(const std::string& line) {
    std::vector<std::string> tokens;
    std::string cur;
    bool in_quote = false;
    bool have = false;
    for (const char c : line) {
        if (c == '"') {
            in_quote = !in_quote;
            have = true;
        } else if (!in_quote && (c == ' ' || c == '\t')) {
            if (have) tokens.push_back(std::move(cur));
            cur.clear();
            have = false;
        } else {
            cur.push_back(c);
            have = true;
        }
    }
    if (have) tokens.push_back(std::move(cur));
    return tokens;
}

bool iface_pattern_matches(const std::string& pattern, const std::string& name) {
    if (!pattern.empty() && pattern.back() == '+') {
        return name.compare(0, pattern.size() - 1, pattern, 0,
                            pattern.size() - 1) == 0;
    }
    return pattern == name;
}

bool is_any_address(const std::string& v) {
    return v == "0.0.0.0/0" || v == "0.0.0.0/0.0.0.0" || v == "0/0";
}

std::string trim_line_end(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
    return s;
}

int parse_rp_value(const std::optional<std::string>& text) {
    if (!text) return 0;
    try {
        return std::stoi(*text);
    } catch (...) {
        return 0;
    }
}

}  // namespace

NatCoverageResult nat_coverage_for_interface(const std::string& nat_text,
                                             const std::string& interface) {
    NatCoverageResult result;
    std::istringstream stream(nat_text);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim_line_end(line);
        if (line.rfind("-A ", 0) != 0) continue;
        const auto tokens = tokenize_rule(line);

        std::string target;
        bool negate = false;
        bool has_out = false;
        bool out_negated = false;
        std::string out_pattern;
        std::string restriction;
        for (std::size_t i = 2; i < tokens.size(); ++i) {
            const std::string& t = tokens[i];
            if (t == "!") {
                negate = true;
                continue;
            }
            const bool has_value = i + 1 < tokens.size();
            if ((t == "-o" || t == "--out-interface") && has_value) {
                has_out = true;
                out_negated = negate;
                out_pattern = tokens[++i];
            } else if ((t == "-s" || t == "--source" || t == "-d" ||
                        t == "--destination") &&
                       has_value) {
                const std::string& value = tokens[++i];
                if ((negate || !is_any_address(value)) && restriction.empty()) {
                    restriction =
                        std::string((t == "-s" || t == "--source") ? "-s " : "-d ") +
                        (negate ? "! " : "") + value;
                }
            } else if ((t == "-j" || t == "-g" || t == "--jump") && has_value) {
                target = tokens[++i];
            }
            negate = false;
        }
        if (target != "MASQUERADE" && target != "SNAT") continue;

        if (has_out) {
            const bool matches = iface_pattern_matches(out_pattern, interface);
            if (matches == out_negated) continue;
        }
        if (restriction.empty()) {
            result.coverage = NatCoverage::covered;
            result.restriction.clear();
            return result;
        }
        if (result.coverage == NatCoverage::missing) {
            result.coverage = NatCoverage::partial;
            result.restriction = restriction;
        }
    }
    return result;
}

std::vector<BalanceCandidate> balance_candidate_interfaces(const Config& config) {
    std::vector<BalanceCandidate> out;
    std::set<std::string> seen;
    const auto& outbounds = config.outbounds.value_or(std::vector<Outbound>{});
    for (const auto& group : outbounds) {
        if (!outbound_uses_balance(group)) continue;
        std::vector<std::string> tags;
        for (const auto& g : group.outbound_groups.value_or(
                 std::vector<api::OutboundGroupElement>{})) {
            for (const auto& member : outbound_group_members(g)) {
                tags.push_back(member.outbound);
            }
            for (const auto& tag : g.outbounds.value_or(std::vector<std::string>{})) {
                tags.push_back(tag);
            }
        }
        for (const auto& tag : tags) {
            const auto it = std::find_if(
                outbounds.begin(), outbounds.end(),
                [&](const Outbound& o) { return o.tag == tag; });
            if (it == outbounds.end() || it->type != OutboundType::INTERFACE ||
                !it->interface.has_value() || it->interface->empty()) {
                continue;
            }
            if (seen.insert(*it->interface).second) {
                out.push_back({*it->interface, it->tag});
            }
        }
    }
    return out;
}

std::optional<std::string> read_proc_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return std::nullopt;
    std::string content;
    std::getline(in, content);
    return content;
}

std::vector<HealthWarning> evaluate_rp_filter_warnings(
    const std::vector<BalanceCandidate>& candidates, const ProcFileReader& reader) {
    std::vector<HealthWarning> warnings;
    const int all =
        parse_rp_value(reader("/proc/sys/net/ipv4/conf/all/rp_filter"));
    for (const auto& c : candidates) {
        if (c.interface.find('/') != std::string::npos) continue;
        const auto per_if =
            reader("/proc/sys/net/ipv4/conf/" + c.interface + "/rp_filter");
        if (!per_if) continue;  // interface does not exist (yet)
        const int effective = std::max(all, parse_rp_value(per_if));
        if (effective != 1) continue;
        HealthWarning w;
        w.code = HealthWarningCode::rp_filter_strict;
        w.interface = c.interface;
        w.outbound = c.outbound_tag;
        w.message = "Strict reverse path filtering (rp_filter=1) is active on " +
                    c.interface +
                    "; replies arriving over a different WAN than the one "
                    "chosen for the connection may be dropped. Set "
                    "net.ipv4.conf.all.rp_filter and net.ipv4.conf." +
                    c.interface + ".rp_filter to 2 (loose) or 0.";
        warnings.push_back(std::move(w));
    }
    return warnings;
}

std::vector<HealthWarning> evaluate_nat_warnings(
    const std::vector<BalanceCandidate>& candidates, const std::string& nat_text) {
    std::vector<HealthWarning> warnings;
    for (const auto& c : candidates) {
        const auto cov = nat_coverage_for_interface(nat_text, c.interface);
        if (cov.coverage == NatCoverage::covered) continue;
        HealthWarning w;
        w.interface = c.interface;
        w.outbound = c.outbound_tag;
        if (cov.coverage == NatCoverage::missing) {
            w.code = HealthWarningCode::nat_missing;
            w.message = "No MASQUERADE/SNAT rule found for outgoing interface " +
                        c.interface +
                        " in the iptables nat table; connections balanced to "
                        "this WAN may hang. Add e.g. iptables -t nat -A "
                        "POSTROUTING -o " + c.interface + " -j MASQUERADE.";
        } else {
            w.code = HealthWarningCode::nat_partial;
            w.message = "NAT for " + c.interface +
                        " only covers traffic restricted by '" + cov.restriction +
                        "'; make sure all LAN subnets routed through this WAN "
                        "are NATed.";
        }
        warnings.push_back(std::move(w));
    }
    return warnings;
}

std::vector<HealthWarning> collect_host_health_warnings(
    const Config& config, FirewallBackend backend, const CommandRunner& runner,
    const ProcFileReader& reader) {
    std::vector<HealthWarning> warnings;
    const auto candidates = balance_candidate_interfaces(config);
    if (candidates.empty()) return warnings;

    if (backend == FirewallBackend::iptables) {
        try {
            const auto nat = runner({"iptables", "-t", "nat", "-S"});
            if (nat.exit_code != 0 || nat.truncated) {
                Logger::instance().debug(
                    "NAT health check skipped: cannot read iptables nat table");
            } else {
                auto nat_warnings = evaluate_nat_warnings(candidates, nat.stdout_output);
                warnings.insert(warnings.end(), nat_warnings.begin(),
                                nat_warnings.end());
            }
        } catch (const std::exception& e) {
            Logger::instance().debug("NAT health check skipped: {}", e.what());
        }
    } else {
        Logger::instance().debug(
            "NAT health check skipped: only supported on the iptables backend");
    }

    auto rp = evaluate_rp_filter_warnings(candidates, reader);
    warnings.insert(warnings.end(), rp.begin(), rp.end());
    return warnings;
}

std::vector<std::string> health_warning_keys(const std::vector<HealthWarning>& w) {
    std::vector<std::string> keys;
    for (const auto& x : w) {
        keys.push_back(std::string(health_warning_code_name(x.code)) + ":" +
                       x.interface.value_or("") + ":" + x.message);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

} // namespace keen_pbr3
