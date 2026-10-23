#include "test_routing_report.hpp"

#include <algorithm>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace keen_pbr3 {

namespace {

using term::Color;
using term::Glyph;

enum class Tone { Ok, Warn, Neutral };

// Why the decision stopped at a rule that could not be evaluated.
struct Undetermined {
    std::string key;
    int rule_number{0};
    std::string outbound;
    std::vector<std::string> fields; // empty: only the router's routes are missing
};

// One evaluated address with everything the steps read about it.
struct Row {
    const TestRoutingEntry* entry{nullptr};
    const RuleDiagnostic* rule{nullptr};
    const RuleIpDiagnostic* ip_row{nullptr};
    std::optional<ListMatchInfo> list_match;
    std::optional<Undetermined> undetermined;
};

using RowGroup = std::vector<const Row*>;
using RowGroups = std::vector<std::pair<std::string, RowGroup>>;

RowGroup& group_for(RowGroups& groups, const std::string& key) {
    for (auto& group : groups) {
        if (group.first == key) return group.second;
    }
    groups.emplace_back(key, RowGroup{});
    return groups.back().second;
}

bool is_ipv6(const std::string& ip) {
    return ip.find(':') != std::string::npos;
}

std::string join(const std::vector<std::string>& items, const std::string& separator) {
    std::string out;
    for (const auto& item : items) {
        if (!out.empty()) out += separator;
        out += item;
    }
    return out;
}

const RuleIpDiagnostic* find_ip_row(const RuleDiagnostic& rule, const std::string& ip) {
    const auto it = std::find_if(rule.ip_rows.begin(), rule.ip_rows.end(),
                                 [&](const RuleIpDiagnostic& row) { return row.ip == ip; });
    return it == rule.ip_rows.end() ? nullptr : &*it;
}

std::string field_flag(const std::string& field) {
    if (field == "src_addr") return "--src-addr";
    if (field == "src_port") return "--src-port";
    if (field == "dest_port") return "--dest-port";
    return "--" + field;
}

std::string field_name(const std::string& field) {
    if (field == "src_addr") return "Source IP";
    if (field == "src_port") return "Source port";
    if (field == "proto") return "Protocol";
    if (field == "dest_port") return "Destination port";
    return field;
}

std::string with_and(const std::vector<std::string>& items) {
    if (items.size() < 2) return join(items, "");
    std::vector<std::string> head(items.begin(), items.end() - 1);
    return join(head, ", ") + " and " + items.back();
}

class Report {
public:
    Report(const TestRoutingResult& result, const TestRoutingCriteria& criteria,
           const term::Style& style)
        : result_(result), criteria_(criteria), style_(style) {}

    std::string build();

private:
    struct Step {
        Tone tone{Tone::Neutral};
        std::string title;
        std::vector<std::string> lines; // details, one per line
        std::string compact;            // the one-line form when everything is OK
    };

    std::string arrow() const { return "->"; }
    std::string apostrophe() const { return "'"; }
    std::string quoted(const std::string& text) const {
        return "\"" + text + "\"";
    }
    std::string dot() const { return style_.glyph(Glyph::Dot); }

    std::string label(const std::string& tag) const {
        if (tag == "(default)") return "System route";
        if (tag == "(unknown)") return "Unknown";
        return tag;
    }
    std::string rule_text(int number, const std::string& outbound) const {
        return "Rule #" + std::to_string(number) + " " + arrow() + " " + outbound;
    }
    std::string ips_of(const RowGroup& group) const {
        std::vector<std::string> ips;
        for (const Row* row : group) ips.push_back(row->entry->ip);
        return join(ips, ", ");
    }
    // "ip, ip:" above a block that covers only some of the addresses.
    void add_group_label(std::vector<std::string>& lines, const RowGroup& group) const {
        if (group.size() < rows_.size()) {
            lines.push_back(style_.paint(Color::Dim, ips_of(group) + ":"));
        }
    }

