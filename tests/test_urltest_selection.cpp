#include <doctest/doctest.h>

#include "routing/urltest_manager.hpp"

namespace keen_pbr3 {
namespace {

UrltestState state_for(OutboundType type) {
    UrltestState state;
    state.config.type = type;
    state.config.tolerance_ms = 10;
    OutboundGroup preferred;
    OutboundGroup fallback;
    const auto member = [type](const char* tag, const char* target) {
        api::OutboundGroupMemberElement value;
        value.outbound = tag;
        if (type == OutboundType::ICMPTEST) value.target = target;
        return value;
    };
    preferred.members = std::vector<api::OutboundGroupMemberElement>{
        member("a", "1.1.1.1"), member("b", "8.8.8.8")};
    fallback.members = std::vector<api::OutboundGroupMemberElement>{member("c", "9.9.9.9")};
    state.config.outbound_groups = std::vector<OutboundGroup>{preferred, fallback};
    for (const auto* tag : {"a", "b", "c"}) {
        state.circuit_breakers.emplace(tag, CircuitBreaker(CircuitBreakerConfig{}));
    }
    return state;
}

URLTestResult result(bool success, uint32_t latency) {
    URLTestResult value;
    value.success = success;
    value.latency_ms = latency;
    return value;
}

} // namespace

TEST_CASE("URLTEST and ICMPTEST share selection and tolerance behavior") {
    for (const auto type : {OutboundType::URLTEST, OutboundType::ICMPTEST}) {
        CAPTURE(type);
        auto state = state_for(type);
        CHECK(select_test_group_outbound(state).empty());

        state.last_results = {{"a", result(true, 30)}, {"b", result(true, 20)}};
        CHECK(select_test_group_outbound(state) == "a");
        CHECK(select_test_group_usable_outbounds(state) ==
              std::vector<std::string>{"a", "b"});

        state.selected_outbound = "a";
        state.last_results["a"] = result(true, 35);
        CHECK(select_test_group_outbound(state) == "b");

        state.selected_outbound = "b";
        state.last_results["b"] = result(false, 0);
        CHECK(select_test_group_outbound(state) == "a");

        state.last_results["a"] = result(false, 0);
        state.last_results["c"] = result(true, 50);
        CHECK(select_test_group_outbound(state) == "c");
    }
}

TEST_CASE("balance candidates use only the first healthy priority tier") {
    auto state = state_for(OutboundType::URLTEST);
    state.last_results = {{"a", result(false, 0)}, {"b", result(true, 20)},
                          {"c", result(true, 1)}};
    CHECK(select_test_group_usable_outbounds(state) ==
          std::vector<std::string>{"b"});
}

} // namespace keen_pbr3
