#include "host_health_warnings.hpp"

#include "../log/logger.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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

struct ParsedNatRule {
    std::string chain;
    std::string target;
    bool has_out{false};
    bool out_negated{false};
    std::string out_pattern;
    std::vector<Ipv4Net> sources;  // positive -s networks
    std::string source_text;       // "-s a,b" for messages
    std::string restriction;       // first non -o/-s restriction, if any
};

bool is_stop_token(const std::string& t) {
    return t == "-m" || t == "-o" || t == "-s" || t == "-d" || t == "-j" ||
           t == "-g" || t == "--jump" || t == "--goto" || t == "!" ||
           t == "--out-interface" || t == "--source" || t == "--destination";
}

std::vector<std::string> split_commas(const std::string& v) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : v) {
        if (c == ',') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

bool net_contains(const Ipv4Net& outer, const Ipv4Net& inner) {
    return (outer.mask & ~inner.mask) == 0 &&
           (inner.addr & outer.mask) == (outer.addr & outer.mask);
}

bool net_overlaps(const Ipv4Net& a, const Ipv4Net& b) {
    return net_contains(a, b) || net_contains(b, a);
}

ParsedNatRule parse_nat_rule(const std::vector<std::string>& tokens) {
    ParsedNatRule rule;
    rule.chain = tokens.size() > 1 ? tokens[1] : "";
    bool negate = false;
    bool in_target = false;
    std::size_t i = 2;
    const auto note_restriction = [&](const std::string& text) {
        if (rule.restriction.empty()) rule.restriction = text;
    };
    while (i < tokens.size()) {
        const std::string& t = tokens[i];
        const bool has_value = i + 1 < tokens.size();
        if (t == "!") {
            negate = true;
            ++i;
            continue;
        }
        if (t == "-j" || t == "-g" || t == "--jump" || t == "--goto") {
            if (has_value) rule.target = tokens[i + 1];
            in_target = true;  // target options never restrict
            i += 2;
            continue;
        }
        if (in_target && t != "-o" && t != "--out-interface" && t != "-s" &&
            t != "--source" && t != "-d" && t != "--destination") {
            ++i;
            negate = false;
            continue;
        }
        if ((t == "-o" || t == "--out-interface") && has_value) {
            rule.has_out = true;
            rule.out_negated = negate;
            rule.out_pattern = tokens[i + 1];
            i += 2;
        } else if ((t == "-s" || t == "--source") && has_value) {
            const std::string& value = tokens[i + 1];
            bool any = false;
            std::vector<Ipv4Net> nets;
            for (const auto& part : split_commas(value)) {
                if (is_any_address(part)) any = true;
                if (const auto net = parse_ipv4_net(part)) nets.push_back(*net);
            }
            if (negate) {
                note_restriction("! -s " + value);
            } else if (!any) {
                if (nets.empty()) {
                    note_restriction("-s " + value);  // unparsable: restrict
                } else {
                    rule.sources.insert(rule.sources.end(), nets.begin(), nets.end());
                    if (rule.source_text.empty()) rule.source_text = "-s " + value;
                }
            }
            i += 2;
        } else if ((t == "-d" || t == "--destination") && has_value) {
            const std::string& value = tokens[i + 1];
            // A negated -d (for example ! -d 10.0.0.0/8) is not a restriction.
            if (!negate && !is_any_address(value)) note_restriction("-d " + value);
            i += 2;
        } else {
            // Any other match (-m <module> ..., -p, -i, --dport, ...) restricts,
            // except the purely informational comment module.
            std::string text = negate ? "! " : "";
            std::size_t j = i;
            while (j < tokens.size() && (j == i || !is_stop_token(tokens[j]))) {
                if (j > i) text += ' ';
                text += tokens[j];
                ++j;
            }
            const bool is_comment = t == "-m" && has_value && tokens[i + 1] == "comment";
            if (!is_comment) note_restriction(text);
            i = j;
        }
        negate = false;
    }
    return rule;
}

std::string hex_bits(std::uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08x", static_cast<unsigned>(v));
    return buf;
}

bool parse_u32(const std::string& text, std::uint32_t& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    const unsigned long long v = std::strtoull(text.c_str(), &end, 0);
    if (end == text.c_str() || *end != '\0' || v > 0xFFFFFFFFULL) return false;
    out = static_cast<std::uint32_t>(v);
    return true;
}

// "V" or "V/M"; has_mask tells which.
bool parse_value_mask(const std::string& text, std::uint32_t& value,
                      std::uint32_t& mask, bool& has_mask) {
    const auto slash = text.find('/');
    has_mask = slash != std::string::npos;
    mask = 0xFFFFFFFFU;
    if (!parse_u32(text.substr(0, slash), value)) return false;
    return !has_mask || parse_u32(text.substr(slash + 1), mask);
}

constexpr std::uint32_t kAllBits = 0xFFFFFFFFU;

// Bits touched (written or matched) by one iptables rule line; 0 when the rule
// has no mark semantics.
std::uint32_t rule_mark_bits(const std::vector<std::string>& tokens) {
    std::string target;
    bool save = false, restore = false;
    std::uint32_t nfmask = 0, ctmask = 0, mask = 0;
    bool has_nfmask = false, has_ctmask = false, has_mask = false;
    std::uint32_t bits = 0;
    for (std::size_t i = 2; i < tokens.size(); ++i) {
        const std::string& t = tokens[i];
        const bool has_value = i + 1 < tokens.size();
        std::uint32_t v = 0, m = 0;
        bool hm = false;
        if ((t == "-j" || t == "-g" || t == "--jump" || t == "--goto") && has_value) {
            target = tokens[++i];
        } else if (t == "--mark" && has_value) {
            // mark / connmark match
            if (parse_value_mask(tokens[++i], v, m, hm)) bits |= hm ? m : v;
        } else if (target != "MARK" && target != "CONNMARK") {
            continue;
        } else if ((t == "--set-xmark" || t == "--set-mark") && has_value) {
            if (parse_value_mask(tokens[++i], v, m, hm)) bits |= m;
        } else if (t == "--or-mark" && has_value) {
            if (parse_u32(tokens[++i], v)) bits |= v;
        } else if (t == "--xor-mark" && has_value) {
            if (parse_u32(tokens[++i], v)) bits |= v;
        } else if (t == "--and-mark" && has_value) {
            if (parse_u32(tokens[++i], v)) bits |= ~v;
        } else if (t == "--save-mark") {
            save = true;
        } else if (t == "--restore-mark") {
            restore = true;
        } else if (t == "--nfmask" && has_value) {
            has_nfmask = parse_u32(tokens[++i], nfmask);
        } else if (t == "--ctmask" && has_value) {
            has_ctmask = parse_u32(tokens[++i], ctmask);
        } else if (t == "--mask" && has_value) {
            has_mask = parse_u32(tokens[++i], mask);
        }
    }
    if (save) bits |= has_ctmask ? ctmask : (has_mask ? mask : kAllBits);
    if (restore) bits |= has_nfmask ? nfmask : (has_mask ? mask : kAllBits);
    return bits;
}

}  // namespace

