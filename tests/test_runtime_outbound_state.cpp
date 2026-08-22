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
        lookup_rule(3U, 102U), lookup_rule(4U, 100U)};
    const std::vector<DumpedRoute> routes{
        default_route(254U, AF_INET, "lo"),
        default_route(100U, AF_INET, "lo"),
        default_route(101U, AF_INET6)};

    UrltestState automatic_state;
    automatic_state.selected_outbound = "wan";
    automatic_state.last_results["wan"] = URLTestResult{
        .success = true,
        .latency_ms = 12,
    };

    const auto response = build_runtime_outbounds_response_from_routes(
        config, marks, rules, {{"auto", "wan"}}, routes,
        [&automatic_state](const std::string& tag) -> std::optional<UrltestState> {
            return tag == "auto" ? std::optional<UrltestState>{automatic_state}
                                 : std::nullopt;
        });

    REQUIRE(response.outbounds.size() == 4);
    CHECK(response.outbounds[0].status == api::ResolverLiveStatus::HEALTHY);
    CHECK(response.outbounds[1].status == api::ResolverLiveStatus::HEALTHY);
    CHECK(response.outbounds[2].status == api::ResolverLiveStatus::UNKNOWN);
    CHECK(response.outbounds[3].status == api::ResolverLiveStatus::HEALTHY);
    CHECK(response.outbounds[3].interfaces.size() == 1);
    CHECK(response.outbounds[3].interfaces[0].status == api::RuntimeInterfaceStatusEnum::ACTIVE);
    CHECK(response.outbounds[3].interfaces[0].latency_ms == 12);
}

TEST_CASE("runtime test-group projection reports table candidate probe state") {
    Config config;
    auto table = make_outbound("external", OutboundType::TABLE);
    table.table = 200;
    auto automatic = make_outbound("auto", OutboundType::ICMPTEST);
    OutboundGroup group;
    api::IcmpCandidateElement candidate;
    candidate.outbound = "external";
    candidate.target = "1.1.1.1";
    group.candidates = std::vector<api::IcmpCandidateElement>{candidate};
    automatic.outbound_groups = std::vector<OutboundGroup>{group};
    config.outbounds = std::vector<Outbound>{table, automatic};

    const OutboundMarkMap marks{{"external", 1U}, {"auto", 2U}};
    const std::vector<RuleSpec> rules{
        lookup_rule(1U, 200U), lookup_rule(2U, 200U)};
    const std::vector<DumpedRoute> routes{default_route(200U, AF_INET)};
    UrltestState state;
    state.selected_outbound = "external";
    URLTestResult result;
    result.success = true;
    result.latency_ms = 23;
    result.packets_attempted = 3;
    result.packets_sent = 3;
    result.packets_received = 3;
    result.packets_failed = 0;
    state.last_results["external"] = result;

    const auto build = [&]() {
        return build_runtime_outbounds_response_from_routes(
            config, marks, rules, {{"auto", "external"}}, routes,
            [&state](const std::string& tag) -> std::optional<UrltestState> {
                return tag == "auto" ? std::optional<UrltestState>{state}
                                     : std::nullopt;
            });
    };

    auto response = build();
    REQUIRE(response.outbounds.size() == 2);
    REQUIRE(response.outbounds[1].interfaces.size() == 1);
    const auto& active = response.outbounds[1].interfaces[0];
    CHECK_FALSE(active.interface_name.has_value());
    CHECK_FALSE(nlohmann::json(active).contains("interface_name"));
    CHECK(active.status == api::RuntimeInterfaceStatusEnum::ACTIVE);
    CHECK(active.latency_ms == 23);
    CHECK(active.packets_received == 3);

    state.last_results["external"].success = false;
    response = build();
    CHECK(response.outbounds[1].interfaces[0].status ==
          api::RuntimeInterfaceStatusEnum::DEGRADED);
    CHECK(response.outbounds[1].status == api::ResolverLiveStatus::DEGRADED);
}

} // namespace keen_pbr3

#endif