    std::string tone_glyph(Tone tone) const {
        switch (tone) {
        case Tone::Ok: return term::painted_glyph(style_, Glyph::Ok);
        case Tone::Warn: return term::painted_glyph(style_, Glyph::Warn);
        case Tone::Neutral: return term::painted_glyph(style_, Glyph::Dot);
        }
        return "";
    }

    void prepare();
    std::optional<Undetermined> undetermined_for(const TestRoutingEntry& entry) const;
    std::optional<ListMatchInfo> list_match_for(const TestRoutingEntry& entry) const;
    std::string gateway_skip_text(const TestRoutingEntry& entry) const;
    std::string ipv6_note() const;
    std::string outbound_summary(const TestRoutingEntry& entry, const std::string& tag) const;

    Step dns_step() const;
    Step list_step() const;
    Step rule_step() const;
    Step firewall_step() const;
    void add_issue_sections(std::vector<std::string>& out) const;
    std::string heading() const;
    std::string issue_title(RoutingDiagnosis code) const;

    const TestRoutingResult& result_;
    const TestRoutingCriteria& criteria_;
    const term::Style& style_;

    std::vector<Row> rows_;
    std::vector<std::string> skipped_ipv6_;
    RowGroups issue_groups_;
    bool dns_missing_{false};
    bool ipv6_off_{false};
    bool all_ok_{false};
    bool rule_unknown_{false};
};

std::optional<Undetermined> Report::undetermined_for(const TestRoutingEntry& entry) const {
    if (!entry.undetermined_rule_index) return std::nullopt;
    const int index = *entry.undetermined_rule_index;
    std::string outbound = "(unknown)";
    for (const auto& rule : result_.rule_diagnostics) {
        if (rule.rule_index == index) outbound = rule.outbound;
    }
    Undetermined out;
    out.rule_number = index + 1;
    out.outbound = label(outbound);
    if (entry.default_gateway_directness) {
        out.key = std::to_string(out.rule_number) + "/gateway";
        return out;
    }
    if (entry.missing_criteria.empty()) return std::nullopt;
    out.fields = entry.missing_criteria;
    out.key = std::to_string(out.rule_number) + "/" + join(entry.missing_criteria, ",");
    return out;
}

std::optional<ListMatchInfo> Report::list_match_for(const TestRoutingEntry& entry) const {
    for (const auto& item : result_.rule_diagnostics) {
        if (route_rule_enabled(item.rule) && item.outbound == entry.expected_outbound &&
            item.target_match) {
            return item.target_match;
        }
    }
    if (entry.list_match) return entry.list_match;
    for (const auto& item : result_.rule_diagnostics) {
        if (!route_rule_enabled(item.rule)) continue;
        const RuleIpDiagnostic* row = find_ip_row(item, entry.ip);
        if (row != nullptr && row->in_lists && row->list_match) return row->list_match;
    }
    return std::nullopt;
}

void Report::prepare() {
    dns_missing_ = result_.is_domain && result_.resolved_ips.empty();
    ipv6_off_ = !result_.ipv6_enabled;
    if (ipv6_off_) {
        if (result_.is_domain) {
            for (const auto& ip : result_.resolved_ips) {
                if (is_ipv6(ip)) skipped_ipv6_.push_back(ip);
            }
        } else if (is_ipv6(result_.target)) {
            skipped_ipv6_.push_back(result_.target);
        }
    }

    rows_.reserve(result_.entries.size());
    for (const auto& entry : result_.entries) {
        if (ipv6_off_ && is_ipv6(entry.ip)) continue;
        Row row;
        row.entry = &entry;
        row.rule = find_expected_rule(result_, entry);
        row.ip_row = row.rule != nullptr ? find_ip_row(*row.rule, entry.ip) : nullptr;
        row.list_match = list_match_for(entry);
        row.undetermined = undetermined_for(entry);
        rows_.push_back(std::move(row));
    }
    for (const Row& row : rows_) {
        if (row.entry->diagnosis != RoutingDiagnosis::Ok) {
            group_for(issue_groups_, routing_diagnosis_name(row.entry->diagnosis))
                .push_back(&row);
        }
        rule_unknown_ = rule_unknown_ || row.entry->diagnosis == RoutingDiagnosis::Criteria ||
                        row.entry->diagnosis == RoutingDiagnosis::NetworkState;
    }
    all_ok_ = !rows_.empty() &&
              std::all_of(rows_.begin(), rows_.end(), [](const Row& row) {
                  return row.entry->diagnosis == RoutingDiagnosis::Ok;
              });
}

std::string Report::ipv6_note() const {
    if (skipped_ipv6_.empty()) return "";
    return "These addresses are not checked: IPv6 support is disabled";
}

Report::Step Report::dns_step() const {
    Step step;
    step.title = "DNS resolution";
    step.tone = dns_missing_ ? Tone::Warn : (result_.is_domain ? Tone::Ok : Tone::Neutral);
    const std::string client_note = style_.paint(
        Color::Dim,
        "This check uses the router's DNS. Devices that use DoH, DoT or their own DNS "
        "server may get different addresses.");
    if (dns_missing_) {
        step.lines.push_back("DNS returned no IP addresses for this domain.");
        step.lines.push_back(client_note);
        step.compact = "no IP addresses";
        return step;
    }
    if (result_.is_domain) {
        std::string ips;
        for (const auto& ip : result_.resolved_ips) {
            if (!ips.empty()) ips += "  ";
            ips += (ipv6_off_ && is_ipv6(ip)) ? style_.paint(Color::Dim, ip) : ip;
        }
        step.compact = ips;
        step.lines.push_back(ips);
        if (!skipped_ipv6_.empty()) step.lines.push_back(ipv6_note());
        if (result_.dns && result_.dns->resolver == "dnsmasq") {
            std::string line = "Resolved by dnsmasq " + result_.dns->endpoint.value_or("");
            if (result_.dns->instance) line += " (" + *result_.dns->instance + ")";
            step.lines.push_back(line);
        } else if (result_.dns && result_.dns->resolver == "system") {
            step.lines.push_back("Resolved by the router" + apostrophe() + "s system resolver");
        }
        step.lines.push_back(client_note);
        return step;
    }
    step.compact = "IP " + result_.target + " was supplied directly; no DNS query is needed.";
    step.lines.push_back(step.compact);
    if (!skipped_ipv6_.empty()) step.lines.push_back(ipv6_note());
    return step;
}

Report::Step Report::list_step() const {
    Step step;
    step.title = "List matching";
    step.tone = Tone::Neutral;
    RowGroups groups;
    for (const Row& row : rows_) {
        std::string key = "none";
        if (row.list_match) {
            key = row.list_match->list_name + "\n" + row.list_match->via;
            step.tone = Tone::Ok;
        } else if (row.rule != nullptr && route_rule_lists(row.rule->rule).empty()) {
            key = "criteria";
        }
        group_for(groups, key).push_back(&row);
    }
    std::vector<std::string> compact;
    for (const auto& [key, group] : groups) {
        const Row& first = *group.front();
        std::string text = "No match";
        if (first.list_match) {
            text = first.list_match->list_name + " (entry " + first.list_match->via + ")";
        } else if (key == "criteria") {
            text = "This rule uses other criteria, without a list.";
        }
        if (groups.size() > 1) {
            step.lines.push_back(style_.paint(Color::Dim, ips_of(group) + ":") + " " + text);
            compact.push_back(ips_of(group) + ": " + text);
        } else {
            step.lines.push_back(text);
            compact.push_back(text);
        }
    }
    step.compact = join(compact, "; ");
    return step;
}

std::string Report::gateway_skip_text(const TestRoutingEntry& entry) const {
    const auto& gateway = entry.default_gateway;
    if (!gateway || gateway->uses_default_gateway != false || !gateway->bypass) return "";
    return "Rule #" + std::to_string(gateway->rule_index + 1) +
           " (default gateway) skipped: " + describe_bypass(entry.ip, *gateway->bypass);
}

Report::Step Report::rule_step() const {
    Step step;
    step.title = "Rule selection";
    step.tone = rule_unknown_
                    ? Tone::Warn
                    : (std::any_of(rows_.begin(), rows_.end(),
                                   [](const Row& row) { return row.rule != nullptr; })
                           ? Tone::Ok
                           : Tone::Neutral);

    RowGroups groups;
    for (const Row& row : rows_) {
        const auto& entry = *row.entry;
        const auto& gateway = entry.default_gateway;
        std::string key = std::to_string(row.rule != nullptr ? row.rule->rule_index : -1) + "|" +
                          entry.expected_outbound + "|" +
                          (row.undetermined ? row.undetermined->key : "") + "|";
        if (gateway) {
            key += gateway->uses_default_gateway
                       ? (*gateway->uses_default_gateway ? "uses" : "skips")
                       : "unknown";
            if (gateway->bypass) {
                key += "|" + gateway->bypass->cidr + "|" +
                       std::to_string(static_cast<int>(gateway->bypass->source)) + "|" +
                       gateway->bypass->interface.value_or("");
            }
            // A skipped rule names the address in its sentence.
            if (gateway->uses_default_gateway == false) key += "|" + entry.ip;
        }
        group_for(groups, key).push_back(&row);
    }

    std::vector<std::string> compact;
    for (const auto& [key, group] : groups) {
        const Row& first = *group.front();
        const auto& entry = *first.entry;
        std::vector<std::string> lines;
        const std::string skip = first.undetermined ? "" : gateway_skip_text(entry);
        if (!skip.empty()) lines.push_back(skip);

        if (first.undetermined) {
            const auto& u = *first.undetermined;
            if (u.fields.empty()) {
                lines.push_back(
                    "Rule #" + std::to_string(u.rule_number) + " (" + arrow() + " " + u.outbound +
                    ") applies to traffic that goes through the default gateway. keen-pbr couldn" +
                    apostrophe() + "t read the router" + apostrophe() + "s routes, so it can" +
                    apostrophe() + "t tell whether " +
                    (group.size() == 1 ? entry.ip : std::string("these addresses")) + " does.");
            } else {
                std::vector<std::string> names;
                std::vector<std::string> flags;
                for (const auto& field : u.fields) {
                    names.push_back(quoted(field_name(field)));
                    flags.push_back(field_flag(field));
                }
                lines.push_back("Pass " + join(flags, " ") + " to pin down the rule.");
                lines.push_back(
                    style_.paint(Color::Dim,
                                 "Rule #" + std::to_string(u.rule_number) + " (" + arrow() + " " +
                                     u.outbound + ") checks " + with_and(names) +
                                     ". Until " + (names.size() == 1 ? "that field is" : "those fields are") +
                                     " filled in, there is no telling whether the traffic matches it."));
            }
        } else if (first.rule != nullptr && entry.default_gateway &&
                   entry.default_gateway->uses_default_gateway == true &&
                   entry.default_gateway->rule_index == first.rule->rule_index) {
            lines.push_back(rule_text(first.rule->rule_index + 1, label(entry.expected_outbound)) +
                            ": " +
                            (group.size() == 1 ? entry.ip : std::string("these addresses")) +
                            (group.size() == 1 ? " goes" : " go") +
                            " through the default gateway");
        } else if (first.rule != nullptr) {
            lines.push_back(rule_text(first.rule->rule_index + 1, label(entry.expected_outbound)));
        } else if (entry.expected_outbound == "(default)") {
            lines.push_back("No rule matched. The system route is used.");
        } else {
            lines.push_back("There is not enough information to select a rule reliably.");
        }

        add_group_label(step.lines, group);
        for (const auto& line : lines) step.lines.push_back(line);
        const std::string summary = join(lines, "; ");
        compact.push_back(groups.size() > 1 ? ips_of(group) + ": " + summary : summary);
    }
    step.compact = join(compact, "; ");
    return step;
}

// "priority, uses home (vpn3sad)" / "balance: wan1 83% - wan2 17%" for the
// group the traffic leaves through; empty for a plain outbound.
std::string Report::outbound_summary(const TestRoutingEntry& entry, const std::string& tag) const {
    if (!entry.outbound_group || entry.outbound_group->tag != tag) return "";
    const auto& group = *entry.outbound_group;
    if (group.strategy == "balance") {
        std::vector<std::string> members;
        for (const auto& member : group.members) {
            members.push_back(member.tag + " " +
                              (member.in_use
                                   ? std::to_string(member.share_percent.value_or(0)) + "%"
                                   : std::string("not in use")));
        }
        const bool any_in_use = std::any_of(group.members.begin(), group.members.end(),
                                            [](const OutboundGroupMember& m) { return m.in_use; });
        if (!any_in_use) {
            return "balance: no member is in use, so the group" + apostrophe() +
                   "s fallback applies";
        }
        return "balance: " + join(members, " " + dot() + " ");
    }
    for (const auto& member : group.members) {
        if (member.in_use) {
            return "priority, uses " + member.tag +
                   (member.interface_name ? " (" + *member.interface_name + ")" : std::string());
        }
    }
    return "priority, no member is selected";
}

Report::Step Report::firewall_step() const {
    Step step;
    step.title = "Firewall check";
    step.tone = all_ok_ ? Tone::Ok
                        : (std::all_of(rows_.begin(), rows_.end(),
                                       [](const Row& row) {
                                           return row.entry->diagnosis == RoutingDiagnosis::Criteria ||
                                                  row.entry->diagnosis == RoutingDiagnosis::NetworkState;
                                       })
                               ? Tone::Neutral
                               : Tone::Warn);
    const auto ok_count = std::count_if(rows_.begin(), rows_.end(), [](const Row& row) {
        return row.entry->diagnosis == RoutingDiagnosis::Ok;
    });
    step.title += " " + dot() + " " + std::to_string(ok_count) + " of " +
                  std::to_string(rows_.size()) + (rows_.size() == 1 ? " IP matches" : " IPs match");

    RowGroups groups;
    for (const Row& row : rows_) {
        const auto& entry = *row.entry;
        std::string key = entry.expected_outbound + "|" + entry.actual_outbound + "|" +
                          routing_diagnosis_name(entry.diagnosis) + "|";
        if (entry.outbound_group) {
            key += entry.outbound_group->tag + "/" + entry.outbound_group->strategy;
            for (const auto& member : entry.outbound_group->members) {
                key += "/" + member.tag + ":" + (member.in_use ? "1" : "0") + ":" +
                       std::to_string(member.share_percent.value_or(0));
            }
        }
        if (entry.default_gateway && entry.default_gateway->applied_uses_default_gateway) {
            key += *entry.default_gateway->applied_uses_default_gateway ? "|applied" : "|not-applied";
        }
        group_for(groups, key).push_back(&row);
    }

    std::vector<std::string> compact;
    for (const auto& [key, group] : groups) {
        const Row& first = *group.front();
        const auto& entry = *first.entry;
        const bool ok = entry.diagnosis == RoutingDiagnosis::Ok;
        const bool plural = group.size() > 1;
        compact.push_back(label(entry.expected_outbound));

        if (groups.size() > 1) {
            const bool unknown = entry.actual_outbound == "(unknown)" ||
                                 entry.expected_outbound == "(unknown)";
            const std::string verdict = ok ? "Matches" : (unknown ? "Unknown" : "Mismatch");
            step.lines.push_back(style_.paint(Color::Dim, ips_of(group) + ":") + " " +
                                 style_.paint(ok ? Color::Green : Color::Yellow, verdict));
        }

        const std::string expected = label(entry.expected_outbound);
        const std::string actual = label(entry.actual_outbound);
        const std::string served =
            (entry.actual_outbound != "(unknown)" && entry.actual_outbound != "(default)")
                ? entry.actual_outbound
                : entry.expected_outbound;
        const size_t width = std::max(expected.size(), actual.size()) + 2;
        const auto outbound_line = [&](const std::string& name, const std::string& tag,
                                       const std::string& text) {
            std::string line = name + std::string(10 - name.size(), ' ') +
                               style_.paint(Color::Bold, text);
            const std::string summary = outbound_summary(entry, tag);
            if (!summary.empty()) {
                line += std::string(width - text.size(), ' ') + style_.paint(Color::Dim, summary);
            }
            return line;
        };
        step.lines.push_back(outbound_line("Expected", entry.expected_outbound == served ? served : "",
                                           expected));
        step.lines.push_back(outbound_line("Actual", entry.actual_outbound == served ? served : "",
                                           actual));

        const auto& gateway = entry.default_gateway;
        if (gateway && gateway->uses_default_gateway && gateway->applied_uses_default_gateway &&
            *gateway->uses_default_gateway != *gateway->applied_uses_default_gateway) {
            step.lines.push_back(
                dot() + " " +
                (*gateway->applied_uses_default_gateway
                     ? std::string("The firewall still treats ") +
                           (plural ? "these addresses" : "this address") +
                           " as default-gateway traffic, based on the routes it last applied."
                     : std::string("The firewall does not treat ") +
                           (plural ? "these addresses" : "this address") +
                           " as default-gateway traffic yet, based on the routes it last applied."));
        }

        const bool has_rows = std::any_of(group.begin(), group.end(),
                                          [](const Row* row) { return row->ip_row != nullptr; });
        if (!ok && has_rows) {
            if (std::any_of(group.begin(), group.end(), [](const Row* row) {
                    return row->ip_row != nullptr && row->ip_row->in_ipset == false;
                })) {
                step.lines.push_back(dot() + (plural ? " The IPs are not in this rule" + apostrophe() +
                                                           "s ipset."
                                                     : " The IP is not in this rule" + apostrophe() +
                                                           "s ipset."));
            }
            std::set<std::string> seen;
            for (const Row* row : group) {
                if (row->ip_row == nullptr) continue;
                const auto& evidence = row->ip_row->set_write_evidence;
                const int status = evidence ? static_cast<int>(evidence->status) : -1;
                const std::string evidence_key =
                    std::to_string(status) + "|" +
                    (evidence && evidence->age_seconds ? std::to_string(*evidence->age_seconds) : "") +
                    "|" + (row->ip_row->in_ipset ? (*row->ip_row->in_ipset ? "1" : "0") : "n");
                if (!seen.insert(evidence_key).second) continue;
                std::string text;
                if (!evidence) {
                    text = "Could not get the write history.";
                } else if (evidence->status == SetWriteEvidenceStatus::Recorded) {
                    text = plural ? "keen-pbr has already added or refreshed these IPs in the ipset."
                                  : "keen-pbr has already added or refreshed this IP in the ipset.";
                    if (evidence->age_seconds) {
                        text += " Last time " + std::to_string(*evidence->age_seconds) + " s ago.";
                    }
                    if (row->ip_row->in_ipset == false) {
                        text += plural ? " They are not in the ipset now, and why they left is unknown."
                                       : " It is not in the ipset now, and why it left is unknown.";
                    }
                } else if (evidence->status == SetWriteEvidenceStatus::NoRecord) {
                    text = plural ? "keen-pbr has no record of adding them."
                                  : "keen-pbr has no record of adding this IP to the ipset.";
                } else {
                    text = "Writes are not tracked for this ipset.";
                }
                step.lines.push_back(dot() + " " + text);
            }
        }
    }
    std::vector<std::string> unique;
    for (const auto& item : compact) {
        if (std::find(unique.begin(), unique.end(), item) == unique.end()) unique.push_back(item);
    }
    step.compact = join(unique, ", ");
    return step;
}

std::string Report::issue_title(RoutingDiagnosis code) const {
    switch (code) {
    case RoutingDiagnosis::Ok: return "The route matches the rule";
    case RoutingDiagnosis::Dns: return "No IP address resolved";
    case RoutingDiagnosis::Criteria: return "The route cannot be determined yet";
    case RoutingDiagnosis::NetworkState: return "Route depends on the network state";
    case RoutingDiagnosis::Firewall: return "The actual route is unknown";
    case RoutingDiagnosis::MissingIpset: return "IP not in the expected ipset";
    case RoutingDiagnosis::OtherIpset: return "IP found in another IPSet";
    case RoutingDiagnosis::ConflictingIpsets: return "IP present in multiple IPSets";
    case RoutingDiagnosis::StaleIpset: return "IPSet differs from list contents";
    case RoutingDiagnosis::Mismatch: return "The route differs from the expected route";
    }
    return "";
}

std::string Report::heading() const {
    if (ipv6_off_ && rows_.empty() && !dns_missing_) return "IPv6 is turned off";
    if (all_ok_) {
        return rows_.size() > 1 ? "The routes match the rules" : "The route matches the rule";
    }
    if (issue_groups_.size() == 1) {
        return issue_title(result_.entries.empty()
                               ? RoutingDiagnosis::Mismatch
                               : issue_groups_.front().second.front()->entry->diagnosis);
    }
    return "Route problems detected";
}

// What the verdict means and what to do about it, per kind of problem.
void Report::add_issue_sections(std::vector<std::string>& out) const {
    for (const auto& [name, group] : issue_groups_) {
        const Row& first = *group.front();
        const RoutingDiagnosis code = first.entry->diagnosis;
        // The rule step already explains this once.
        if (code == RoutingDiagnosis::NetworkState && issue_groups_.size() == 1) continue;

        std::vector<std::string> lines;
        if (issue_groups_.size() > 1) {
            lines.push_back("  " + style_.paint(Color::Bold, issue_title(code)));
        }
        if (group.size() < rows_.size()) {
            lines.push_back("  " + style_.paint(Color::Dim, ips_of(group) + ":"));
        }
        const std::string indent = "  ";
        switch (code) {
        case RoutingDiagnosis::NetworkState:
            break;
        case RoutingDiagnosis::Criteria:
            if (first.undetermined && !first.undetermined->fields.empty()) {
                std::vector<std::string> flags;
                for (const auto& field : first.undetermined->fields) {
                    flags.push_back(field_flag(field));
                }
                lines.push_back(indent + "Pass " + join(flags, " ") + " to pin down the rule.");
            } else {
                lines.push_back(indent +
                                "Packet criteria are incomplete or the address families are incompatible.");
                lines.push_back(indent +
                                "Provide source IP, source port and DSCP if required by the rules.");
            }
            break;
        case RoutingDiagnosis::MissingIpset: {
            // DNS advice only fits addresses learned through a domain, not
            // entries that a static IP list put into the set.
            const bool learned_from_domain =
                result_.is_domain &&
                std::any_of(group.begin(), group.end(), [](const Row* row) {
                    return row->list_match && row->list_match->via != row->entry->ip;
                });
            if (learned_from_domain) {
                lines.push_back(indent + "The domain is in the list, but its IPs are not in the rule" +
                                apostrophe() + "s ipset yet. keen-pbr adds addresses when a client looks "
                                "the domain up through the router" + apostrophe() + "s DNS.");
                lines.push_back(indent + "  1. Open this site on the client device and run the check again.");
                lines.push_back(indent + "  2. Still missing " + std::string("-") +
                                " clear the DNS cache on the device and in the browser, then try again.");
                lines.push_back(indent + "  3. Still missing " + std::string("-") +
                                " make sure the device does not use third-party DNS (DoH, DoT or its "
                                "own DNS server) that keen-pbr cannot intercept. To check, open the "
                                "keen-pbr home page from that device and look at the " +
                                quoted("Domain-based routing") + " section, and see the requests log.");
            } else {
                lines.push_back(indent + "The address is in the list but not in the selected rule" +
                                apostrophe() + "s ipset.");
                lines.push_back(indent + "Check applied configuration and list updates, then try again.");
            }
            break;
        }
        case RoutingDiagnosis::Dns:
            lines.push_back(indent + "Without an IP address, firewall membership cannot be checked.");
            lines.push_back(indent + "Check the domain and the router" + apostrophe() +
                            "s DNS, then try again.");
            break;
        case RoutingDiagnosis::Firewall:
            lines.push_back(indent + "The sets or rule conditions on the router could not be checked.");
            lines.push_back(indent +
                            "Check the service status and the Diagnostics section below. Since the check "
                            "failed, it is not certain that the IP is missing from the set.");
            break;
        case RoutingDiagnosis::OtherIpset:
            lines.push_back(indent + "The address is absent from the expected set but present in another "
                            "rule" + apostrophe() + "s set for the actual outbound.");
            break;
        case RoutingDiagnosis::ConflictingIpsets:
            lines.push_back(indent + "The address is in the expected set and another rule" + apostrophe() +
                            "s set, but the firewall chooses a different outbound.");
            lines.push_back(indent +
                            "An earlier rule may have priority. Check rule order and current set contents.");
            break;
        case RoutingDiagnosis::StaleIpset:
            lines.push_back(indent + "The address is in a set for the actual outbound but does not match "
                            "that rule" + apostrophe() + "s current lists.");
            lines.push_back(indent +
                            "Check list updates and applied configuration. The set entry may be stale.");
            break;
        case RoutingDiagnosis::Mismatch:
            lines.push_back(indent + "The expected and firewall outbounds differ. There is not enough "
                            "evidence to identify the exact cause.");
            lines.push_back(indent + "Check applied configuration, rule order and detailed diagnostics.");
            break;
        case RoutingDiagnosis::Ok:
            break;
        }
        if (lines.empty()) continue;
        out.emplace_back("");
        for (auto& line : lines) out.push_back(std::move(line));
    }
}

std::string Report::build() {
    prepare();

    std::vector<std::string> out;
    const bool nothing_checked = ipv6_off_ && rows_.empty() && !dns_missing_;
    const Tone verdict = all_ok_ ? Tone::Ok : (nothing_checked ? Tone::Neutral : Tone::Warn);
    out.push_back(tone_glyph(verdict) + " " + style_.paint(Color::Bold, heading()) + " " + dot() +
                  " " + result_.target);
    if (criteria_.has_any()) {
        out.push_back(style_.paint(Color::Dim, "Packet: " + describe_packet_criteria(criteria_)));
    }
    out.emplace_back("");

    std::vector<Step> steps;
    steps.push_back(dns_step());
    steps.push_back(list_step());
    steps.push_back(rule_step());
    if (!dns_missing_ && !rows_.empty()) steps.push_back(firewall_step());
    if (rows_.empty()) steps.resize(1);

    for (const Step& step : steps) {
        if (all_ok_) {
            out.push_back("  " + tone_glyph(step.tone) + " " + step.title +
                          (step.compact.empty() ? "" : " " + dot() + " " + step.compact));
            continue;
        }
        out.push_back("  " + tone_glyph(step.tone) + " " + step.title);
        for (const auto& line : step.lines) out.push_back("      " + line);
    }

    if (rows_.empty() && !skipped_ipv6_.empty() && !dns_missing_) {
        out.emplace_back("");
        out.push_back("  " + ipv6_note());
    }
    add_issue_sections(out);
    if (all_ok_) {
        out.emplace_back("");
        out.push_back("  " + style_.paint(Color::Dim,
                                          "This checks the route from firewall state, not website connectivity."));
    }

    std::string text;
    for (const auto& line : out) text += line + "\n";
    return text;
}

} // namespace

std::string format_test_routing_report(const TestRoutingResult& result,
                                       const TestRoutingCriteria& criteria,
                                       const term::Style& style) {
    return Report(result, criteria, style).build();
}

} // namespace keen_pbr3
