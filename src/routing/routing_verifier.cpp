#include "routing_verifier.hpp"

#include <netinet/in.h>
#include <sstream>
#include <string>

namespace keen_pbr3 {

namespace {

std::string route_type_label(const DumpedRoute& route) {
    if (route.blackhole) return "blackhole";
    if (route.unreachable) return "unreachable";
    return "unicast";
}

std::string route_type_label(const RouteSpec& route) {
    if (route.blackhole) return "blackhole";
    if (route.unreachable) return "unreachable";
    return "unicast";
}

bool route_type_matches(const RouteSpec& expected, const DumpedRoute& actual) {
    return expected.blackhole == actual.blackhole &&
           expected.unreachable == actual.unreachable;
}

bool route_metric_matches(const RouteSpec& expected, const DumpedRoute& actual) {
    return expected.metric == actual.metric;
}

bool route_matches_expected(const RouteSpec& expected, const DumpedRoute& actual) {
    if (actual.destination != "default") {
        return false;
    }
    if (!route_type_matches(expected, actual)) {
        return false;
    }
    if (!route_metric_matches(expected, actual)) {
        return false;
    }
    if (expected.blackhole || expected.unreachable) {
        return true;
    }
    if (expected.interface) {
        if (!actual.interface || *actual.interface != *expected.interface) {
            return false;
        }
    }
    if (expected.gateway) {
        if (!actual.gateway || *actual.gateway != *expected.gateway) {
            return false;
        }
    }
    return true;
}

} // anonymous namespace

RoutingVerifier::RoutingVerifier(NetlinkManager& netlink)
    : netlink_(netlink) {}

RouteTableCheck RoutingVerifier::verify_route_table(const RouteSpec& expected,
                                                     const std::string& outbound_tag) {
    RouteTableCheck result;
    result.table_id       = expected.table;
    result.outbound_tag   = outbound_tag;
    result.expected_destination = expected.destination;
    result.expected_interface = expected.interface;
    result.expected_gateway   = expected.gateway;
    if (expected.metric != 0) {
        result.expected_metric = expected.metric;
    }
    result.expected_route_type = route_type_label(expected);

    try {
        auto routes = netlink_.dump_routes_in_table(expected.table);

        // Consider the table to exist if we got any routes (even without a
        // default route, the table itself is accessible).
        result.table_exists = !routes.empty();

        const DumpedRoute* first_default = nullptr;
        for (const auto& r : routes) {
            if (r.destination != "default") {
                continue;
            }

            result.default_route_present = true;
            if (!first_default) {
                first_default = &r;
            }

            if (!route_matches_expected(expected, r)) {
                continue;
            }

            result.interface_matches = true;
            result.gateway_matches = true;
            result.status = CheckStatus::ok;
            return result;
        }

        if (first_default) {
            result.status = CheckStatus::mismatch;
            result.interface_matches = !expected.interface ||
                (first_default->interface && *first_default->interface == *expected.interface);
            result.gateway_matches = !expected.gateway ||
                (first_default->gateway && *first_default->gateway == *expected.gateway);

            std::ostringstream detail;
            if (!route_type_matches(expected, *first_default)) {
                detail << "route type mismatch: expected '" << route_type_label(expected)
                       << "', got '" << route_type_label(*first_default) << "'.";
            } else if (!route_metric_matches(expected, *first_default)) {
                detail << "metric mismatch: expected '" << expected.metric
                       << "', got '" << first_default->metric << "'.";
            } else {
                if (!result.interface_matches) {
                    detail << "interface mismatch: expected '"
                           << expected.interface.value_or("(none)") << "', got '"
                           << first_default->interface.value_or("(none)") << "'.";
                }
                if (!result.gateway_matches) {
                    if (!detail.str().empty()) detail << " ";
                    detail << "gateway mismatch: expected '"
                           << expected.gateway.value_or("(none)") << "', got '"
                           << first_default->gateway.value_or("(none)") << "'.";
                }
            }
            result.detail = detail.str();
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
