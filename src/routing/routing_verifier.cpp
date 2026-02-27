#include "routing_verifier.hpp"

#include <netinet/in.h>
#include <sstream>
#include <string>

namespace keen_pbr3 {

RoutingVerifier::RoutingVerifier(NetlinkManager& netlink)
    : netlink_(netlink) {}

RouteTableCheck RoutingVerifier::verify_route_table(const RouteSpec& expected,
                                                     const std::string& outbound_tag) {
    RouteTableCheck result;
    result.table_id       = expected.table;
    result.outbound_tag   = outbound_tag;
    result.expected_interface = expected.interface;
    result.expected_gateway   = expected.gateway;

    try {
        auto routes = netlink_.dump_routes_in_table(expected.table);

        // Consider the table to exist if we got any routes (even without a
        // default route, the table itself is accessible).
        result.table_exists = !routes.empty();

        for (const auto& r : routes) {
            // We are looking for the default route (destination "default")
            if (r.destination != "default") {
                continue;
            }

            result.default_route_present = true;

            // Check interface match
            if (expected.interface) {
                result.interface_matches =
                    (r.interface && *r.interface == *expected.interface);
            } else {
                // No interface expected — consider it matched unless one is set
                result.interface_matches = true;
            }

            // Check gateway match
            if (expected.gateway) {
                result.gateway_matches =
                    (r.gateway && *r.gateway == *expected.gateway);
            } else {
                result.gateway_matches = true;
            }

            // Determine overall status
            if (result.interface_matches && result.gateway_matches) {
                result.status = CheckStatus::ok;
            } else {
                result.status = CheckStatus::mismatch;
                std::ostringstream detail;
                if (!result.interface_matches) {
                    detail << "interface mismatch: expected '"
                           << expected.interface.value_or("(none)") << "', got '"
                           << r.interface.value_or("(none)") << "'. ";
                }
                if (!result.gateway_matches) {
                    detail << "gateway mismatch: expected '"
                           << expected.gateway.value_or("(none)") << "', got '"
                           << r.gateway.value_or("(none)") << "'.";
                }
                result.detail = detail.str();
            }
            return result;
        }

        // No default route found in this table
        if (!result.table_exists) {
            result.status = CheckStatus::missing;
            result.detail = "routing table " + std::to_string(expected.table) +
                            " has no routes (table missing or empty)";
        } else {
            result.status = CheckStatus::missing;
            result.detail = "no default route found in table " +
                            std::to_string(expected.table);
        }
    } catch (const NetlinkError& e) {
        result.status = CheckStatus::missing;
        result.detail = std::string("netlink error: ") + e.what();
    }

    return result;
}

PolicyRuleCheck RoutingVerifier::verify_policy_rule(const RuleSpec& expected,
                                                     const std::string& outbound_tag) {
    PolicyRuleCheck result;
    result.fwmark         = expected.fwmark;
    result.fwmask         = expected.fwmask;
    result.expected_table = expected.table;
    result.priority       = expected.priority;

    try {
        auto rules = netlink_.dump_policy_rules();

        for (const auto& r : rules) {
            if (r.fwmark != expected.fwmark || r.fwmask != expected.fwmask ||
                r.table  != expected.table) {
                continue;
            }
            if (r.family == AF_INET) {
                result.rule_present_v4 = true;
            } else if (r.family == AF_INET6) {
                result.rule_present_v6 = true;
            }
        }

        bool need_v4 = (expected.family == 0 || expected.family == AF_INET);
        bool need_v6 = (expected.family == 0 || expected.family == AF_INET6);

        bool ok_v4 = !need_v4 || result.rule_present_v4;
        bool ok_v6 = !need_v6 || result.rule_present_v6;

        if (ok_v4 && ok_v6) {
            result.status = CheckStatus::ok;
        } else {
            result.status = CheckStatus::missing;
            std::ostringstream detail;
            detail << "ip rule fwmark=0x" << std::hex << expected.fwmark
                   << "/0x" << expected.fwmask << " table=" << std::dec
                   << expected.table << " missing:";
            if (!ok_v4) detail << " IPv4";
            if (!ok_v6) detail << " IPv6";
            result.detail = detail.str();
        }
    } catch (const NetlinkError& e) {
        result.status = CheckStatus::missing;
        result.detail = std::string("netlink error: ") + e.what();
    }

    return result;
}

} // namespace keen_pbr3
