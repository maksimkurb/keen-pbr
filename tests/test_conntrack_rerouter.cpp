#include <doctest/doctest.h>

#include "../src/routing/conntrack_rerouter.hpp"

#include <map>
#include <string>
#include <vector>

using namespace keen_pbr3;

namespace {

// A scriptable DynamicSetReader: each poll consumes the next canned snapshot.
struct FakeSetSource {
    std::vector<std::map<std::string, std::vector<std::string>>> snapshots;
    std::size_t next = 0;

    std::map<std::string, std::vector<std::string>> read(
        const std::vector<std::string>& /*set_names*/) {
        if (next >= snapshots.size()) {
            return {};
        }
        return snapshots[next++];
    }
};

} // namespace

TEST_CASE("ConntrackRerouter: first poll baselines without flushing") {
    FakeSetSource source;
    source.snapshots = {
        {{"kpbr4d_a", {"1.1.1.1", "2.2.2.2"}}},
    };
    std::vector<std::string> flushed;

    ConntrackRerouter rerouter(
        [&](const std::vector<std::string>& n) { return source.read(n); },
        [&](const std::string& ip) { flushed.push_back(ip); });

    const std::size_t count = rerouter.poll({"kpbr4d_a"});
    CHECK(count == 0);
    CHECK(flushed.empty());
}

TEST_CASE("ConntrackRerouter: flushes only IPs added after the baseline") {
    FakeSetSource source;
    source.snapshots = {
        {{"kpbr4d_a", {"1.1.1.1"}}},                          // baseline
        {{"kpbr4d_a", {"1.1.1.1", "3.3.3.3"}}},               // 3.3.3.3 is new
        {{"kpbr4d_a", {"1.1.1.1", "3.3.3.3"}}},               // nothing new
    };
    std::vector<std::string> flushed;

    ConntrackRerouter rerouter(
        [&](const std::vector<std::string>& n) { return source.read(n); },
        [&](const std::string& ip) { flushed.push_back(ip); });

    CHECK(rerouter.poll({"kpbr4d_a"}) == 0);
    CHECK(rerouter.poll({"kpbr4d_a"}) == 1);
    CHECK(rerouter.poll({"kpbr4d_a"}) == 0);

    REQUIRE(flushed.size() == 1);
    CHECK(flushed[0] == "3.3.3.3");
}

TEST_CASE("ConntrackRerouter: an IP that leaves and returns is flushed again") {
    FakeSetSource source;
    source.snapshots = {
        {{"kpbr4d_a", {"9.9.9.9"}}},   // baseline
        {{"kpbr4d_a", {}}},            // 9.9.9.9 expired out
        {{"kpbr4d_a", {"9.9.9.9"}}},   // resolved again -> treated as new
    };
    std::vector<std::string> flushed;

    ConntrackRerouter rerouter(
        [&](const std::vector<std::string>& n) { return source.read(n); },
        [&](const std::string& ip) { flushed.push_back(ip); });

    rerouter.poll({"kpbr4d_a"});
    rerouter.poll({"kpbr4d_a"});
    CHECK(rerouter.poll({"kpbr4d_a"}) == 1);
    REQUIRE(flushed.size() == 1);
    CHECK(flushed[0] == "9.9.9.9");
}

TEST_CASE("ConntrackRerouter: reset re-baselines so nothing is flushed next poll") {
    FakeSetSource source;
    source.snapshots = {
        {{"kpbr4d_a", {"1.1.1.1"}}},
        {{"kpbr4d_a", {"1.1.1.1", "2.2.2.2"}}},
    };
    std::vector<std::string> flushed;

    ConntrackRerouter rerouter(
        [&](const std::vector<std::string>& n) { return source.read(n); },
        [&](const std::string& ip) { flushed.push_back(ip); });

    rerouter.poll({"kpbr4d_a"});
    rerouter.reset();
    // After reset the next poll is a fresh baseline even though 2.2.2.2 is new
    // relative to the pre-reset snapshot.
    CHECK(rerouter.poll({"kpbr4d_a"}) == 0);
    CHECK(flushed.empty());
}

TEST_CASE("ConntrackRerouter: tracks multiple sets independently") {
    FakeSetSource source;
    source.snapshots = {
        {{"kpbr4d_a", {"1.1.1.1"}}, {"kpbr6d_a", {"2001:db8::1"}}},
        {{"kpbr4d_a", {"1.1.1.1", "4.4.4.4"}}, {"kpbr6d_a", {"2001:db8::1"}}},
    };
    std::vector<std::string> flushed;

    ConntrackRerouter rerouter(
        [&](const std::vector<std::string>& n) { return source.read(n); },
        [&](const std::string& ip) { flushed.push_back(ip); });

    rerouter.poll({"kpbr4d_a", "kpbr6d_a"});
    CHECK(rerouter.poll({"kpbr4d_a", "kpbr6d_a"}) == 1);
    REQUIRE(flushed.size() == 1);
    CHECK(flushed[0] == "4.4.4.4");
}

TEST_CASE("ConntrackRerouter: a set that disappears is re-baselined when it returns") {
    FakeSetSource source;
    source.snapshots = {
        {{"kpbr4d_a", {"1.1.1.1"}}},   // baseline
        {},                            // set absent this poll (reader omitted it)
        {{"kpbr4d_a", {"1.1.1.1"}}},   // back again -> fresh baseline, no flush
    };
    std::vector<std::string> flushed;

    ConntrackRerouter rerouter(
        [&](const std::vector<std::string>& n) { return source.read(n); },
        [&](const std::string& ip) { flushed.push_back(ip); });

    rerouter.poll({"kpbr4d_a"});
    rerouter.poll({"kpbr4d_a"});
    CHECK(rerouter.poll({"kpbr4d_a"}) == 0);
    CHECK(flushed.empty());
}

TEST_CASE("ConntrackRerouter: empty set name list is a no-op") {
    ConntrackRerouter rerouter(
        [](const std::vector<std::string>&) {
            return std::map<std::string, std::vector<std::string>>{};
        },
        [](const std::string&) {});
    CHECK(rerouter.poll({}) == 0);
}
