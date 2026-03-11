#include "status.hpp"

#include "../config/routing_state.hpp"
#include "../firewall/firewall.hpp"
#include "../health/routing_health_checker.hpp"
#include "../routing/firewall_state.hpp"
#include "../routing/netlink.hpp"
#include "../routing/policy_rule.hpp"
#include "../routing/route_table.hpp"

#include <cstdint>
#include <format>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace keen_pbr3 {

namespace {

std::string check_status_label(CheckStatus s) {
    switch (s) {
        case CheckStatus::ok: return "OK";
        case CheckStatus::missing: return "MISSING";
        case CheckStatus::mismatch: return "MISMATCH";
    }
    return "ERROR";
}

std::string fwmark_hex(uint32_t v) {
    return std::format("0x{:08x}", v);
}

std::string pad_dots(const std::string& prefix,
                     const std::string& suffix,
                     size_t width = 72) {
    if (prefix.size() + suffix.size() + 1 >= width) {
        return prefix + " " + suffix;
    }
    return prefix + " " + std::string(width - prefix.size() - suffix.size() - 1, '.') + " " + suffix;
}

std::string outbound_type_label(OutboundType t) {
    switch (t) {
        case OutboundType::INTERFACE: return "interface";
        case OutboundType::TABLE: return "table";
        case OutboundType::BLACKHOLE: return "blackhole";
        case OutboundType::IGNORE: return "ignore";
        case OutboundType::URLTEST: return "urltest";
    }
    return "unknown";
}

struct UrltestRuleInfo {
    std::string urltest_tag;
    std::map<uint32_t, std::string> child_tags_by_mark;
};

struct DisplayFirewallRule {
    std::string set_name;
    std::string action;
    std::optional<uint32_t> expected_fwmark;
    std::optional<uint32_t> actual_fwmark;
    CheckStatus status{CheckStatus::missing};
    std::optional<std::string> status_label_override;
    std::string detail;
    std::optional<std::string> selected_outbound;
};

std::string format_route_brief(const RouteTableCheck& rt) {
    std::string desc = std::format("table={} ", rt.table_id);
    const std::string route_type = rt.expected_route_type.value_or("unicast");
    if (route_type == "blackhole") {
        desc += "blackhole default";
    } else if (route_type == "unreachable") {
        desc += "unreachable default";
    } else {
        desc += "default";
        if (rt.expected_interface) {
            desc += std::format(" dev {}", *rt.expected_interface);
        }
        if (rt.expected_gateway) {
            desc += std::format(" via {}", *rt.expected_gateway);
        }
    }
    if (rt.expected_metric && *rt.expected_metric != 0) {
        desc += std::format(" metric {}", *rt.expected_metric);
    }
    return desc;
}

std::string format_rule_presence(const PolicyRuleCheck& pr) {
    const bool v4 = pr.rule_present_v4;
    const bool v6 = pr.rule_present_v6;
    if (v4 && v6) return "[v4+v6]";
    if (v4) return "[v4]";
    if (v6) return "[v6]";
    return {};
}

void print_detail_if_needed(const std::string& detail, const std::string& indent = "      ") {
    if (!detail.empty()) {
        std::cout << indent << detail << "\n";
    }
}

const Outbound* find_outbound(const std::vector<Outbound>& outbounds, const std::string& tag) {
    for (const auto& ob : outbounds) {
        if (ob.tag == tag) {
            return &ob;
        }
    }
    return nullptr;
}

std::map<std::string, UrltestRuleInfo> build_urltest_rule_info_by_set(
    const Config& config,
    const OutboundMarkMap& marks) {
    std::map<std::string, UrltestRuleInfo> infos;
    const auto& outbounds = config.outbounds.value_or(std::vector<Outbound>{});
    const auto rule_states = build_fw_rule_states(config, marks);

    for (const auto& rs : rule_states) {
        const Outbound* outbound = find_outbound(outbounds, rs.outbound_tag);
        if (!outbound || outbound->type != OutboundType::URLTEST) {
            continue;
        }

        UrltestRuleInfo info;
        info.urltest_tag = outbound->tag;

        if (outbound->outbound_groups) {
            for (const auto& group : *outbound->outbound_groups) {
                for (const auto& child_tag : group.outbounds) {
                    auto mark_it = marks.find(child_tag);
                    if (mark_it != marks.end()) {
                        info.child_tags_by_mark.emplace(mark_it->second, child_tag);
                    }
                }
            }
        }

        if (info.child_tags_by_mark.empty()) {
            continue;
        }

        for (const auto& set_name : rs.set_names) {
            infos.emplace(set_name, info);
        }
    }

    return infos;
}

std::vector<DisplayFirewallRule> build_display_firewall_rules(
    const Config& config,
    const OutboundMarkMap& marks,
    const std::vector<FirewallRuleCheck>& checks) {
    std::vector<DisplayFirewallRule> display_rules;
    display_rules.reserve(checks.size());

    const auto urltest_rule_info_by_set = build_urltest_rule_info_by_set(config, marks);

    for (const auto& check : checks) {
        DisplayFirewallRule display{
            .set_name = check.set_name,
            .action = check.action,
            .expected_fwmark = check.expected_fwmark,
            .actual_fwmark = check.actual_fwmark,
            .status = check.status,
            .status_label_override = std::nullopt,
            .detail = check.detail,
        };

        auto info_it = urltest_rule_info_by_set.find(check.set_name);
        if (info_it != urltest_rule_info_by_set.end() &&
            check.action == "mark" &&
            check.actual_fwmark.has_value()) {
            const auto child_it = info_it->second.child_tags_by_mark.find(*check.actual_fwmark);
            if (child_it != info_it->second.child_tags_by_mark.end()) {
                display.selected_outbound = child_it->second;
                display.expected_fwmark = check.actual_fwmark;
                display.status = CheckStatus::ok;
                display.detail = "selected outbound: " + *display.selected_outbound;
            } else if (check.status != CheckStatus::missing) {
                display.status = CheckStatus::mismatch;
                display.status_label_override = "ERROR";
                display.detail = std::format("fwmark {} is not a child of urltest outbound {}",
                                             fwmark_hex(*check.actual_fwmark),
                                             info_it->second.urltest_tag);
            }
        }

        display_rules.push_back(std::move(display));
    }

    return display_rules;
}

int count_failed_checks(const RoutingHealthReport& report,
                        const std::vector<DisplayFirewallRule>& firewall_rules) {
    int failed = 0;

    if (!report.firewall_chain.chain_present || !report.firewall_chain.prerouting_hook_present) {
        ++failed;
    }

    for (const auto& fr : firewall_rules) {
        if (fr.status != CheckStatus::ok) {
            ++failed;
        }
    }
    for (const auto& rt : report.route_tables) {
        if (rt.status != CheckStatus::ok) {
            ++failed;
        }
    }
    for (const auto& pr : report.policy_rules) {
        if (pr.status != CheckStatus::ok) {
            ++failed;
        }
    }
    if (!report.error.empty()) {
        ++failed;
    }

    return failed;
}

void print_header(const RoutingHealthReport& report, const std::string& config_path) {
    std::cout << "keen-pbr3 status - config: " << config_path << "\n";
    if (!report.firewall_backend.empty()) {
        std::cout << "Firewall backend: " << report.firewall_backend << "\n";
    } else {
        std::cout << "Firewall backend: (unknown)\n";
    }

    if (!report.error.empty()) {
        std::cout << "Health check error: " << report.error << "\n";
    }
}

void print_outbound_section(const Config& config,
                            const OutboundMarkMap& marks,
                            const RouteTable& routes,
                            const RoutingHealthReport& report) {
    std::map<std::string, std::vector<const RouteTableCheck*>> routes_by_outbound;
    for (const auto& rt : report.route_tables) {
        routes_by_outbound[rt.outbound_tag].push_back(&rt);
    }

    std::map<std::pair<uint32_t, uint32_t>, const PolicyRuleCheck*> rules_by_mark_table;
    for (const auto& pr : report.policy_rules) {
        rules_by_mark_table[{pr.fwmark, pr.expected_table}] = &pr;
    }

    std::cout << "\nOutbounds:\n";

    uint32_t table_start = static_cast<uint32_t>(
        config.iproute.value_or(IprouteConfig{}).table_start.value_or(100));
    uint32_t table_offset = 0;

    const auto& outbounds = config.outbounds.value_or(std::vector<Outbound>{});
    for (const auto& ob : outbounds) {
        const bool routable =
            (ob.type == OutboundType::INTERFACE ||
             ob.type == OutboundType::TABLE ||
             ob.type == OutboundType::URLTEST);

        uint32_t table_id = 0;
        uint32_t priority = 0;

        if (routable) {
            if (ob.type == OutboundType::TABLE) {
                table_id = static_cast<uint32_t>(ob.table.value_or(0));
                priority = table_start + table_offset;
            } else {
                table_id = table_start + table_offset;
                priority = table_id;
            }
            ++table_offset;
        }

        uint32_t fwmark = 0;
        auto mark_it = marks.find(ob.tag);
        if (mark_it != marks.end()) {
            fwmark = mark_it->second;
        }

        std::cout << "  " << ob.tag << " [" << outbound_type_label(ob.type) << "]";
        if (ob.type == OutboundType::INTERFACE) {
            std::cout << " iface=" << ob.interface.value_or("")
                      << " fwmark=" << fwmark_hex(fwmark)
                      << " table=" << table_id;
        } else if (ob.type == OutboundType::TABLE) {
            std::cout << " table=" << table_id
                      << " fwmark=" << fwmark_hex(fwmark);
        } else if (ob.type == OutboundType::URLTEST) {
            std::cout << " fwmark=" << fwmark_hex(fwmark)
                      << " table=" << table_id;
        }
        std::cout << "\n";

        auto rt_it = routes_by_outbound.find(ob.tag);
        if (rt_it != routes_by_outbound.end()) {
            for (const auto* rt : rt_it->second) {
                const std::string route_desc = "route   " + format_route_brief(*rt);
                std::cout << "    " << pad_dots(route_desc, check_status_label(rt->status)) << "\n";
                if (rt->status != CheckStatus::ok) {
                    print_detail_if_needed(rt->detail, "      ");
                }
            }
        }

        if (fwmark != 0) {
            auto pr_it = rules_by_mark_table.find({fwmark, table_id});
            if (pr_it != rules_by_mark_table.end()) {
                const auto& pr = *pr_it->second;
                std::string suffix = check_status_label(pr.status);
                const auto families = format_rule_presence(pr);
                if (!families.empty()) {
                    suffix += " " + families;
                }
                const std::string rule_desc = std::format("rule    {}/{} -> table={} pri={}",
                                                          fwmark_hex(pr.fwmark),
                                                          fwmark_hex(pr.fwmask),
                                                          pr.expected_table,
                                                          pr.priority);
                std::cout << "    " << pad_dots(rule_desc, suffix) << "\n";
                if (pr.status != CheckStatus::ok) {
                    print_detail_if_needed(pr.detail, "      ");
                }
            } else if (routable) {
                const std::string rule_desc = std::format("rule    {} -> table={} pri={}",
                                                          fwmark_hex(fwmark),
                                                          table_id,
                                                          priority);
                std::cout << "    " << pad_dots(rule_desc, "MISSING") << "\n";
            }
        }
    }
}

void print_firewall_section(const std::vector<DisplayFirewallRule>& firewall_rules,
                            const RoutingHealthReport& report) {
    std::cout << "\nFirewall:\n";
    const bool chain_ok = report.firewall_chain.chain_present &&
                          report.firewall_chain.prerouting_hook_present;
    std::cout << "  "
              << pad_dots("chain   KeenPbrTable / prerouting hook",
                          chain_ok ? "OK" : "MISSING")
              << "\n";
    if (!chain_ok) {
        print_detail_if_needed(report.firewall_chain.detail, "    ");
    }

    for (const auto& fr : firewall_rules) {
        std::string rule_desc = "rule    " + fr.set_name + " -> ";
        if (fr.action == "mark") {
            rule_desc += "MARK " + fwmark_hex(fr.expected_fwmark.value_or(0));
            if (fr.selected_outbound) {
                rule_desc += " selected=" + *fr.selected_outbound;
            }
        } else {
            rule_desc += "DROP";
        }
        const std::string status_label =
            fr.status_label_override.value_or(check_status_label(fr.status));
        std::cout << "  " << pad_dots(rule_desc, status_label) << "\n";
        if (fr.status != CheckStatus::ok) {
            print_detail_if_needed(fr.detail, "    ");
        }
    }
}

void print_overall_summary(const RoutingHealthReport& report,
                           const std::vector<DisplayFirewallRule>& firewall_rules) {
    const int failed = count_failed_checks(report, firewall_rules);
    std::cout << "\nOverall: ";
    if (!report.error.empty()) {
        std::cout << "ERROR\n";
    } else if (failed == 0) {
        std::cout << "OK\n";
    } else {
        std::cout << "DEGRADED (" << failed << " check(s) failed)\n";
    }

    std::cout << "Status values: OK / MISSING / MISMATCH / ERROR\n";
}

} // namespace

