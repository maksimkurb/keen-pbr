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

// ---------------------------------------------------------------------------
// Outbounds shared by two balance groups (open question W6).
//
// Pins CURRENT behaviour, which is believed to be a bug: probe health is kept
// per group (UrltestState inside UrltestManager), but the connmark a balance
// classifier saves, and the conntrack flush that follows a failure, are keyed
// by the member OUTBOUND's mark. The flush is inline in
// Daemon::handle_urltest_selection_change (src/daemon/daemon_runtime.cpp),
// so these tests drive the same building blocks (UrltestState,
// newly_failed_children, allocate_outbound_marks, ConntrackManager with a
// recording runner) through a helper that mirrors that code.
// ---------------------------------------------------------------------------

#include "../src/config/config.hpp"
#include "../src/routing/urltest_manager.hpp"
#include "../src/runtime/conntrack_manager.hpp"

namespace {

OutboundGroup members_of(std::initializer_list<const char*> tags) {
    OutboundGroup group;
    std::vector<api::OutboundGroupMemberElement> members;
    for (const char* tag : tags) {
        api::OutboundGroupMemberElement member;
        member.outbound = tag;
        members.push_back(member);
    }
    group.members = std::move(members);
    return group;
}

Outbound interface_outbound(const char* tag) {
    Outbound outbound;
    outbound.type = OutboundType::INTERFACE;
    outbound.tag = tag;
    outbound.interface = tag;
    return outbound;
}

Outbound balance_group(const char* tag, std::initializer_list<const char*> tags) {
    Outbound outbound;
    outbound.type = OutboundType::URLTEST;
    outbound.tag = tag;
    outbound.strategy = api::Strategy::BALANCE;
    outbound.outbound_groups = std::vector<OutboundGroup>{members_of(tags)};
    return outbound;
}

URLTestResult probe(bool success) {
    URLTestResult result;
    result.success = success;
    result.latency_ms = 10;
    return result;
}

UrltestState group_state(const Outbound& config,
                         std::map<std::string, bool> member_up) {
    UrltestState state;
    state.config = config;
    for (const auto& [tag, up] : member_up) {
        state.last_results[tag] = probe(up);
        state.circuit_breakers.emplace(tag, CircuitBreaker(CircuitBreakerConfig{}));
    }
    return state;
}

// Mirrors the failed-child detection in handle_urltest_selection_change.
std::set<std::string> failed_children_of(const UrltestState& state) {
    std::set<std::string> failed;
    for (const auto& group : state.config.outbound_groups.value_or(std::vector<OutboundGroup>{})) {
        for (const auto& tag : outbound_group_tags(group)) {
            const auto breaker = state.circuit_breakers.find(tag);
            const bool open = breaker != state.circuit_breakers.end() &&
                              breaker->second.state(tag) == CircuitState::open;
            const auto result = state.last_results.find(tag);
            if (open || (result != state.last_results.end() && !result->second.success)) {
                failed.insert(tag);
            }
        }
    }
    return failed;
}

struct SharedMemberFixture {
    FwmarkConfig fwmark;
    OutboundMarkMap marks;
    Outbound grp_a = balance_group("grp_a", {"wan1", "wan2"});
    Outbound grp_b = balance_group("grp_b", {"wan1", "wan3"});
    std::vector<std::vector<std::string>> conntrack_commands;
    ConntrackManager conntrack{[this](const std::vector<std::string>& args) {
        conntrack_commands.push_back(args);
        return ConntrackManager::CommandResult{0, {}};
    }};

    SharedMemberFixture() {
        marks = allocate_outbound_marks(
            fwmark, {interface_outbound("wan1"), interface_outbound("wan2"),
                     interface_outbound("wan3"), grp_a, grp_b});
    }

    // One handle_urltest_selection_change() cycle for a group: flush the marks
    // of members that newly failed, using the global fwmark mask.
    BalanceClassifierState cycle(const UrltestState& state,
                                 const BalanceClassifierState* applied) {
        BalanceClassifierState classifier;
        classifier.failed_children = failed_children_of(state);
        for (const auto& child : newly_failed_children(applied, classifier.failed_children)) {
            REQUIRE(conntrack.delete_mark(marks.at(child), fwmark_mask_value(fwmark)));
        }
        return classifier;
    }

