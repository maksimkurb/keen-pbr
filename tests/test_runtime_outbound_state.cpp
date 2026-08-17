#ifdef WITH_API

#include <doctest/doctest.h>

#include "health/runtime_outbound_state.hpp"

#include <netinet/in.h>

namespace keen_pbr3 {
namespace {

Outbound make_outbound(std::string tag, OutboundType type,
                       std::optional<std::string> interface = std::nullopt) {
    Outbound outbound;
    outbound.tag = std::move(tag);
    outbound.type = type;
    outbound.interface = std::move(interface);
    return outbound;
}

RuleSpec lookup_rule(std::uint32_t mark, std::uint32_t table) {
    RuleSpec rule;
    rule.fwmark = mark;
    rule.table = table;
    rule.action = RuleAction::lookup;
    return rule;
}

DumpedRoute default_route(std::uint32_t table, int family,
                          std::optional<std::string> interface = std::nullopt) {
    DumpedRoute route;
    route.destination = "default";
    route.table = table;
    route.family = family;
    route.interface = std::move(interface);
    return route;
}

} // namespace

TEST_CASE("runtime outbound projection reuses one route snapshot") {
    Config config;
    auto interface = make_outbound("wan", OutboundType::INTERFACE, "lo");
    auto table = make_outbound("v6", OutboundType::TABLE);
    auto missing = make_outbound("missing", OutboundType::TABLE);
    auto automatic = make_outbound("auto", OutboundType::URLTEST);
    OutboundGroup group;
    group.outbounds = std::vector<std::string>{"wan"};
    automatic.outbound_groups = std::vector<OutboundGroup>{group};
    config.outbounds = std::vector<Outbound>{interface, table, missing, automatic};

    const OutboundMarkMap marks{{"wan", 1U}, {"v6", 2U},
                                {"missing", 3U}, {"auto", 4U}};
    const std::vector<RuleSpec> rules{
        lookup_rule(1U, 100U), lookup_rule(2U, 101U),
        lookup_rule(3U, 102U), lookup_rule(4U, 103U)};
    const std::vector<DumpedRoute> routes{
        default_route(254U, AF_INET, "lo"),
        default_route(100U, AF_INET, "lo"),
        default_route(101U, AF_INET6),
        default_route(103U, AF_INET, "lo")};

    const auto response = build_runtime_outbounds_response_from_routes(
        config, marks, rules, routes,
        [](const std::string&) { return std::optional<UrltestState>{}; });

    REQUIRE(response.outbounds.size() == 4);
    CHECK(response.outbounds[0].status == api::ResolverLiveStatus::HEALTHY);
    CHECK(response.outbounds[1].status == api::ResolverLiveStatus::HEALTHY);
    CHECK(response.outbounds[2].status == api::ResolverLiveStatus::UNKNOWN);
    CHECK(response.outbounds[3].status == api::ResolverLiveStatus::HEALTHY);
    CHECK(response.outbounds[3].interfaces.size() == 1);
}

} // namespace keen_pbr3

#endif