std::vector<ForeignMarkRule> find_foreign_mark_overlaps(
    const std::string& iptables_text, std::uint32_t fwmark_mask) {
    std::vector<ForeignMarkRule> out;
    std::istringstream stream(iptables_text);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim_line_end(line);
        if (line.rfind("-A ", 0) != 0) continue;
        if (line.find("kpbr:v1:") != std::string::npos) continue;
        const auto tokens = tokenize_rule(line);
        if (tokens.size() < 2 || tokens[1].rfind("KeenPbr", 0) == 0) continue;
        const std::uint32_t overlap = rule_mark_bits(tokens) & fwmark_mask;
        if (overlap == 0) continue;
        out.push_back({line, overlap});
    }
    return out;
}

std::vector<ForeignPolicyRule> find_foreign_policy_rule_overlaps(
    const std::vector<DumpedRule>& dumped, const std::vector<RuleSpec>& own_rules,
    std::uint32_t fwmark_mask) {
    std::vector<ForeignPolicyRule> out;
    for (const auto& r : dumped) {
        if (r.fwmask == 0) continue;  // no fwmark selector
        const bool own = std::any_of(
            own_rules.begin(), own_rules.end(), [&](const RuleSpec& s) {
                return s.priority == r.priority && s.fwmark == r.fwmark &&
                       s.fwmask == r.fwmask && s.table == r.table;
            });
        if (own) continue;
        const std::uint32_t overlap = r.fwmask & fwmark_mask;
        if (overlap == 0) continue;
        const bool dup = std::any_of(out.begin(), out.end(), [&](const auto& x) {
            return x.priority == r.priority && x.fwmark == r.fwmark &&
                   x.fwmask == r.fwmask;
        });
        if (!dup) out.push_back({r.priority, r.fwmark, r.fwmask, overlap});
    }
    return out;
}