    std::string mark_arg(const char* tag) const {
        return std::to_string(marks.at(tag)) + "/" +
               std::to_string(fwmark_mask_value(fwmark));
    }
};

} // namespace

TEST_CASE("shared member: both balance groups use the same per-outbound mark") {
    SharedMemberFixture fx;
    // The connmark saved for a connection depends only on the member
    // outbound, so a wan1 connection balanced by grp_a carries the same value
    // as one balanced by grp_b: they are indistinguishable in conntrack.
    CHECK(fx.marks.count("wan1") == 1);
    CHECK(fx.marks.at("wan1") != fx.marks.at("wan2"));
    CHECK(fx.marks.at("wan1") != fx.marks.at("wan3"));
    CHECK(fx.marks.at("wan1") != fx.marks.at("grp_a"));
    CHECK(fx.marks.at("wan1") != fx.marks.at("grp_b"));
}

TEST_CASE("shared member: health is kept per group, so two groups can disagree about it") {
    SharedMemberFixture fx;
    const auto a = group_state(fx.grp_a, {{"wan1", false}, {"wan2", true}});
    const auto b = group_state(fx.grp_b, {{"wan1", true}, {"wan3", true}});
    CHECK(failed_children_of(a) == std::set<std::string>{"wan1"});
    CHECK(failed_children_of(b).empty());
    CHECK(select_test_group_usable_outbounds(a) == std::vector<std::string>{"wan2"});
    CHECK(select_test_group_usable_outbounds(b) ==
          std::vector<std::string>{"wan1", "wan3"});
}

TEST_CASE("shared member: group A dropping wan1 flushes wan1's mark although group B keeps it") {
    SharedMemberFixture fx;
    const auto b_up = group_state(fx.grp_b, {{"wan1", true}, {"wan3", true}});
    const auto a_up = group_state(fx.grp_a, {{"wan1", true}, {"wan2", true}});
    const auto a_down = group_state(fx.grp_a, {{"wan1", false}, {"wan2", true}});

    // Both groups healthy: nothing flushed.
    const auto applied_a = fx.cycle(a_up, nullptr);
    (void)fx.cycle(b_up, nullptr);
    CHECK(fx.conntrack_commands.empty());

    // Group A's own probes fail wan1; group B still sees it healthy.
    const auto after_a = fx.cycle(a_down, &applied_a);
    // The flush is per mark/mask, not per group: both families, wan1's mark.
    REQUIRE(fx.conntrack_commands.size() == 2);
    CHECK(fx.conntrack_commands[0] ==
          std::vector<std::string>{"conntrack", "-D", "-f", "ipv4", "--mark",
                                   fx.mark_arg("wan1")});
    CHECK(fx.conntrack_commands[1] ==
          std::vector<std::string>{"conntrack", "-D", "-f", "ipv6", "--mark",
                                   fx.mark_arg("wan1")});
    // Group B still lists wan1 as usable, so the flush also killed flows
    // that grp_b legitimately balanced onto a healthy wan1.
    CHECK(select_test_group_usable_outbounds(b_up) ==
          std::vector<std::string>{"wan1", "wan3"});
    CHECK(after_a.failed_children == std::set<std::string>{"wan1"});
}

TEST_CASE("shared member: a still-failed member is flushed once, then not on later cycles") {
    SharedMemberFixture fx;
    const auto a_down = group_state(fx.grp_a, {{"wan1", false}, {"wan2", true}});
    const auto first = fx.cycle(a_down, nullptr);
    CHECK(fx.conntrack_commands.size() == 2);
    (void)fx.cycle(a_down, &first);
    CHECK(fx.conntrack_commands.size() == 2);
}

TEST_CASE("shared member: both groups failing wan1 flush its mark once per group") {
    SharedMemberFixture fx;
    const auto a_down = group_state(fx.grp_a, {{"wan1", false}, {"wan2", true}});
    const auto b_down = group_state(fx.grp_b, {{"wan1", false}, {"wan3", true}});
    (void)fx.cycle(a_down, nullptr);
    (void)fx.cycle(b_down, nullptr);
    // No cross-group dedup: the same mark/mask is deleted twice (4 commands).
    REQUIRE(fx.conntrack_commands.size() == 4);
    CHECK(fx.conntrack_commands[0] == fx.conntrack_commands[2]);
    CHECK(fx.conntrack_commands[1] == fx.conntrack_commands[3]);
}