int run_status_command(const Config& config, const std::string& config_path) {
    auto marks = allocate_outbound_marks(config.fwmark.value_or(FwmarkConfig{}),
                                         config.outbounds.value_or(std::vector<Outbound>{}));

    NetlinkManager netlink;
    RouteTable routes(netlink, true);
    PolicyRuleManager rules(netlink, true);
    populate_routing_state(
        config,
        marks,
        routes,
        rules,
        [&netlink](const Outbound& outbound) {
            return is_interface_outbound_reachable(outbound, netlink);
        },
        nullptr);

    FirewallState fw_state;
    fw_state.set_outbound_marks(marks);
    fw_state.set_rules(build_fw_rule_states(config, marks));

    auto firewall = create_firewall("auto");
    RoutingHealthChecker checker(*firewall, fw_state, routes, rules, netlink);
    RoutingHealthReport report = checker.check();
    const auto display_firewall_rules = build_display_firewall_rules(config, marks, report.firewall_rules);

    print_header(report, config_path);
    print_outbound_section(config, marks, routes, report);
    print_firewall_section(display_firewall_rules, report);
    print_overall_summary(report, display_firewall_rules);

    return count_failed_checks(report, display_firewall_rules) == 0 ? 0 : 1;
}

} // namespace keen_pbr3