std::vector<HealthWarning> evaluate_fwmark_conflict_warnings(
    const std::vector<std::pair<std::string, std::string>>& iptables_texts,
    const std::vector<DumpedRule>& dumped_rules,
    const std::vector<RuleSpec>& own_rules, std::uint32_t fwmark_mask) {
    std::vector<HealthWarning> warnings;
    std::set<std::string> seen;
    const std::string advice =
        "; move fwmark.start/fwmark.mask to bits nobody else uses";
    for (const auto& [label, text] : iptables_texts) {
        for (const auto& f : find_foreign_mark_overlaps(text, fwmark_mask)) {
            if (!seen.insert(f.rule).second) continue;
            HealthWarning w;
            w.code = HealthWarningCode::fwmark_mask_conflict;
            w.message = "Foreign iptables " + label + " rule '" + f.rule +
                        "' uses mark bits " + hex_bits(f.overlap) +
                        " that overlap fwmark.mask " + hex_bits(fwmark_mask) +
                        " and can clobber keen-pbr's routing choice" + advice;
            warnings.push_back(std::move(w));
        }
    }
    for (const auto& r :
         find_foreign_policy_rule_overlaps(dumped_rules, own_rules, fwmark_mask)) {
        HealthWarning w;
        w.code = HealthWarningCode::fwmark_mask_conflict;
        w.message = "Foreign ip rule priority " + std::to_string(r.priority) +
                    " fwmark " + hex_bits(r.fwmark) + "/" + hex_bits(r.fwmask) +
                    " matches mark bits " + hex_bits(r.overlap) +
                    " that overlap fwmark.mask " + hex_bits(fwmark_mask) + advice;
        warnings.push_back(std::move(w));
    }
    return warnings;
}


