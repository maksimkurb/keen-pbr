#include "routing_health_checker.hpp"

#include "../api/generated/api_types.hpp"
#include "../firewall/firewall_verifier.hpp"
#include "../routing/routing_verifier.hpp"
#include "../util/format_compat.hpp"
#include "../util/string_compat.hpp"

#include <stdexcept>

namespace keen_pbr3 {

namespace {

std::string route_type_label(const RouteSpec& spec) {
    if (spec.unreachable) return "unreachable";
    if (spec.blackhole) return "blackhole";
    return "unicast";
}

std::string route_type_label(const DumpedRoute& route) {
    if (route.unreachable) return "unreachable";
    if (route.blackhole) return "blackhole";
    return "unicast";
}

bool route_matches(const RouteSpec& expected, const DumpedRoute& actual) {
    return expected.destination == actual.destination &&
           expected.interface == actual.interface &&
           expected.gateway == actual.gateway &&
           expected.blackhole == actual.blackhole &&
           expected.unreachable == actual.unreachable &&
           expected.metric == actual.metric &&
           (expected.family == 0 || expected.family == actual.family);
}

} // anonymous namespace

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

RoutingHealthReport build_routing_health_report(
    FirewallBackend firewall_backend,
    const FirewallState& firewall_state,
    const std::vector<RouteSpec>& tracked_routes,
    const std::vector<RuleSpec>& tracked_policy_rules,
    NetlinkManager& netlink) {
    RoutingHealthReport report;
    report.firewall_backend = firewall_backend;

    try {
        // 1. Create firewall verifier
        auto verifier = create_firewall_verifier(firewall_backend);

        // 2. Verify firewall chain
        report.firewall_chain = verifier->verify_chain();

        // 3. Verify firewall rules
        const auto& expected_rules = firewall_state.get_rules();
        report.firewall_rules = verifier->verify_rules(expected_rules);

        // 4. Create routing verifier
        RoutingVerifier rv(netlink);

        // Build a helper map: table_id -> outbound_tag
        // Using policy_rules (fwmark -> table) and outbound_marks (tag -> fwmark)
        const auto& marks = firewall_state.get_outbound_marks();

        // map: table_id -> outbound_tag (via fwmark)
        std::map<uint32_t, std::string> table_to_outbound;
        for (const auto& rule_spec : tracked_policy_rules) {
            for (const auto& [tag, mark] : marks) {
                if (mark == rule_spec.fwmark) {
                    table_to_outbound[rule_spec.table] = tag;
                    break;
                }
            }
        }

        // 5. Verify route tables and detect unexpected live routes.
        std::map<uint32_t, std::vector<RouteSpec>> expected_routes_by_table;
        for (const auto& spec : tracked_routes) {
            std::string outbound_tag;
            auto it = table_to_outbound.find(spec.table);
            if (it != table_to_outbound.end()) {
                outbound_tag = it->second;
            }
            expected_routes_by_table[spec.table].push_back(spec);
            report.route_tables.push_back(rv.verify_route_table(spec, outbound_tag));
        }

        for (const auto& [table_id, expected_routes] : expected_routes_by_table) {
            auto routes = netlink.dump_routes_in_table(table_id);
            std::vector<bool> matched(routes.size(), false);

            for (const auto& expected : expected_routes) {
                for (size_t i = 0; i < routes.size(); ++i) {
                    if (matched[i]) continue;
                    if (route_matches(expected, routes[i])) {
                        matched[i] = true;
                        break;
                    }
                }
            }

            for (size_t i = 0; i < routes.size(); ++i) {
                if (matched[i]) continue;

                RouteTableCheck extra_check;
                extra_check.table_id = table_id;
                extra_check.outbound_tag = contains(table_to_outbound, table_id)
                    ? table_to_outbound.at(table_id)
                    : "";
                extra_check.expected_destination = routes[i].destination;
                extra_check.expected_interface = routes[i].interface;
                extra_check.expected_gateway = routes[i].gateway;
                if (routes[i].metric != 0) {
                    extra_check.expected_metric = routes[i].metric;
                }
                extra_check.expected_route_type = route_type_label(routes[i]);
                extra_check.table_exists = true;
                extra_check.default_route_present = (routes[i].destination == "default");
                extra_check.interface_matches = true;
                extra_check.gateway_matches = true;
                extra_check.status = CheckStatus::mismatch;
                extra_check.detail = "unexpected route present in table";
                report.route_tables.push_back(std::move(extra_check));
            }
        }

        // 6. Verify policy rules
        for (const auto& spec : tracked_policy_rules) {
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

RoutingHealthReport RoutingHealthChecker::check() const {
    return build_routing_health_report(
        firewall_.backend(),
        firewall_state_,
        route_table_.get_routes(),
        policy_rules_.get_rules(),
        netlink_);
}

static std::string hex_str(uint32_t v) {
    return keen_pbr3::format("0x{:08x}", v);
}

static api::CheckStatus to_api_check_status(CheckStatus s) {
    switch (s) {
        case CheckStatus::ok:       return api::CheckStatus::OK;
        case CheckStatus::missing:  return api::CheckStatus::MISSING;
        case CheckStatus::mismatch: return api::CheckStatus::MISMATCH;
    }
    return api::CheckStatus::MISSING;
}

static api::RoutingHealthResponseFirewallBackend to_api_firewall_backend(FirewallBackend backend) {
    switch (backend) {
        case FirewallBackend::iptables:
            return api::RoutingHealthResponseFirewallBackend::IPTABLES;
        case FirewallBackend::nftables:
            return api::RoutingHealthResponseFirewallBackend::NFTABLES;
    }

    throw std::runtime_error("Unexpected firewall backend value");
}

nlohmann::json routing_health_report_to_json(const RoutingHealthReport& r) {
    if (!r.error.empty()) {
        api::RoutingHealthErrorResponse err;
        err.error = r.error;
        err.overall = api::RoutingHealthErrorResponseOverall::ERROR;
        return nlohmann::json(err);
    }

    api::RoutingHealthResponse resp;
    resp.overall = r.overall_ok
        ? api::RoutingHealthResponseOverall::OK
        : api::RoutingHealthResponseOverall::DEGRADED;

    if (!r.firewall_backend.has_value()) {
        throw std::runtime_error("Routing health report missing firewall backend");
    }

    resp.firewall_backend = to_api_firewall_backend(*r.firewall_backend);

    resp.firewall.chain_present = r.firewall_chain.chain_present;
    resp.firewall.prerouting_hook_present = r.firewall_chain.prerouting_hook_present;
    if (!r.firewall_chain.detail.empty()) resp.firewall.detail = r.firewall_chain.detail;

    for (const auto& fc : r.firewall_rules) {
        api::FirewallRuleCheck arc;
        arc.set_name = fc.set_name;
        arc.action   = fc.action;
        arc.status   = to_api_check_status(fc.status);
        if (fc.expected_fwmark) arc.expected_fwmark = hex_str(*fc.expected_fwmark);
        if (fc.actual_fwmark)   arc.actual_fwmark   = hex_str(*fc.actual_fwmark);
        if (!fc.detail.empty()) arc.detail           = fc.detail;
        resp.firewall_rules.push_back(std::move(arc));
    }

    for (const auto& rt : r.route_tables) {
        api::RouteTableCheck arc;
        arc.table_id             = rt.table_id;
        arc.outbound_tag         = rt.outbound_tag;
        arc.table_exists         = rt.table_exists;
        arc.default_route_present = rt.default_route_present;
        arc.interface_matches    = rt.interface_matches;
        arc.gateway_matches      = rt.gateway_matches;
        arc.status               = to_api_check_status(rt.status);
        if (rt.expected_destination) arc.expected_destination = *rt.expected_destination;
        if (rt.expected_interface) arc.expected_interface = *rt.expected_interface;
        if (rt.expected_gateway)   arc.expected_gateway   = *rt.expected_gateway;
        if (rt.expected_metric)    arc.expected_metric    = static_cast<int64_t>(*rt.expected_metric);
        if (rt.expected_route_type) arc.expected_route_type = *rt.expected_route_type;
        if (!rt.detail.empty())    arc.detail             = rt.detail;
        resp.route_tables.push_back(std::move(arc));
    }

    for (const auto& pr : r.policy_rules) {
        api::PolicyRuleCheck arc;
        arc.fwmark          = hex_str(pr.fwmark);
        arc.fwmask          = hex_str(pr.fwmask);
        arc.expected_table  = pr.expected_table;
        arc.priority        = pr.priority;
        arc.rule_present_v4 = pr.rule_present_v4;
        arc.rule_present_v6 = pr.rule_present_v6;
        arc.status          = to_api_check_status(pr.status);
        if (!pr.detail.empty()) arc.detail = pr.detail;
        resp.policy_rules.push_back(std::move(arc));
    }

    return nlohmann::json(resp);
}

} // namespace keen_pbr3
