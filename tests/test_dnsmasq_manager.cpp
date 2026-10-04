#include <doctest/doctest.h>

#include "../src/daemon/dnsmasq_manager.hpp"

#include <unistd.h>

#include <filesystem>
#include <string>
#include <vector>

using namespace keen_pbr3;

namespace {

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() /
               ("keen-pbr-test-dnsmasq-manager-" + std::to_string(::getpid()));
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

struct FakeHook {
    std::vector<std::vector<std::string>> calls;
    ExecCaptureResult result = [] {
        ExecCaptureResult ok;
        ok.exit_code = 0;
        return ok;
    }();

    DnsmasqExecFn fn() {
        return [this](const std::vector<std::string>& args) {
            calls.push_back(args);
            return result;
        };
    }
};

Config make_config(const std::string& mode, const std::string& domain) {
    std::string json = R"({
        "lists": {"l": {"domains": [")" + domain + R"("]}},
        "dns": {
            "servers": [{"tag":"up","address":"1.1.1.1"}],
            "rules": [{"list":["l"],"server":"up"}])";
    if (!mode.empty()) {
        json += R"(, "resolver_integration": ")" + mode + R"(")";
    }
    json += "}}";
    return parse_config(json);
}

// Runs one blocking sync with a registry/streamer built from the config.
void run_sync(DnsmasqManager& manager, const Config& config) {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);
    const DnsServerRegistry registry(config.dns.value_or(DnsConfig{}));
    manager.sync(config, registry, streamer);
}

bool dir_is_empty(const std::filesystem::path& dir) {
    return std::filesystem::directory_iterator(dir) == std::filesystem::directory_iterator();
}

} // namespace

TEST_CASE("dnsmasq manager: first sync applies once without writing files") {
    TempDir dir;
    FakeHook hook;
    DnsmasqManager manager("/hook", hook.fn());
    run_sync(manager, make_config("dnsmasq", "example.com"));

    REQUIRE(hook.calls.size() == 1);
    CHECK(hook.calls[0] == std::vector<std::string>{"/hook", "apply"});
    CHECK(dir_is_empty(dir.path));

    const auto status = manager.status();
    CHECK(status.mode == ResolverIntegrationMode::DNSMASQ);
    CHECK(status.state == DnsmasqSyncState::Ok);
    CHECK(status.config_hash.size() == 32);
    CHECK(status.last_apply_ts.has_value());
    CHECK(status.last_error.empty());
    CHECK(status.rules == 1);
    CHECK(status.domains == 1);
}

TEST_CASE("dnsmasq manager: unchanged config does not run the hook again") {
    TempDir dir;
    FakeHook hook;
    DnsmasqManager manager("/hook", hook.fn());
    const Config config = make_config("dnsmasq", "example.com");
    run_sync(manager, config);
    run_sync(manager, config);
    CHECK(hook.calls.size() == 1);
    CHECK(manager.status().state == DnsmasqSyncState::Ok);
}

TEST_CASE("dnsmasq manager: absent integration with dns.rules means dnsmasq") {
    TempDir dir;
    FakeHook hook;
    DnsmasqManager manager("/hook", hook.fn());
    run_sync(manager, make_config("", "example.com"));
    CHECK(hook.calls.size() == 1);
}

TEST_CASE("dnsmasq manager: changed rule applies again") {
    TempDir dir;
    FakeHook hook;
    DnsmasqManager manager("/hook", hook.fn());
    run_sync(manager, make_config("dnsmasq", "example.com"));
    const auto first_hash = manager.status().config_hash;
    run_sync(manager, make_config("dnsmasq", "changed.example"));
    REQUIRE(hook.calls.size() == 2);
    CHECK(hook.calls[1][1] == "apply");
    CHECK(manager.status().config_hash != first_hash);
}

