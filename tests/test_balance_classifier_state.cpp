#include <doctest/doctest.h>

#include "../src/routing/balance_classifier_state.hpp"

using namespace keen_pbr3;

namespace {

BalanceClassifierState state(std::string selected, std::set<std::string> failed) {
    BalanceClassifierState s;
    s.selected_child = std::move(selected);
    s.candidates = {{0x100, true, true}, {0x200, true, false}};
    s.failed_children = std::move(failed);
    return s;
}

} // namespace

TEST_CASE("balance classifier state: unchanged probe cycle compares equal") {
    CHECK(state("a", {"c"}) == state("a", {"c"}));
}

TEST_CASE("balance classifier state: any classifier input change compares unequal") {
    const auto applied = state("a", {});
    CHECK(applied != state("b", {}));    // selection changed
    CHECK(applied != state("a", {"b"})); // a child failed
    auto reachability = state("a", {});
    reachability.candidates[1].ipv6 = true;
    CHECK(applied != reachability);
}

TEST_CASE("balance classifier state: a failed child is flushed once") {
    CHECK(newly_failed_children(nullptr, {"b"}) == std::set<std::string>{"b"});
    const auto applied = state("a", {"b"});
    CHECK(newly_failed_children(&applied, {"b"}).empty());
    CHECK(newly_failed_children(&applied, {"b", "c"}) == std::set<std::string>{"c"});
    // Recovered and failed again: flushed again.
    const auto recovered = state("a", {});
    CHECK(newly_failed_children(&recovered, {"b"}) == std::set<std::string>{"b"});
}
