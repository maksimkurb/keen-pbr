#include <doctest/doctest.h>

#include "../src/daemon/lifecycle_stages.hpp"
#include "../src/daemon/system_resolver_hook.hpp"
#include "../src/resolver/dnsmasq_integration.hpp"
#include "../src/resolver/resolver_integration.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace keen_pbr3 {

namespace {

constexpr auto kDnsmasq = api::ResolverIntegration::DNSMASQ;
constexpr auto kNone = api::ResolverIntegration::NONE;

Config make_config(api::ResolverIntegration mode, bool with_resolver = true,
                   int ready_timeout_seconds = 2) {
    Config config;
    config.dns = DnsConfig{};
    config.dns->resolver_integration = mode;
    if (with_resolver) {
        api::SystemResolver resolver;
        resolver.address = "127.0.0.1:5300";
        config.dns->system_resolver = resolver;
    }
    config.daemon = DaemonConfig{};
    config.daemon->resolver_ready_timeout_seconds = ready_timeout_seconds;
    return config;
}

// Records every hook invocation.  `on_hook` may simulate dnsmasq streaming the
// configuration back through the control socket.
struct FakeResolver {
    std::mutex mutex;
    std::vector<std::vector<std::string>> calls;
    std::function<void(const std::string& action)> on_hook;
    int exit_code{0};
    std::atomic<int> snapshot_refreshes{0};
    ResolverIntegration* integration{nullptr};
    bool saw_hook_in_flight{false};

    std::vector<std::string> actions() {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<std::string> result;
        for (const auto& call : calls) {
            result.push_back(call.size() > 1 ? call[1] : std::string{});
        }
        return result;
    }

    ResolverIntegrationDeps deps() {
        ResolverIntegrationDeps deps;
        deps.host.on_control_thread = [] { return false; };
        deps.host.runtime_generation = [] { return std::uint64_t{1}; };
        deps.host.routing_runtime_active = [] { return true; };
        deps.host.post_control_task = [](std::function<void()>, const std::string&) {
            return true;
        };
        deps.host.refresh_generation_snapshot = [this] { ++snapshot_refreshes; };
        deps.hook_executor = [this](const std::vector<std::string>& args) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                calls.push_back(args);
            }
            if (integration != nullptr && integration->hook_in_flight()) {
                saw_hook_in_flight = true;
            }
            const std::string action = args.size() > 1 ? args[1] : std::string{};
            if (on_hook) on_hook(action);
            return exit_code;
        };
        return deps;
    }
};

} // namespace

TEST_CASE("NoResolverIntegration is inert and reports disabled health") {
    FakeResolver fake;
    auto integration = make_resolver_integration(kNone, fake.deps());
    fake.integration = integration.get();
    integration->configure(make_config(kNone));

    CHECK(integration->mode() == kNone);
    CHECK_FALSE(integration->enabled());
    CHECK_FALSE(integration->active());

    integration->prepare_runtime();
    integration->apply_started(100);
    integration->runtime_running();
    integration->runtime_stopping();
    integration->refresh_health_async();
    const auto reload = integration->reload({"test"});
    CHECK(reload.ok);
    CHECK_FALSE(reload.performed);
    const auto verify = integration->verify({"test"});
    CHECK(verify.ok);
    CHECK_FALSE(verify.performed);
    bool async_ran = false;
    integration->reload_async({"test"}, [&](ResolverReloadResult result) {
        async_ran = true;
        CHECK(result.ok);
        CHECK_FALSE(result.performed);
    });
    CHECK(async_ran);
    CHECK(integration->fallback());
    CHECK(integration->deactivate());
    CHECK_FALSE(integration->hook_in_flight());
    CHECK_FALSE(integration->accept_generated_config("abc"));

    CHECK(fake.actions().empty());  // no hook was ever invoked
    CHECK(fake.snapshot_refreshes == 0);

    const auto health = integration->health();
    CHECK(health.mode == kNone);
    CHECK(health.probe_status == api::ResolverConfigProbeStatus::DISABLED);
    CHECK(health.expected_hash.empty());
    CHECK(health.actual_hash.empty());
    CHECK_FALSE(health.apply_started_ts.has_value());
    CHECK_FALSE(health.sync_state.has_value());
}

