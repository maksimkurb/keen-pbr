#include <doctest/doctest.h>

#include "../src/config/config.hpp"
#include "../src/firewall/iptables.hpp"
#include "../src/intercept/intercept_capabilities.hpp"
#include "../src/intercept/intercept_settings.hpp"
#include "../src/netfilter/conntrack.hpp"
#include "../src/netfilter/nfqueue.hpp"
#include "../src/util/firewall_backend_utils.hpp"
#include "../src/util/ipv6_support.hpp"
#include "../src/util/kernel_capabilities.hpp"
#include "../src/util/safe_exec.hpp"
#include "probe_fakes.hpp"

#include <sys/stat.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace keen_pbr3;
using namespace keen_pbr3::probe_fakes;

namespace {

// Fake `nft`, `iptables*`, `ipset` that accept everything, first on PATH.
struct FakeToolsPath {
    std::filesystem::path dir;
    std::string old_path;
    bool had_path{false};

    FakeToolsPath() {
        dir = std::filesystem::temp_directory_path() /
              ("keen-pbr-kernel-caps-" + std::to_string(static_cast<long long>(getpid())));
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        for (const char* name : {"nft", "iptables", "iptables-restore", "ip6tables",
                                 "ip6tables-restore", "ipset"}) {
            const auto path = dir / name;
            std::ofstream(path) << "#!/bin/sh\ncat >/dev/null 2>&1 </dev/null\nexit 0\n";
            ::chmod(path.c_str(), 0755);
        }
        if (const char* current = std::getenv("PATH")) {
            had_path = true;
            old_path = current;
        }
        REQUIRE(setenv("PATH", dir.c_str(), 1) == 0);
        reset_kernel_capabilities_for_tests();
    }
    ~FakeToolsPath() {
        if (had_path) {
            (void)setenv("PATH", old_path.c_str(), 1);
        } else {
            (void)unsetenv("PATH");
        }
        std::filesystem::remove_all(dir);
        safe_exec_observer_for_tests() = nullptr;
        reset_kernel_capabilities_for_tests();
    }
};

} // namespace

TEST_CASE("every capability probe runs exactly once across startup, applies, refresh and re-bind") {
    FakeToolsPath fake;
    std::map<std::string, int> commands;
    safe_exec_observer_for_tests() = [&commands](const std::vector<std::string>& args) {
        ++commands[safe_exec_command_string(args)];
    };

    // Service start: tools, IPv6 per backend, xt_comment, xt_statistic.
    const std::uint64_t runs_before = kernel_capability_probe_runs();
    const auto snapshot = kernel_capabilities();
    CHECK(kernel_capability_probe_runs() == runs_before + 1);
    CHECK(snapshot->tools.nft);
    CHECK(snapshot->tools.ipset);
    CHECK(snapshot->xt_statistic);
    CHECK(snapshot->iptables_ipv6 == snapshot->system_ipv6);

    // Interception probe at start: scratch set, netlink checks, modules.
    int scratch_created = 0;
    int scratch_destroyed = 0;
    int transports = 0;
    int modprobes = 0;
    InterceptProbeEnv env;
    env.runtime_probes = true;
    env.modprobe = [&modprobes](const std::string&) { ++modprobes; };
    env.kernel_release = [] { return std::string("test"); };
    env.create_scratch_set = [&scratch_created] { ++scratch_created; return true; };
    env.destroy_scratch_set = [&scratch_destroyed] { ++scratch_destroyed; };
    env.make_transport = [&transports]() -> std::unique_ptr<nfnl::SetWriterTransport> {
        ++transports;
        auto transport = std::make_unique<FakeTransport>();
        transport->handler = [](const MsgView& message) {
            if (message.type == kIpsetProtocolMsg) return Reply{0, 7, false};
            return Reply{};
        };
        return transport;
    };
    env.ip_targets = env.ip6_targets = "/nonexistent";
    env.ip_matches = env.ip6_matches = "/nonexistent";
    const auto startup =
        probe_intercept_startup(FirewallBackend::iptables, snapshot->system_ipv6, env);
    CHECK(scratch_created == 1);
    CHECK(scratch_destroyed == 1);
    CHECK(modprobes > 0);
    const int transports_at_start = transports;
    const int modprobes_at_start = modprobes;
    CHECK(startup.with_ipv6.probe.set_write.status == nfnl::ProbeStatus::ok);

    // Whatever probed so far did so once per command.
    for (const auto& entry : commands) {
        INFO(entry.first);
        CHECK(entry.second == 1);
    }
    const auto spawns_at_start = safe_exec_spawn_count().load();

    // Later: several applies, an interface refresh, a listener re-bind, config
    // validations and status calls only read the cached answers.
    Config config;
    for (int round = 0; round < 5; ++round) {
        CHECK_NOTHROW((void)resolve_ipv6_support(config));
        CHECK_NOTHROW((void)resolve_firewall_backend(FirewallBackendPreference::auto_detect));
        CHECK_NOTHROW((void)resolve_firewall_backend(FirewallBackendPreference::iptables));
        CHECK_NOTHROW(
            require_iptables_balance_support(FirewallBackend::iptables, true));
        (void)kernel_capabilities();
        // Same resolution an apply / refresh / re-bind does.
        InterceptRuntimeProbe listeners;
        listeners.nfqueue = nfnl::make_probe_result(nfnl::ProbeStatus::error, "bind failed");
        listeners.forget_blocking_listener_results();
        CHECK(listeners.nfqueue.status == nfnl::ProbeStatus::not_run);
        const auto effective = resolve_effective_intercept(
            config, FirewallBackend::iptables, startup.for_ipv6(round % 2 == 0));
        (void)effective;
    }
    CHECK(kernel_capability_probe_runs() == runs_before + 1);
    CHECK(safe_exec_spawn_count().load() == spawns_at_start);
    CHECK(transports == transports_at_start);
    CHECK(modprobes == modprobes_at_start);
    CHECK(scratch_created == 1);
}

