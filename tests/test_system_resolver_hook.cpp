#include "../src/daemon/system_resolver_hook.hpp"

#include <doctest/doctest.h>

namespace keen_pbr3 {

TEST_CASE("build_system_resolver_reload_command: empty when resolver config absent") {
    Config cfg;
    CHECK(build_system_resolver_reload_command(cfg).empty());
}

TEST_CASE("build_system_resolver_reload_command: uses '<hook> reload'") {
    Config cfg;
    cfg.dns = DnsConfig{};
    cfg.dns->system_resolver = api::SystemResolver{};
    cfg.dns->system_resolver->hook = "/usr/local/bin/resolver-hook";

    CHECK(build_system_resolver_reload_command(cfg) == "/usr/local/bin/resolver-hook reload");
}

TEST_CASE("execute_system_resolver_reload_hook: succeeds for zero exit code") {
    Config cfg;
    cfg.dns = DnsConfig{};
    cfg.dns->system_resolver = api::SystemResolver{};
    cfg.dns->system_resolver->hook = "resolver-hook";

    std::string observed_command;
    std::string command;
    int exit_code = -1;

    const bool ok = execute_system_resolver_reload_hook(
        cfg,
        [&observed_command](const std::string& cmd) {
            observed_command = cmd;
            return 0;
        },
        command,
        exit_code);

    CHECK(ok);
    CHECK(command == "resolver-hook reload");
    CHECK(observed_command == "resolver-hook reload");
    CHECK(exit_code == 0);
}

TEST_CASE("execute_system_resolver_reload_hook: fails but remains non-throwing") {
    Config cfg;
    cfg.dns = DnsConfig{};
    cfg.dns->system_resolver = api::SystemResolver{};
    cfg.dns->system_resolver->hook = "resolver-hook";

    std::string command;
    int exit_code = -1;

    const bool ok = execute_system_resolver_reload_hook(
        cfg,
        [](const std::string&) {
            return 17;
        },
        command,
        exit_code);

    CHECK_FALSE(ok);
    CHECK(command == "resolver-hook reload");
    CHECK(exit_code == 17);
}

} // namespace keen_pbr3