TEST_CASE("lifecycle stages: resolver stages exist only with an integration") {
    const auto ids = [](const std::vector<LifecycleOperationStage>& stages) {
        std::vector<std::string> result;
        for (const auto& stage : stages) result.push_back(stage.id);
        return result;
    };
    const auto has_resolver_stage = [&](LifecycleOperationType type, bool enabled) {
        for (const auto& id : ids(lifecycle_stages(type, enabled))) {
            if (id == "reload_dnsmasq" || id == "verify_dnsmasq" || id == "reload_fallback") {
                return true;
            }
        }
        return false;
    };
    for (const auto type : {LifecycleOperationType::ApplyConfig,
                            LifecycleOperationType::RollbackConfig,
                            LifecycleOperationType::Restart, LifecycleOperationType::Start,
                            LifecycleOperationType::Stop}) {
        CHECK_FALSE(has_resolver_stage(type, false));
        CHECK(has_resolver_stage(type, true));
    }
    CHECK(ids(lifecycle_stages(LifecycleOperationType::Stop, false)) ==
          std::vector<std::string>{"stop_routing"});
    CHECK(ids(lifecycle_stages(LifecycleOperationType::Start, true)) ==
          std::vector<std::string>{"start_routing", "reload_dnsmasq", "verify_dnsmasq"});
    CHECK(ids(lifecycle_stages(LifecycleOperationType::Stop, true)) ==
          std::vector<std::string>{"stop_routing", "reload_fallback"});
}

TEST_CASE("DnsmasqIntegration activates on the first reload and reloads afterwards") {
    FakeResolver fake;
    auto integration = make_resolver_integration(kDnsmasq, fake.deps());
    fake.integration = integration.get();
    // dnsmasq answers every hook by streaming its configuration.
    fake.on_hook = [&](const std::string&) { integration->accept_generated_config("hash-1"); };
    integration->configure(make_config(kDnsmasq));

    CHECK(integration->mode() == kDnsmasq);
    CHECK(integration->enabled());
    CHECK(integration->active());

    integration->apply_started(500);
    const auto first = integration->reload({"first"});
    CHECK(first.ok);
    CHECK(first.performed);
    const auto second = integration->reload({"second"});
    CHECK(second.ok);
    CHECK(integration->fallback());

    CHECK(fake.actions() == std::vector<std::string>{"activate", "reload", "reload"});
    CHECK(fake.saw_hook_in_flight);
    CHECK_FALSE(integration->hook_in_flight());
    CHECK(fake.snapshot_refreshes == 2);

    const auto health = integration->health();
    CHECK(health.mode == kDnsmasq);
    CHECK(health.expected_hash == "hash-1");
    CHECK(health.apply_started_ts == 500);

    CHECK(fake.calls.front().front() == system_resolver_hook_path());
}

TEST_CASE("DnsmasqIntegration reload fails when the hook fails or no stream arrives") {
    {
        FakeResolver fake;
        fake.exit_code = 3;
        auto integration = make_resolver_integration(kDnsmasq, fake.deps());
        integration->configure(make_config(kDnsmasq));
        const auto result = integration->reload({"test"});
        CHECK_FALSE(result.ok);
        CHECK_FALSE(result.error.empty());
        CHECK_FALSE(integration->fallback());
    }
    {
        FakeResolver fake;  // hook succeeds but dnsmasq never streams
        auto integration = make_resolver_integration(kDnsmasq, fake.deps());
        integration->configure(make_config(kDnsmasq, true, 1));
        const auto result = integration->reload({"test"});
        CHECK_FALSE(result.ok);
    }
}

TEST_CASE("DnsmasqIntegration without a system resolver does nothing") {
    FakeResolver fake;
    auto integration = make_resolver_integration(kDnsmasq, fake.deps());
    integration->configure(make_config(kDnsmasq, /*with_resolver=*/false));
    CHECK(integration->enabled());
    CHECK_FALSE(integration->active());
    CHECK_FALSE(integration->reload({"test"}).performed);
    CHECK_FALSE(integration->verify({"test"}).performed);
    CHECK(integration->fallback());
    CHECK(integration->deactivate());
    CHECK(fake.actions().empty());
}

TEST_CASE("DnsmasqIntegration reload_async reports the result on its worker") {
    FakeResolver fake;
    auto integration = make_resolver_integration(kDnsmasq, fake.deps());
    fake.on_hook = [&](const std::string&) { integration->accept_generated_config("h"); };
    integration->configure(make_config(kDnsmasq));

    std::promise<ResolverReloadResult> done;
    integration->reload_async({"async"}, [&](ResolverReloadResult result) {
        done.set_value(std::move(result));
    });
    auto future = done.get_future();
    REQUIRE(future.wait_for(std::chrono::seconds{5}) == std::future_status::ready);
    const auto result = future.get();
    CHECK(result.ok);
    CHECK(result.performed);
}

