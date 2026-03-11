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
    return "MISSING";
}

std::string fwmark_hex(uint32_t v) {
    return std::format("0x{:08x}", v);
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

struct RouteKey {
    uint32_t table;
    std::optional<std::string> iface;
    std::optional<std::string> gw;

    bool operator<(const RouteKey& other) const {
        return std::tie(table, iface, gw) < std::tie(other.table, other.iface, other.gw);
    }
};

std::string format_route_brief(const RouteTableCheck& rt,
                               const std::map<RouteKey, bool>& route_is_blackhole) {
    RouteKey key{rt.table_id, rt.expected_interface, rt.expected_gateway};
    auto it = route_is_blackhole.find(key);
    const bool is_blackhole = (it != route_is_blackhole.end()) && it->second;

    std::string desc = std::format("table={} ", rt.table_id);
    if (is_blackhole) {
        desc += "blackhole default";
        return desc;
    }

    desc += "default";
    if (rt.expected_interface) {
        desc += std::format(" dev {}", *rt.expected_interface);
    }
    if (rt.expected_gateway) {
        desc += std::format(" via {}", *rt.expected_gateway);
    }
    return desc;
}

std::string format_rule_presence(const PolicyRuleCheck& pr) {
    const bool v4 = pr.rule_present_v4;
    const bool v6 = pr.rule_present_v6;
    if (v4 && v6) return "[v4+v6]";
    if (v4) return "[v4]";
    if (v6) return "[v6]";
    return "[v4+v6]";
}

void print_detail_if_needed(const std::string& detail) {
    if (!detail.empty()) {
        std::cout << "            detail: " << detail << "\n";
    }
}

} // namespace

int run_status_command(const Config& config, const std::string& config_path) {
    auto marks = allocate_outbound_marks(config.fwmark.value_or(FwmarkConfig{}),
                                         config.outbounds.value_or(std::vector<Outbound>{}));

    NetlinkManager netlink;
    RouteTable routes(netlink, true);
    PolicyRuleManager rules(netlink, true);
    populate_routing_state(config, marks, routes, rules);

    FirewallState fw_state;
    fw_state.set_outbound_marks(marks);
    fw_state.set_rules(build_fw_rule_states(config, marks));

    auto firewall = create_firewall("auto");
    RoutingHealthChecker checker(*firewall, fw_state, routes, rules, netlink);
    RoutingHealthReport report = checker.check();

    std::cout << "keen-pbr3 status - config: " << config_path << "\n";
    if (!report.firewall_backend.empty()) {
        std::cout << "Firewall backend: " << report.firewall_backend << "\n";
    } else {
        std::cout << "Firewall backend: (unknown)\n";
    }

    if (!report.error.empty()) {
        std::cout << "Error: " << report.error << "\n";
    }

    std::map<std::string, std::vector<const RouteTableCheck*>> routes_by_outbound;
    for (const auto& rt : report.route_tables) {
        routes_by_outbound[rt.outbound_tag].push_back(&rt);
    }

    std::map<std::pair<uint32_t, uint32_t>, const PolicyRuleCheck*> rules_by_mark_table;
    for (const auto& pr : report.policy_rules) {
        rules_by_mark_table[{pr.fwmark, pr.expected_table}] = &pr;
    }

    std::map<RouteKey, bool> route_is_blackhole;
    for (const auto& spec : routes.get_routes()) {
        RouteKey key{spec.table, spec.interface, spec.gateway};
        route_is_blackhole[key] = spec.blackhole;
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
                std::cout << "            route   " << format_route_brief(*rt, route_is_blackhole)
                          << "  " << check_status_label(rt->status) << "\n";
                if (rt->status != CheckStatus::ok) {
                    print_detail_if_needed(rt->detail);
                }
            }
        }

        if (fwmark != 0) {
            auto pr_it = rules_by_mark_table.find({fwmark, table_id});
            if (pr_it != rules_by_mark_table.end()) {
                const auto& pr = *pr_it->second;
                std::cout << "            rule    " << fwmark_hex(pr.fwmark)
                          << "/" << fwmark_hex(pr.fwmask)
                          << " -> table=" << pr.expected_table
                          << " pri=" << pr.priority
                          << "  " << check_status_label(pr.status) << " "
                          << format_rule_presence(pr) << "\n";
                if (pr.status != CheckStatus::ok) {
                    print_detail_if_needed(pr.detail);
                }
            } else if (routable) {
                std::cout << "            rule    " << fwmark_hex(fwmark)
                          << " -> table=" << table_id
                          << " pri=" << priority
                          << "  MISSING\n";
            }
        }
    }

    std::cout << "\nFirewall:\n";
    const bool chain_ok = report.firewall_chain.chain_present &&
                          report.firewall_chain.prerouting_hook_present;
    std::cout << "  chain   KeenPbrTable / prerouting hook  "
              << (chain_ok ? "OK" : "MISSING") << "\n";
    if (!chain_ok) {
        print_detail_if_needed(report.firewall_chain.detail);
    }

    for (const auto& fr : report.firewall_rules) {
        std::cout << "  rule    " << fr.set_name << " -> ";
        if (fr.action == "mark") {
            std::cout << "MARK " << fwmark_hex(fr.expected_fwmark.value_or(0));
        } else {
            std::cout << "DROP";
        }
        std::cout << "  " << check_status_label(fr.status) << "\n";
        if (fr.status != CheckStatus::ok) {
            print_detail_if_needed(fr.detail);
        }
    }

    int failed = 0;
    if (!chain_ok) failed++;
    for (const auto& fr : report.firewall_rules) {
        if (fr.status != CheckStatus::ok) failed++;
    }
    for (const auto& rt : report.route_tables) {
        if (rt.status != CheckStatus::ok) failed++;
    }
    for (const auto& pr : report.policy_rules) {
        if (pr.status != CheckStatus::ok) failed++;
    }

    std::cout << "\nOverall: ";
    if (!report.error.empty()) {
        std::cout << "ERROR\n";
    } else if (report.overall_ok) {
        std::cout << "OK\n";
    } else {
        std::cout << "DEGRADED (" << failed << " check(s) failed)\n";
    }

    std::cout << "Status values: OK / MISSING / MISMATCH / ERROR\n";

    return report.overall_ok ? 0 : 1;
}

} // namespace keen_pbr3