std::optional<Ipv4Net> parse_ipv4_net(const std::string& text) {
    const auto slash = text.find('/');
    const std::string addr = text.substr(0, slash);
    unsigned a = 0, b = 0, c = 0, d = 0;
    char tail = 0;
    if (std::sscanf(addr.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 ||
        a > 255 || b > 255 || c > 255 || d > 255) {
        return std::nullopt;
    }
    uint32_t mask = 0xFFFFFFFFu;
    if (slash != std::string::npos) {
        const std::string m = text.substr(slash + 1);
        unsigned ma = 0, mb = 0, mc = 0, md = 0;
        if (std::sscanf(m.c_str(), "%u.%u.%u.%u%c", &ma, &mb, &mc, &md, &tail) == 4) {
            mask = (ma << 24) | (mb << 16) | (mc << 8) | md;
        } else {
            unsigned len = 0;
            if (m.empty() || std::sscanf(m.c_str(), "%u%c", &len, &tail) != 1 ||
                len > 32) {
                return std::nullopt;
            }
            mask = len == 0 ? 0U : (0xFFFFFFFFu << (32U - len));
        }
    }
    const uint32_t ip = (a << 24) | (b << 16) | (c << 8) | d;
    return Ipv4Net{ip & mask, mask};
}

std::vector<Ipv4Net> interface_subnets(const std::vector<DumpedInterface>& interfaces,
                                       const std::vector<std::string>& names) {
    std::vector<Ipv4Net> out;
    for (const auto& iface : interfaces) {
        if (std::find(names.begin(), names.end(), iface.name) == names.end()) continue;
        for (const auto& addr : iface.ipv4_addresses) {
            if (const auto net = parse_ipv4_net(addr)) out.push_back(*net);
        }
    }
    return out;
}

NatCoverageResult nat_coverage_for_interface(const std::string& nat_text,
                                             const std::string& interface,
                                             const std::vector<Ipv4Net>& lan_subnets) {
    std::vector<ParsedNatRule> rules;
    std::istringstream stream(nat_text);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim_line_end(line);
        if (line.rfind("-A ", 0) != 0) continue;
        rules.push_back(parse_nat_rule(tokenize_rule(line)));
    }

    // Transitive closure of chains reachable from POSTROUTING.
    std::set<std::string> reachable{"POSTROUTING"};
    for (bool grown = true; grown;) {
        grown = false;
        for (const auto& rule : rules) {
            if (reachable.count(rule.chain) != 0 && !rule.target.empty() &&
                reachable.insert(rule.target).second) {
                grown = true;
            }
        }
    }

    NatCoverageResult result;
    for (const auto& rule : rules) {
        if (reachable.count(rule.chain) == 0) continue;
        if (rule.target != "MASQUERADE" && rule.target != "SNAT") continue;
        if (rule.has_out) {
            const bool matches = iface_pattern_matches(rule.out_pattern, interface);
            if (matches == rule.out_negated) continue;
        }

        std::string restriction = rule.restriction;
        if (restriction.empty() && !rule.sources.empty() && !lan_subnets.empty()) {
            std::size_t contained = 0;
            bool overlaps = false;
            for (const auto& lan : lan_subnets) {
                bool in = false;
                for (const auto& src : rule.sources) {
                    if (net_contains(src, lan)) in = true;
                    if (net_overlaps(src, lan)) overlaps = true;
                }
                if (in) ++contained;
            }
            if (!overlaps) continue;  // unrelated to the LAN: does not count
            if (contained != lan_subnets.size()) restriction = rule.source_text;
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
    const std::vector<BalanceCandidate>& candidates, const std::string& nat_text,
    const std::vector<Ipv4Net>& lan_subnets) {
    std::vector<HealthWarning> warnings;
    for (const auto& c : candidates) {
        const auto cov = nat_coverage_for_interface(nat_text, c.interface, lan_subnets);
        if (cov.coverage == NatCoverage::covered) continue;
        HealthWarning w;
        w.interface = c.interface;
        w.outbound = c.outbound_tag;
        if (cov.coverage == NatCoverage::missing) {
            w.code = HealthWarningCode::nat_missing;
            w.message = "No MASQUERADE/SNAT rule found for outgoing interface " +
                        c.interface +
                        " in the iptables nat table; connections balanced to "
                        "this WAN may hang. Only the iptables nat table of this "
                        "flavour was checked: NAT done in native nftables "
                        "(nftables.conf, firewalld) or in the other iptables "
                        "flavour (legacy vs nft) is not visible, so ignore this "
                        "warning if NAT is configured there. Otherwise add e.g. "
                        "iptables -t nat -A POSTROUTING -o " + c.interface +
                        " -j MASQUERADE.";
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

HostHealthInputs host_health_inputs(const Config& config) {
    HostHealthInputs inputs;
    inputs.candidates = balance_candidate_interfaces(config);
    if (config.route.has_value() && config.route->inbound_interfaces.has_value()) {
        inputs.inbound_interfaces = *config.route->inbound_interfaces;
    }
    try {
        inputs.fwmark_mask = fwmark_mask_value(config.fwmark.value_or(FwmarkConfig{}));
    } catch (const std::exception& e) {
        Logger::instance().debug("fwmark overlap check skipped: {}", e.what());
    }
    return inputs;
}

std::optional<std::string> IptablesTableCache::get(const CommandRunner& runner,
                                                   const std::string& command,
                                                   const std::string& table) {
    const std::string key = command + " " + table;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = entries_.find(key);
        if (it != entries_.end() && Clock::now() - it->second.read_at <= lifetime_) {
            return it->second.text;
        }
    }
    const auto r = runner({command, "-t", table, "-S"});
    if (r.exit_code != 0 || r.truncated) return std::nullopt;
    std::lock_guard<std::mutex> lock(mutex_);
    entries_[key] = Entry{r.stdout_output, Clock::now()};
    return r.stdout_output;
}

void IptablesTableCache::invalidate() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
}

namespace {

void collect_fwmark_conflicts(const HostHealthInputs& inputs,
                              FirewallBackend backend,
                              const PolicyRuleInputs& policy_rules,
                              IptablesTableCache& cache,
                              const CommandRunner& runner,
                              std::vector<HealthWarning>& warnings) {
    if (inputs.fwmark_mask == 0) return;

    std::vector<std::pair<std::string, std::string>> texts;
    if (backend == FirewallBackend::iptables) {
        for (const char* command : {"iptables", "ip6tables"}) {
            for (const char* table : {"mangle", "raw"}) {
                try {
                    if (auto text = cache.get(runner, command, table)) {
                        texts.emplace_back(table, std::move(*text));
                    }
                } catch (const std::exception& e) {
                    Logger::instance().debug(
                        "fwmark overlap check: cannot read {} {}: {}", command,
                        table, e.what());
                }
            }
        }
    } else {
        Logger::instance().debug(
            "fwmark overlap firewall check skipped: only supported on the "
            "iptables backend");
    }
    auto found = evaluate_fwmark_conflict_warnings(
        texts, policy_rules.dumped, policy_rules.own, inputs.fwmark_mask);
    warnings.insert(warnings.end(), found.begin(), found.end());
}

}  // namespace

std::vector<HealthWarning> collect_host_health_warnings(
    const HostHealthInputs& inputs,
    const std::vector<DumpedInterface>& interfaces, FirewallBackend backend,
    const PolicyRuleInputs& policy_rules, IptablesTableCache& table_cache,
    const CommandRunner& runner, const ProcFileReader& reader) {
    std::vector<HealthWarning> warnings;
    // Mark overlap breaks regular marking too, so it is not balance-only.
    collect_fwmark_conflicts(inputs, backend, policy_rules, table_cache, runner,
                             warnings);
    if (inputs.candidates.empty()) return warnings;

    if (backend == FirewallBackend::iptables) {
        try {
            const auto nat = table_cache.get(runner, "iptables", "nat");
            if (!nat) {
                Logger::instance().debug(
                    "NAT health check skipped: cannot read iptables nat table");
            } else {
                const auto lan = interface_subnets(interfaces, inputs.inbound_interfaces);
                auto nat_warnings = evaluate_nat_warnings(inputs.candidates, *nat, lan);
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

    auto rp = evaluate_rp_filter_warnings(inputs.candidates, reader);
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