TEST_CASE("reconfigure_resolver_integration: dnsmasq to none deactivates the old integration") {
    FakeResolver fake;
    const auto deps = fake.deps();
    std::unique_ptr<ResolverIntegration> current;

    CHECK(reconfigure_resolver_integration(current, make_config(kDnsmasq), deps));
    REQUIRE(current);
    CHECK(current->mode() == kDnsmasq);
    fake.integration = current.get();
    ResolverIntegration* const old_instance = current.get();
    fake.on_hook = [&](const std::string&) { old_instance->accept_generated_config("h"); };
    REQUIRE(current->reload({"test"}).ok);  // activate
    fake.calls.clear();

    // Same mode: the instance is kept and only receives the new settings.
    CHECK_FALSE(reconfigure_resolver_integration(current, make_config(kDnsmasq), deps));
    CHECK(current.get() == old_instance);
    CHECK(fake.actions().empty());

    // dnsmasq -> none: the old integration is deactivated (hook `deactivate`).
    CHECK(reconfigure_resolver_integration(current, make_config(kNone, false), deps));
    CHECK(current->mode() == kNone);
    CHECK_FALSE(current->enabled());
    CHECK(fake.actions() == std::vector<std::string>{"deactivate"});
    fake.calls.clear();

    // none -> none never touches a resolver.
    CHECK_FALSE(reconfigure_resolver_integration(current, make_config(kNone, false), deps));
    CHECK(fake.actions().empty());

    // none -> dnsmasq re-activates the hook on its first reload.
    CHECK(reconfigure_resolver_integration(current, make_config(kDnsmasq), deps));
    REQUIRE(current->mode() == kDnsmasq);
    ResolverIntegration* const new_instance = current.get();
    fake.on_hook = [&](const std::string&) { new_instance->accept_generated_config("h"); };
    REQUIRE(current->reload({"test"}).ok);
    CHECK(fake.actions() == std::vector<std::string>{"activate"});
}

TEST_CASE("reconfigure_resolver_integration: a failing deactivation keeps old ownership") {
    FakeResolver fake;
    const auto deps = fake.deps();
    std::unique_ptr<ResolverIntegration> current;
    CHECK(reconfigure_resolver_integration(current, make_config(kDnsmasq), deps));
    REQUIRE(current);
    ResolverIntegration* const old_instance = current.get();
    fake.integration = old_instance;
    fake.on_hook = [&](const std::string&) { old_instance->accept_generated_config("h"); };
    REQUIRE(current->reload({"test"}).ok);
    fake.calls.clear();
    fake.exit_code = 1;
    CHECK_THROWS(reconfigure_resolver_integration(current, make_config(kNone, false), deps));
    CHECK(current.get() == old_instance);
    CHECK(current->mode() == kDnsmasq);
    CHECK(fake.actions() == std::vector<std::string>{"deactivate"});

    fake.exit_code = 0;
    CHECK(reconfigure_resolver_integration(current, make_config(kNone, false), deps));
    CHECK(current->mode() == kNone);
}

TEST_CASE("same-mode dnsmasq config removal deactivates before clearing settings") {
    FakeResolver fake;
    const auto deps = fake.deps();
    std::unique_ptr<ResolverIntegration> current;
    CHECK(reconfigure_resolver_integration(current, make_config(kDnsmasq), deps));
    REQUIRE(current);
    ResolverIntegration* const instance = current.get();
    fake.integration = instance;
    fake.on_hook = [&](const std::string&) { instance->accept_generated_config("h"); };
    REQUIRE(current->reload({"test"}).ok);
    fake.calls.clear();

    CHECK_FALSE(reconfigure_resolver_integration(current, make_config(kDnsmasq, false), deps));
    CHECK(current.get() == instance);
    CHECK(current->mode() == kDnsmasq);
    CHECK_FALSE(current->active());
    CHECK(fake.actions() == std::vector<std::string>{"deactivate"});
}

TEST_CASE("reconfigure_resolver_integration uses the effective mode of the config") {
    FakeResolver fake;
    const auto deps = fake.deps();
    std::unique_ptr<ResolverIntegration> current;
    // No explicit mode but a system resolver: migration yields dnsmasq.
    Config migrated;
    migrated.dns = DnsConfig{};
    api::SystemResolver resolver;
    resolver.address = "127.0.0.1";
    migrated.dns->system_resolver = resolver;
    CHECK(reconfigure_resolver_integration(current, migrated, deps));
    CHECK(current->mode() == kDnsmasq);
    CHECK(reconfigure_resolver_integration(current, Config{}, deps));
    CHECK(current->mode() == kNone);
}

TEST_CASE("system resolver hook args by resolver presence") {
    CHECK(build_system_resolver_hook_args(false, "reload").empty());
    const auto args = build_system_resolver_hook_args(true, "activate");
    REQUIRE(args.size() == 2);
    CHECK(args[0] == system_resolver_hook_path());
    CHECK(args[1] == "activate");
}

} // namespace keen_pbr3
