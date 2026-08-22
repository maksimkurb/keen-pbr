#include <doctest/doctest.h>

#include "runtime/conntrack_manager.hpp"

#include <vector>
#include <string>

namespace keen_pbr3 {

TEST_CASE("ConntrackManager reconciles only policy changes") {
    ConntrackManager manager;
    const ConntrackPolicy enabled{true};

    CHECK(manager.reconcile(enabled));
    CHECK(manager.inspect() == enabled);
    CHECK_FALSE(manager.reconcile(enabled));
    CHECK(manager.reconcile(ConntrackPolicy{}));
    CHECK_FALSE(manager.inspect().bypass_established_or_dnat);
}

TEST_CASE("ConntrackManager deletes only the requested mark in both families") {
    std::vector<std::vector<std::string>> commands;
    ConntrackManager manager([&commands](const std::vector<std::string>& args) {
        commands.push_back(args);
        return ConntrackManager::CommandResult{0, {}};
    });

    CHECK(manager.delete_mark(0x120000, 0xFF0000));
    REQUIRE(commands.size() == 2);
    CHECK(commands[0] == std::vector<std::string>{"conntrack", "-D", "-f", "ipv4", "--mark", "1179648/16711680"});
    CHECK(commands[1] == std::vector<std::string>{"conntrack", "-D", "-f", "ipv6", "--mark", "1179648/16711680"});
}

TEST_CASE("ConntrackManager reports family cleanup failure without broad fallback") {
    ConntrackManager manager([](const std::vector<std::string>& args) {
        return ConntrackManager::CommandResult{args[3] == "ipv6" ? 1 : 0,
                                               "netlink error"};
    });
    CHECK_FALSE(manager.delete_mark(0x120000, 0xFF0000));
}

TEST_CASE("ConntrackManager treats an already-empty family as cleanup success") {
    ConntrackManager manager([](const std::vector<std::string>&) {
        return ConntrackManager::CommandResult{
            1,
            "conntrack v1.4.8 (conntrack-tools): 0 flow entries have been deleted.\n"};
    });
    CHECK(manager.delete_mark(0x120000, 0xFF0000));
}

TEST_CASE("ConntrackManager does not hide other conntrack exit status one errors") {
    ConntrackManager manager([](const std::vector<std::string>&) {
        return ConntrackManager::CommandResult{1, "Operation not permitted\n"};
    });
    CHECK_FALSE(manager.delete_mark(0x120000, 0xFF0000));
}

TEST_CASE("ConntrackManager preserves foreign mark bits while restoring original direction") {
    constexpr uint32_t owned = 0x00FF0000U;
    CHECK(ConntrackManager::restore_original_mark(0xA50000CCU, 0x00340011U, owned) ==
          0xA53400CCU);
    CHECK(ConntrackManager::save_selected_mark(0x5A0000AAU, 0x00BC00DDU, owned) ==
          0x5ABC00AAU);
}

TEST_CASE("Test-group conntrack cleanup distinguishes failure from healthy switching") {
    const auto initial = classify_test_group_switch(false, false);
    CHECK(initial == TestGroupSwitchReason::initial);
    CHECK_FALSE(should_delete_test_group_conntrack(initial, false));
    CHECK_FALSE(should_delete_test_group_conntrack(initial, true));

    const auto failed = classify_test_group_switch(true, false);
    CHECK(failed == TestGroupSwitchReason::selected_path_unhealthy);
    CHECK(should_delete_test_group_conntrack(failed, false));
    CHECK(should_delete_test_group_conntrack(failed, true));

    const auto healthy = classify_test_group_switch(true, true);
    CHECK(healthy == TestGroupSwitchReason::healthy_policy_change);
    CHECK_FALSE(should_delete_test_group_conntrack(healthy, false));
    CHECK(should_delete_test_group_conntrack(healthy, true));
}

} // namespace keen_pbr3
