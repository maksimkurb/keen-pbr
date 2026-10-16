#include <doctest/doctest.h>

#include "../src/intercept/rebind_backoff.hpp"

using namespace keen_pbr3;
using namespace std::chrono_literals;
using Clock = RebindBackoff::Clock;

TEST_CASE("rebind backoff doubles from 1 s up to 60 s") {
    CHECK(RebindBackoff::delay_for(0) == 1s);
    CHECK(RebindBackoff::delay_for(1) == 2s);
    CHECK(RebindBackoff::delay_for(2) == 4s);
    CHECK(RebindBackoff::delay_for(5) == 32s);
    CHECK(RebindBackoff::delay_for(6) == 60s);
    CHECK(RebindBackoff::delay_for(50) == 60s);
}

TEST_CASE("rebind backoff stays idle while healthy") {
    RebindBackoff b;
    const auto t0 = Clock::now();
    b.observe(true, t0);
    CHECK_FALSE(b.pending());
    CHECK_FALSE(b.due(t0 + 1h));
}

TEST_CASE("rebind backoff schedules the first attempt after 1 s and doubles") {
    RebindBackoff b;
    auto t = Clock::now();
    b.observe(false, t);
    CHECK(b.pending());
    CHECK_FALSE(b.due(t + 999ms));
    CHECK(b.due(t + 1s));
    t += 1s;
    b.attempted(t);
    b.observe(false, t);
    CHECK(b.attempts() == 1);
    CHECK_FALSE(b.due(t + 1999ms));
    CHECK(b.due(t + 2s));
    t += 2s;
    b.attempted(t);
    CHECK_FALSE(b.due(t + 3999ms));
    CHECK(b.due(t + 4s));
}

TEST_CASE("rebind backoff resets only after a stable healthy window") {
    RebindBackoff b;
    auto t = Clock::now();
    b.observe(false, t);
    t += 1s;
    b.attempted(t);
    b.observe(true, t);
    CHECK_FALSE(b.pending());
    CHECK(b.attempts() == 1);
    // Dies again before the window ends: the schedule keeps growing.
    t += 10s;
    b.observe(false, t);
    CHECK(b.pending());
    CHECK(b.attempts() == 1);
    CHECK(b.due(t + 2s));
    t += 2s;
    b.attempted(t);
    b.observe(true, t);
    b.observe(true, t + RebindBackoff::kStable - 1s);
    CHECK(b.attempts() == 2);
    b.observe(true, t + RebindBackoff::kStable);
    CHECK(b.attempts() == 0);
    b.observe(false, t + 2 * RebindBackoff::kStable);
    CHECK(b.due(t + 2 * RebindBackoff::kStable + 1s));
}

TEST_CASE("rebind backoff reset cancels a pending attempt") {
    RebindBackoff b;
    const auto t = Clock::now();
    b.observe(false, t);
    b.reset();
    CHECK_FALSE(b.pending());
    CHECK_FALSE(b.due(t + 1h));
}
