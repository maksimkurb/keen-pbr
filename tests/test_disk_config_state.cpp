#include <doctest/doctest.h>

#include "../src/daemon/disk_config_state.hpp"

#include <cstdio>
#include <fstream>
#include <unistd.h>

using namespace keen_pbr3;

TEST_CASE("disk config inspector compares JSON without mutating active config") {
    const std::string path = "/tmp/keen-pbr-disk-config-" + std::to_string(getpid()) + ".json";
    Config active;
    active.daemon = DaemonConfig{};
    active.daemon->pid_file = "/run/keen-pbr.pid";

    { std::ofstream output(path); output << nlohmann::json(active).dump(); }
    CHECK(inspect_disk_config_state(path, active).matches_active);

    { std::ofstream output(path); output << R"({"daemon":{"pid_file":"/other.pid"}})"; }
    const auto mismatch = inspect_disk_config_state(path, active);
    CHECK_FALSE(mismatch.matches_active);
    CHECK(mismatch.error.empty());
    std::remove(path.c_str());
}

TEST_CASE("disk config inspector reports unreadable paths") {
    const auto state = inspect_disk_config_state("/missing/keen-pbr-config.json", Config{});
    CHECK_FALSE(state.matches_active);
    CHECK_FALSE(state.error.empty());
}

TEST_CASE("disk config inspector reports invalid JSON") {
    const std::string path = "/tmp/keen-pbr-disk-config-invalid-" + std::to_string(getpid()) + ".json";
    { std::ofstream output(path); output << "not JSON"; }
    const auto state = inspect_disk_config_state(path, Config{});
    CHECK_FALSE(state.matches_active);
    CHECK_FALSE(state.error.empty());
    std::remove(path.c_str());
}
