#include <doctest/doctest.h>

#include "routing/routing_verifier.hpp"

#include <netinet/in.h>
#include <vector>

namespace keen_pbr3 {
namespace {

class VerifierNetlink final : public RoutingNetlinkOperations {
public:
    RouteAddResult add_route(const RouteSpec&) override { return RouteAddResult::Created; }
    void delete_route(const RouteSpec&) override {}
    RuleAddResult add_rule_for_family(const RuleSpec&, int) override {
        return RuleAddResult::Created;
    }
    void delete_rule_for_family(const RuleSpec&, int) override {}
    std::vector<DumpedRoute> dump_routes_in_table(uint32_t, int = 0) override {
        return routes;
    }
    std::vector<DumpedRule> dump_policy_rules(int = 0) override { return {}; }

    std::vector<DumpedRoute> routes;
};

} // namespace

TEST_CASE("RoutingVerifier does not compare an IPv6 expectation with an IPv4 default") {
    VerifierNetlink netlink;
    netlink.routes.push_back(DumpedRoute{
        "default", 403, std::string("vpn3sad"), std::nullopt,
        false, false, AF_INET, 0, 186});

    RouteSpec expected;
    expected.destination = "default";
    expected.table = 403;
    expected.interface = "vpn3sad";
    expected.family = AF_INET6;
    expected.protocol = 186;

    RoutingVerifier verifier(netlink);
    const auto result = verifier.verify_route_table(expected, "vpn_home");

    CHECK(result.status == CheckStatus::missing);
    CHECK_FALSE(result.default_route_present);
    CHECK(result.detail == "no IPv6 default route found in table 403");
}

} // namespace keen_pbr3