TEST_CASE("backend detection and validation read the cached tools without spawning") {
    FakeToolsPath fake;
    const auto spawns = safe_exec_spawn_count().load();
    HostTools tools;
    tools.iptables = true;
    set_host_tools_for_tests(tools);
    for (int i = 0; i < 3; ++i) {
        CHECK(resolve_firewall_backend(FirewallBackendPreference::auto_detect) ==
              FirewallBackend::iptables);
        CHECK_THROWS_AS((void)resolve_firewall_backend(FirewallBackendPreference::nftables),
                        FirewallError);
    }
    tools.nft = true;
    set_host_tools_for_tests(tools);
    CHECK(resolve_firewall_backend(FirewallBackendPreference::auto_detect) ==
          FirewallBackend::nftables);
    CHECK(safe_exec_spawn_count().load() == spawns);
}

TEST_CASE("required tools per backend are listed once") {
    HostTools tools;
    tools.nft = true;
    CHECK(tools.missing_required(FirewallBackend::nftables).empty());
    CHECK(tools.missing_required(FirewallBackend::iptables) ==
          "iptables, iptables-restore, ipset");
    tools.iptables = tools.iptables_restore = true;
    CHECK(tools.missing_required(FirewallBackend::iptables) == "ipset");
}

TEST_CASE("a failed listener bind is forgotten for the retry, kernel facts are not") {
    InterceptRuntimeProbe probe;
    probe.set_backend = nfnl::make_probe_result(nfnl::ProbeStatus::ok);
    probe.set_write = nfnl::make_probe_result(nfnl::ProbeStatus::unsupported, "no");
    probe.nfqueue = nfnl::make_probe_result(nfnl::ProbeStatus::error, "bind failed");
    probe.fail_open = nfnl::make_probe_result(nfnl::ProbeStatus::ok);
    probe.nflog = nfnl::make_probe_result(nfnl::ProbeStatus::ok);
    probe.forget_blocking_listener_results();
    CHECK(probe.nfqueue.status == nfnl::ProbeStatus::not_run);
    CHECK(probe.fail_open.status == nfnl::ProbeStatus::not_run);
    CHECK(probe.nflog.status == nfnl::ProbeStatus::ok);
    CHECK(probe.set_backend.status == nfnl::ProbeStatus::ok);
    CHECK(probe.set_write.blocks());
}

TEST_CASE("scratch set probe that cannot create its set is skipped, not blocking") {
    InterceptProbeEnv env;
    env.create_scratch_set = [] { return false; };
    env.destroy_scratch_set = [] { FAIL("nothing to destroy"); };
    const auto result = probe_intercept_scratch_set(FirewallBackend::iptables, env);
    CHECK(result.set_write.status == nfnl::ProbeStatus::skipped);
    CHECK_FALSE(result.set_write.blocks());
}

TEST_CASE("learned kernel facts are process wide") {
    nfnl::reset_conntrack_kernel_filter_state_for_tests();
    CHECK_FALSE(nfnl::conntrack_kernel_filter_unsupported());
    nfnl::note_conntrack_kernel_filter_unsupported();
    CHECK(nfnl::conntrack_kernel_filter_unsupported());
    nfnl::reset_conntrack_kernel_filter_state_for_tests();
    nfnl::reset_nfqueue_gso_state_for_tests();
    CHECK_FALSE(nfnl::nfqueue_gso_rejected());
}
