#include <doctest/doctest.h>

#include "routing/urltest_manager.hpp"

namespace keen_pbr3 {
namespace {

UrltestState state_for(OutboundType type) {
    UrltestState state;
    state.config.type = type;
    state.config.tolerance_ms = 10;
    OutboundGroup preferred;
    preferred.weight = 1;
    OutboundGroup fallback;
    fallback.weight = 2;
    if (type == OutboundType::ICMPTEST) {
        api::IcmpCandidateElement candidate_a;
        candidate_a.outbound = "a";
        candidate_a.target = "1.1.1.1";
        api::IcmpCandidateElement candidate_b;
        candidate_b.outbound = "b";
        candidate_b.target = "8.8.8.8";
        api::IcmpCandidateElement candidate_c;
        candidate_c.outbound = "c";
        candidate_c.target = "9.9.9.9";
        preferred.candidates = std::vector<api::IcmpCandidateElement>{
            candidate_a, candidate_b,
        };
        fallback.candidates = std::vector<api::IcmpCandidateElement>{
            candidate_c,
        };
    } else {
        preferred.outbounds = std::vector<std::string>{"a", "b"};
        fallback.outbounds = std::vector<std::string>{"c"};
    }
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

} // namespace keen_pbr3