TEST_CASE("dnsmasq manager: hook failure is reported and retried") {
    TempDir dir;
    FakeHook hook;
    hook.result.exit_code = 3;
    hook.result.stdout_output = "dnsmasq restart failed\n";
    DnsmasqManager manager("/hook", hook.fn());
    const Config config = make_config("dnsmasq", "example.com");

    run_sync(manager, config);
    auto status = manager.status();
    CHECK(status.state == DnsmasqSyncState::Error);
    CHECK(status.last_error == "hook exited 3: dnsmasq restart failed");
    CHECK(status.config_hash.empty());
    CHECK_FALSE(status.last_apply_ts.has_value());

    hook.result.exit_code = 0;
    hook.result.stdout_output.clear();
    run_sync(manager, config);
    CHECK(hook.calls.size() == 2);
    status = manager.status();
    CHECK(status.state == DnsmasqSyncState::Ok);
    CHECK(status.last_error.empty());
    CHECK(status.last_apply_ts.has_value());
}

TEST_CASE("dnsmasq manager: switching to none removes the config") {
    TempDir dir;
    FakeHook hook;
    DnsmasqManager manager("/hook", hook.fn());
    run_sync(manager, make_config("dnsmasq", "example.com"));
    REQUIRE(hook.calls.size() == 1);

    run_sync(manager, make_config("none", "example.com"));
    REQUIRE(hook.calls.size() == 2);
    CHECK(hook.calls[1] == std::vector<std::string>{"/hook", "remove"});
    CHECK(dir_is_empty(dir.path));
    const auto status = manager.status();
    CHECK(status.mode == ResolverIntegrationMode::NONE);
    CHECK(status.state == DnsmasqSyncState::Disabled);
    CHECK(status.config_hash.empty());

    // Re-enabling installs again because the applied hash was cleared.
    run_sync(manager, make_config("dnsmasq", "example.com"));
    CHECK(hook.calls.size() == 3);
    CHECK(hook.calls[2][1] == "apply");
}

TEST_CASE("dnsmasq manager: none removes on first sync only") {
    TempDir dir;
    FakeHook hook;
    DnsmasqManager manager("/hook", hook.fn());
    run_sync(manager, make_config("none", "example.com"));
    REQUIRE(hook.calls.size() == 1);
    CHECK(hook.calls[0] == std::vector<std::string>{"/hook", "remove"});
    CHECK(manager.status().state == DnsmasqSyncState::Disabled);

    run_sync(manager, make_config("none", "example.com"));
    CHECK(hook.calls.size() == 1);
    CHECK(dir_is_empty(dir.path));
}

TEST_CASE("dnsmasq manager: none with empty hook path calls nothing") {
    FakeHook hook;
    DnsmasqManager manager("", hook.fn());
    run_sync(manager, make_config("none", "example.com"));
    CHECK(hook.calls.empty());
}

TEST_CASE("dnsmasq manager: empty hook path in dnsmasq mode is an error") {
    TempDir dir;
    FakeHook hook;
    DnsmasqManager manager("", hook.fn());
    run_sync(manager, make_config("dnsmasq", "example.com"));
    CHECK(hook.calls.empty());
    const auto status = manager.status();
    CHECK(status.mode == ResolverIntegrationMode::DNSMASQ);
    CHECK(status.state == DnsmasqSyncState::Error);
    CHECK(status.last_error == "no dnsmasq hook for this platform");
}

TEST_CASE("dnsmasq manager: request_sync coalesces queued requests") {
    TempDir dir;
    FakeHook hook;
    DnsmasqManager manager("/hook", hook.fn());
    CacheManager cache("/nonexistent/cache");

    std::vector<std::function<void()>> queue;
    const DnsmasqPostFn post = [&queue](std::function<void()> task) {
        queue.push_back(std::move(task));
        return true;
    };
    manager.request_sync(make_config("dnsmasq", "first.example"), cache, post);
    manager.request_sync(make_config("dnsmasq", "second.example"), cache, post);
    REQUIRE(queue.size() == 1);
    queue[0]();

    REQUIRE(hook.calls.size() == 1);

    // A finished run lets the next request queue a new job.
    manager.request_sync(make_config("dnsmasq", "third.example"), cache, post);
    CHECK(queue.size() == 2);
}
