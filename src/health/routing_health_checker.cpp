#include "routing_health_checker.hpp"

#include "../firewall/firewall_verifier.hpp"
#include "../routing/routing_verifier.hpp"

#include <format>
#include <stdexcept>

namespace keen_pbr3 {

RoutingHealthChecker::RoutingHealthChecker(const Firewall& firewall,
                                           const FirewallState& firewall_state,
                                           const RouteTable& route_table,
                                           const PolicyRuleManager& policy_rules,
                                           NetlinkManager& netlink)
    : firewall_(firewall),
      firewall_state_(firewall_state),
      route_table_(route_table),
      policy_rules_(policy_rules),
      netlink_(netlink) {}

RoutingHealthReport RoutingHealthChecker::check() const {
    RoutingHealthReport report;

    try {
        // Set firewall backend name
        switch (firewall_.backend()) {
            case FirewallBackend::iptables:
                report.firewall_backend = "iptables";
                break;
            case FirewallBackend::nftables:
                report.firewall_backend = "nftables";
                break;
        }

        // 1. Create firewall verifier
        auto verifier = create_firewall_verifier(firewall_.backend());

        // 2. Verify firewall chain
        report.firewall_chain = verifier->verify_chain();

        // 3. Verify firewall rules
        const auto& expected_rules = firewall_state_.get_rules();
        report.firewall_rules = verifier->verify_rules(expected_rules);

        // 4. Create routing verifier
        RoutingVerifier rv(netlink_);

        // Build a helper map: table_id -> outbound_tag
        // Using policy_rules (fwmark -> table) and outbound_marks (tag -> fwmark)
        const auto& marks = firewall_state_.get_outbound_marks();
        const auto& policy_rule_specs = policy_rules_.get_rules();

        // map: table_id -> outbound_tag (via fwmark)
        std::map<uint32_t, std::string> table_to_outbound;
        for (const auto& rule_spec : policy_rule_specs) {
            for (const auto& [tag, mark] : marks) {
                if (mark == rule_spec.fwmark) {
                    table_to_outbound[rule_spec.table] = tag;
                    break;
                }
            }
        }

        // 5. Verify route tables
        for (const auto& spec : route_table_.get_routes()) {
            std::string outbound_tag;
            auto it = table_to_outbound.find(spec.table);
            if (it != table_to_outbound.end()) {
                outbound_tag = it->second;
            }
            report.route_tables.push_back(rv.verify_route_table(spec, outbound_tag));
        }

        // 6. Verify policy rules
        for (const auto& spec : policy_rules_.get_rules()) {
            std::string outbound_tag;
            for (const auto& [tag, mark] : marks) {
                if (mark == spec.fwmark) {
                    outbound_tag = tag;
                    break;
                }
            }
            report.policy_rules.push_back(rv.verify_policy_rule(spec, outbound_tag));
        }

        // 7. Determine overall_ok
        bool all_ok = true;

        if (!report.firewall_chain.chain_present ||
            !report.firewall_chain.prerouting_hook_present) {
            all_ok = false;
        }

        if (all_ok) {
            for (const auto& fc : report.firewall_rules) {
                if (fc.status != CheckStatus::ok) {
                    all_ok = false;
                    break;
                }
            }
        }

        if (all_ok) {
            for (const auto& rt : report.route_tables) {
                if (rt.status != CheckStatus::ok) {
                    all_ok = false;
                    break;
                }
            }
        }

        if (all_ok) {
            for (const auto& pr : report.policy_rules) {
                if (pr.status != CheckStatus::ok) {
                    all_ok = false;
                    break;
                }
            }
        }

        report.overall_ok = all_ok;

    } catch (const std::exception& e) {
        report.overall_ok = false;
        report.error = e.what();
    } catch (...) {
        report.overall_ok = false;
        report.error = "unknown error during health check";
    }

    return report;
}

static std::string check_status_to_str(CheckStatus s) {
    switch (s) {
        case CheckStatus::ok:       return "ok";
        case CheckStatus::missing:  return "missing";
        case CheckStatus::mismatch: return "mismatch";
    }
    return "unknown";
}

static std::string hex_str(uint32_t v) {
    return std::format("0x{:08x}", v);
}

nlohmann::json routing_health_report_to_json(const RoutingHealthReport& r) {
    nlohmann::json j;

    if (!r.error.empty()) {
        j["overall"] = "error";
        j["error"] = r.error;
        return j;
    }

    j["overall"] = r.overall_ok ? "ok" : "degraded";
    j["firewall_backend"] = r.firewall_backend;

    // Firewall chain check
    {
        nlohmann::json fw;
        fw["chain_present"] = r.firewall_chain.chain_present;
        fw["prerouting_hook_present"] = r.firewall_chain.prerouting_hook_present;
        if (!r.firewall_chain.detail.empty()) {
            fw["detail"] = r.firewall_chain.detail;
        }
        j["firewall"] = fw;
    }

    // Firewall rules
    {
        nlohmann::json rules = nlohmann::json::array();
        for (const auto& fc : r.firewall_rules) {
            nlohmann::json rule;
            rule["set_name"] = fc.set_name;
            rule["action"] = fc.action;
            if (fc.expected_fwmark.has_value()) {
                rule["expected_fwmark"] = hex_str(*fc.expected_fwmark);
            }
            if (fc.actual_fwmark.has_value()) {
                rule["actual_fwmark"] = hex_str(*fc.actual_fwmark);
            }
            rule["status"] = check_status_to_str(fc.status);
            if (!fc.detail.empty()) {
                rule["detail"] = fc.detail;
            }
            rules.push_back(std::move(rule));
        }
        j["firewall_rules"] = std::move(rules);
    }

    // Route tables
    {
        nlohmann::json tables = nlohmann::json::array();
        for (const auto& rt : r.route_tables) {
            nlohmann::json table;
            table["table_id"] = rt.table_id;
            table["outbound_tag"] = rt.outbound_tag;
            if (rt.expected_interface.has_value()) {
                table["expected_interface"] = *rt.expected_interface;
            }
            if (rt.expected_gateway.has_value()) {
                table["expected_gateway"] = *rt.expected_gateway;
            }
            table["table_exists"] = rt.table_exists;
            table["default_route_present"] = rt.default_route_present;
            table["interface_matches"] = rt.interface_matches;
            table["gateway_matches"] = rt.gateway_matches;
            table["status"] = check_status_to_str(rt.status);
            if (!rt.detail.empty()) {
                table["detail"] = rt.detail;
            }
            tables.push_back(std::move(table));
        }
        j["route_tables"] = std::move(tables);
    }

    // Policy rules
    {
        nlohmann::json rules = nlohmann::json::array();
        for (const auto& pr : r.policy_rules) {
            nlohmann::json rule;
            rule["fwmark"] = hex_str(pr.fwmark);
            rule["fwmask"] = hex_str(pr.fwmask);
            rule["expected_table"] = pr.expected_table;
            rule["priority"] = pr.priority;
            rule["rule_present_v4"] = pr.rule_present_v4;
            rule["rule_present_v6"] = pr.rule_present_v6;
            rule["status"] = check_status_to_str(pr.status);
            if (!pr.detail.empty()) {
                rule["detail"] = pr.detail;
            }
            rules.push_back(std::move(rule));
        }
        j["policy_rules"] = std::move(rules);
    }

    return j;
}

} // namespace keen_pbr3
