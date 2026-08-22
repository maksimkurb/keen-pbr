#include "../src/daemon/system_resolver_hook.hpp"
#include "../src/daemon/resolver_stream_wait.hpp"

#include <doctest/doctest.h>

#include <limits>

namespace keen_pbr3 {

TEST_CASE("resolver ready timeout defaults to 120 seconds") {
    Config cfg;
    CHECK(resolver_ready_timeout(cfg) ==
          std::chrono::seconds{kDefaultResolverReadyTimeoutSeconds});
}

TEST_CASE("resolver ready timeout honors configured seconds") {
    Config cfg;
    cfg.daemon = DaemonConfig{};
    cfg.daemon->resolver_ready_timeout_seconds = 1;
    CHECK(resolver_ready_timeout(cfg) == std::chrono::seconds{1});
}

TEST_CASE("resolver stream wait accepts delayed completion before deadline") {
    Config cfg;
    cfg.daemon = DaemonConfig{};
    cfg.daemon->resolver_ready_timeout_seconds = 10;
    std::uint64_t completions = 0;
    int pump_calls = 0;
    auto now = std::chrono::steady_clock::time_point{};
    CHECK(wait_for_resolver_stream_after(
        0, resolver_ready_timeout(cfg),
        [&completions] { return completions; },
        [&completions, &pump_calls, &now] {
            ++pump_calls;
            if (now.time_since_epoch() >= std::chrono::seconds{6}) {
                ++completions;
            }
        },
        [&now](std::chrono::milliseconds) { now += std::chrono::seconds{2}; },
        [&now] { return now; }));
    CHECK(now.time_since_epoch() == std::chrono::seconds{6});
    CHECK(pump_calls == 4);
}

TEST_CASE("resolver stream wait times out without completion") {
    int pump_calls = 0;
    auto now = std::chrono::steady_clock::time_point{};
    CHECK_FALSE(wait_for_resolver_stream_after(
        4, std::chrono::seconds{3},
        [] { return std::uint64_t{4}; },
        [&pump_calls] { ++pump_calls; },
        [&now](std::chrono::milliseconds) { now += std::chrono::seconds{1}; },
        [&now] { return now; }));
    CHECK(now.time_since_epoch() == std::chrono::seconds{3});
    CHECK(pump_calls == 3);
}

TEST_CASE("resolver stream wait saturates an extreme timeout safely") {
    CHECK(wait_for_resolver_stream_after(
        0, std::chrono::seconds{std::numeric_limits<std::int64_t>::max()},
        [] { return std::uint64_t{1}; },
        [] {},
        [](std::chrono::milliseconds) {},
        [] { return std::chrono::steady_clock::time_point{}; }));
}

TEST_CASE("build_system_resolver_reload_args: empty when resolver config absent") {
    Config cfg;
    CHECK(build_system_resolver_reload_args(cfg).empty());
}

TEST_CASE("build_system_resolver_reload_args: returns hook and reload as separate args") {
    Config cfg;
    cfg.dns = DnsConfig{};
    cfg.dns->system_resolver = api::SystemResolver{};

    const auto args = build_system_resolver_reload_args(cfg);
    REQUIRE(args.size() == 2);
    CHECK(args[0] == system_resolver_hook_path());
    CHECK(args[1] == "reload");
}

TEST_CASE("build_system_resolver_hook_args: preserves shutdown action") {
    Config cfg;
    cfg.dns = DnsConfig{};
    cfg.dns->system_resolver = api::SystemResolver{};

    const auto args = build_system_resolver_hook_args(cfg, "deactivate");
    REQUIRE(args.size() == 2);
    CHECK(args[0] == system_resolver_hook_path());
    CHECK(args[1] == "deactivate");
}

TEST_CASE("execute_system_resolver_reload_hook: succeeds for zero exit code") {
    Config cfg;
    cfg.dns = DnsConfig{};
    cfg.dns->system_resolver = api::SystemResolver{};

    std::vector<std::string> observed_args;
    std::string command;
    int exit_code = -1;

    const bool ok = execute_system_resolver_reload_hook(
        cfg,
        [&observed_args](const std::vector<std::string>& args) {
            observed_args = args;
            return 0;
        },
        command,
        exit_code);

    CHECK(ok);
    CHECK(command == std::string(system_resolver_hook_path()) + " reload");
    REQUIRE(observed_args.size() == 2);
    CHECK(observed_args[0] == system_resolver_hook_path());
    CHECK(observed_args[1] == "reload");
    CHECK(exit_code == 0);
}

TEST_CASE("execute_system_resolver_reload_hook: fails but remains non-throwing") {
    Config cfg;
    cfg.dns = DnsConfig{};
    cfg.dns->system_resolver = api::SystemResolver{};

    std::string command;
    int exit_code = -1;

    const bool ok = execute_system_resolver_reload_hook(
        cfg,
        [](const std::vector<std::string>&) {
            return 17;
        },
        command,
        exit_code);

    CHECK_FALSE(ok);
    CHECK(command == std::string(system_resolver_hook_path()) + " reload");
    CHECK(exit_code == 17);
}

} // namespace keen_pbr3
