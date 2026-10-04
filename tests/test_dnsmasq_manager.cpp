#include <doctest/doctest.h>

#include "../src/daemon/dnsmasq_manager.hpp"
#include "../src/dns/dnsmasq_gen.hpp"

#include <unistd.h>

#include <filesystem>
#include <functional>
#include <streambuf>
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

// Simulates a local dnsmasq plus the platform hook: `apply` restarts dnsmasq
// (advancing the boottime clock) and, unless a scenario overrides on_apply,
// makes it serve the stamp of `target_hash`.
struct FakeDnsmasq {
    std::int64_t now_ms = 1'000'000;
    std::int64_t unix_ts = 1'700'000'000;
    DnsTxtProbeResult served;
    std::string target_hash;
    std::function<void(FakeDnsmasq&)> on_apply;
    int probes = 0;
    int sleeps = 0;
    std::vector<std::vector<std::string>> calls;
    ExecCaptureResult hook_result = [] {
        ExecCaptureResult ok;
        ok.exit_code = 0;
        return ok;
    }();

    FakeDnsmasq() { serve_missing(); }

    int applies() const {
        int n = 0;
        for (const auto& call : calls) {
            if (call.size() > 1 && call[1] == "apply") ++n;
        }
        return n;
    }

    void serve_missing() {
        served = {};
        served.status = DnsTxtProbeStatus::Missing;
    }
    void serve_failed(const std::string& error) {
        served = {};
        served.status = DnsTxtProbeStatus::QueryFailed;
        served.error = error;
    }
    void serve_stamp(const std::string& hash, std::int64_t boottime_ms, std::int64_t ts) {
        served = {};
        served.status = DnsTxtProbeStatus::Ok;
        served.txt = hash + "|" + std::to_string(boottime_ms) + "|" + std::to_string(ts);
    }
    // dnsmasq restarted now and loaded `hash`.
    void restart_with(const std::string& hash) {
        now_ms += 1000;
        unix_ts += 1;
        serve_stamp(hash, now_ms, unix_ts);
    }

    DnsmasqExecFn exec() {
        return [this](const std::vector<std::string>& args) {
            calls.push_back(args);
            if (hook_result.exit_code == 0 && args.size() > 1) {
                if (args[1] == "apply") {
                    if (on_apply) {
                        on_apply(*this);
                    } else {
                        restart_with(target_hash);
                    }
                } else if (args[1] == "remove") {
                    now_ms += 1000;
                    serve_missing();
                }
            }
            return hook_result;
        };
    }
    DnsmasqProbeFn probe() {
        return [this] {
            ++probes;
            return served;
        };
    }
    DnsmasqClockFn clock() {
        return [this] { return now_ms; };
    }
    DnsmasqTiming timing() {
        DnsmasqTiming t;
        t.confirm_timeout = std::chrono::milliseconds(2000);
        t.confirm_interval = std::chrono::milliseconds(500);
        t.sleep = [this](std::chrono::milliseconds) { ++sleeps; };
        return t;
    }
};

struct Rig {
    FakeDnsmasq fake;
    DnsmasqManager manager;
    explicit Rig(const std::string& hook = "/hook")
        : manager(hook, fake.exec(), fake.probe(), fake.clock(), fake.timing()) {}
};

class DiscardBuf : public std::streambuf {
protected:
    int_type overflow(int_type c) override { return traits_type::not_eof(c); }
    std::streamsize xsputn(const char*, std::streamsize n) override { return n; }
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

// The hash the conf-script would report for `config` (same as the manager's).
std::string config_hash(const Config& config) {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);
    const DnsConfig dns = config.dns.value_or(DnsConfig{});
    const DnsServerRegistry registry(dns);
    const auto lists = config.lists.value_or(std::map<std::string, ListConfig>{});
    DnsmasqGenerator generator(registry, streamer, dns, lists);
    DiscardBuf discard;
    std::ostream out(&discard);
    return generator.generate(out);
}

// Runs one blocking sync with a registry/streamer built from the config.
void run_sync(Rig& rig, const Config& config) {
    rig.fake.target_hash = config_hash(config);
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);
    const DnsServerRegistry registry(config.dns.value_or(DnsConfig{}));
    rig.manager.sync(config, registry, streamer);
}

// Runs one blocking sync the way lifecycle operations do (bypassing backoff).
void run_lifecycle_sync(Rig& rig, const Config& config) {
    rig.fake.target_hash = config_hash(config);
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);
    const DnsServerRegistry registry(config.dns.value_or(DnsConfig{}));
    rig.manager.sync(config, registry, streamer, /*bypass_backoff=*/true);
}

// Runs the periodic check inline.
void run_check(Rig& rig) {
    std::vector<std::function<void()>> queue;
    rig.manager.request_check([&queue](std::function<void()> task) {
        queue.push_back(std::move(task));
        return true;
    });
    for (auto& task : queue) task();
}

bool dir_is_empty(const std::filesystem::path& dir) {
    return std::filesystem::directory_iterator(dir) == std::filesystem::directory_iterator();
}

constexpr std::int64_t kBackoffInitialMs = 5 * 60 * 1000;

} // namespace

TEST_CASE("dnsmasq manager: first sync applies once without writing files") {
    TempDir dir;
    Rig rig;
    auto& manager = rig.manager;
    auto& hook = rig.fake;
    run_sync(rig, make_config("dnsmasq", "example.com"));

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
    Rig rig;
    auto& manager = rig.manager;
    auto& hook = rig.fake;
    const Config config = make_config("dnsmasq", "example.com");
    run_sync(rig, config);
    run_sync(rig, config);
    CHECK(hook.calls.size() == 1);
    CHECK(manager.status().state == DnsmasqSyncState::Ok);
}

TEST_CASE("dnsmasq manager: absent integration with dns.rules means dnsmasq") {
    TempDir dir;
    Rig rig;
    auto& manager = rig.manager;
    auto& hook = rig.fake;
    run_sync(rig, make_config("", "example.com"));
    CHECK(hook.calls.size() == 1);
}

TEST_CASE("dnsmasq manager: changed rule applies again") {
    TempDir dir;
    Rig rig;
    auto& manager = rig.manager;
    auto& hook = rig.fake;
    run_sync(rig, make_config("dnsmasq", "example.com"));
    const auto first_hash = manager.status().config_hash;
    run_sync(rig, make_config("dnsmasq", "changed.example"));
    REQUIRE(hook.calls.size() == 2);
    CHECK(hook.calls[1][1] == "apply");
    CHECK(manager.status().config_hash != first_hash);
}

TEST_CASE("dnsmasq manager: hook failure is reported and retried after the backoff") {
    TempDir dir;
    Rig rig;
    auto& manager = rig.manager;
    auto& hook = rig.fake;
    hook.hook_result.exit_code = 3;
    hook.hook_result.stdout_output = "dnsmasq restart failed\n";
    const Config config = make_config("dnsmasq", "example.com");

    run_sync(rig, config);
    auto status = manager.status();
    CHECK(status.state == DnsmasqSyncState::Error);
    CHECK(status.last_error == "hook exited 3: dnsmasq restart failed");
    CHECK(status.config_hash == config_hash(config));
    CHECK_FALSE(status.last_apply_ts.has_value());

    hook.hook_result.exit_code = 0;
    hook.hook_result.stdout_output.clear();
    hook.now_ms += kBackoffInitialMs + 1;
    run_sync(rig, config);
    CHECK(hook.applies() == 2);
    status = manager.status();
    CHECK(status.state == DnsmasqSyncState::Ok);
    CHECK(status.last_error.empty());
    CHECK(status.last_apply_ts.has_value());
}

TEST_CASE("dnsmasq manager: switching to none removes the config") {
    TempDir dir;
    Rig rig;
    auto& manager = rig.manager;
    auto& hook = rig.fake;
    run_sync(rig, make_config("dnsmasq", "example.com"));
    REQUIRE(hook.calls.size() == 1);

    run_sync(rig, make_config("none", "example.com"));
    REQUIRE(hook.calls.size() == 2);
    CHECK(hook.calls[1] == std::vector<std::string>{"/hook", "remove"});
    CHECK(dir_is_empty(dir.path));
    const auto status = manager.status();
    CHECK(status.mode == ResolverIntegrationMode::NONE);
    CHECK(status.state == DnsmasqSyncState::Disabled);
    CHECK(status.config_hash.empty());

    // Re-enabling installs again: remove restarted dnsmasq without the config.
    run_sync(rig, make_config("dnsmasq", "example.com"));
    CHECK(hook.calls.size() == 3);
    CHECK(hook.calls[2][1] == "apply");
}

TEST_CASE("dnsmasq manager: none removes on first sync only") {
    TempDir dir;
    Rig rig;
    auto& manager = rig.manager;
    auto& hook = rig.fake;
    run_sync(rig, make_config("none", "example.com"));
    REQUIRE(hook.calls.size() == 1);
    CHECK(hook.calls[0] == std::vector<std::string>{"/hook", "remove"});
    CHECK(manager.status().state == DnsmasqSyncState::Disabled);

    run_sync(rig, make_config("none", "example.com"));
    CHECK(hook.calls.size() == 1);
    CHECK(dir_is_empty(dir.path));
}

TEST_CASE("dnsmasq manager: none with empty hook path calls nothing") {
    Rig rig("");
    auto& manager = rig.manager;
    auto& hook = rig.fake;
    run_sync(rig, make_config("none", "example.com"));
    CHECK(hook.calls.empty());
}

TEST_CASE("dnsmasq manager: empty hook path in dnsmasq mode is an error") {
    TempDir dir;
    Rig rig("");
    auto& manager = rig.manager;
    auto& hook = rig.fake;
    run_sync(rig, make_config("dnsmasq", "example.com"));
    CHECK(hook.calls.empty());
    const auto status = manager.status();
    CHECK(status.mode == ResolverIntegrationMode::DNSMASQ);
    CHECK(status.state == DnsmasqSyncState::Error);
    CHECK(status.last_error == "no dnsmasq hook for this platform");
}

TEST_CASE("dnsmasq manager: request_sync coalesces queued requests") {
    TempDir dir;
    Rig rig;
    auto& manager = rig.manager;
    auto& hook = rig.fake;
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

TEST_CASE("dnsmasq manager: startup with dnsmasq already serving the config runs no hook") {
    Rig rig;
    const Config config = make_config("dnsmasq", "example.com");
    rig.fake.serve_stamp(config_hash(config), 500'000, 1'699'999'000);
    run_sync(rig, config);

    CHECK(rig.fake.calls.empty());
    const auto status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Ok);
    CHECK(status.probe_state == DnsmasqProbeState::Ok);
    CHECK(status.config_hash == config_hash(config));
    CHECK(status.loaded_hash == config_hash(config));
    CHECK(status.loaded_boottime_ms == 500'000);
    CHECK(status.loaded_ts == 1'699'999'000);
    CHECK(status.last_check_ts.has_value());
    CHECK_FALSE(status.last_apply_ts.has_value());
    CHECK_FALSE(status.last_external_reload_ts.has_value());
    CHECK(status.rules == 1);
}

TEST_CASE("dnsmasq manager: missing TXT triggers one apply that is confirmed") {
    Rig rig;
    const Config config = make_config("dnsmasq", "example.com");
    run_sync(rig, config);

    CHECK(rig.fake.applies() == 1);
    const auto status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Ok);
    CHECK(status.probe_state == DnsmasqProbeState::Ok);
    CHECK(status.loaded_hash == config_hash(config));
    CHECK(status.last_apply_ts.has_value());
    CHECK(status.last_error.empty());
}

TEST_CASE("dnsmasq manager: dnsmasq that keeps its old stamp was not restarted") {
    Rig rig;
    const Config config = make_config("dnsmasq", "example.com");
    rig.fake.serve_stamp("0123456789abcdef0123456789abcdef", 100, 1'600'000'000);
    rig.fake.on_apply = [](FakeDnsmasq&) {};
    run_sync(rig, config);

    CHECK(rig.fake.applies() == 1);
    const auto status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Error);
    CHECK(status.last_error.find("dnsmasq was not restarted after apply") != std::string::npos);
    CHECK(status.last_error.find("2020-09-13") != std::string::npos);
    CHECK(rig.fake.sleeps > 0);
}

TEST_CASE("dnsmasq manager: apply with TXT still missing points at the drop-in") {
    Rig rig;
    rig.fake.on_apply = [](FakeDnsmasq& f) {
        f.now_ms += 1000;  // restarted, but without our config
        f.serve_missing();
    };
    run_sync(rig, make_config("dnsmasq", "example.com"));

    const auto status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Error);
    CHECK(status.probe_state == DnsmasqProbeState::Missing);
    CHECK(status.last_error ==
          "dnsmasq is running without the keen-pbr config (TXT config-hash.keen.pbr missing); "
          "the dnsmasq hook did not install its drop-in where dnsmasq reads it");
}

TEST_CASE("dnsmasq manager: dnsmasq reloading a different config is reported") {
    Rig rig;
    const std::string other = "ffffffffffffffffffffffffffffffff";
    rig.fake.on_apply = [other](FakeDnsmasq& f) { f.restart_with(other); };
    const Config config = make_config("dnsmasq", "example.com");
    run_sync(rig, config);

    const auto status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Error);
    CHECK(status.last_error == "dnsmasq restarted but loaded a different config (hash " + other +
                                   ", expected " + config_hash(config) + ")");
    CHECK(status.loaded_hash == other);
}

TEST_CASE("dnsmasq manager: failing probe after apply reports the address") {
    Rig rig;
    rig.fake.on_apply = [](FakeDnsmasq& f) { f.serve_failed("timed out"); };
    run_sync(rig, make_config("dnsmasq", "example.com"));

    const auto status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Error);
    CHECK(status.probe_state == DnsmasqProbeState::QueryFailed);
    CHECK(status.last_error ==
          "dnsmasq did not answer the config check on 127.0.0.1:53: timed out");
}

TEST_CASE("dnsmasq manager: unparsable TXT is reported as invalid") {
    Rig rig;
    rig.fake.on_apply = [](FakeDnsmasq& f) {
        f.now_ms += 1000;
        f.served = {};
        f.served.status = DnsTxtProbeStatus::Ok;
        f.served.txt = "hello";
    };
    run_sync(rig, make_config("dnsmasq", "example.com"));

    const auto status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Error);
    CHECK(status.probe_state == DnsmasqProbeState::Invalid);
    CHECK(status.last_error == "invalid config-hash.keen.pbr TXT record: hello");
}

TEST_CASE("dnsmasq manager: failed confirmation backs off repeated applies") {
    Rig rig;
    const Config config = make_config("dnsmasq", "example.com");
    rig.fake.on_apply = [](FakeDnsmasq& f) {
        f.now_ms += 1000;
        f.serve_missing();
    };
    run_sync(rig, config);
    REQUIRE(rig.fake.applies() == 1);

    // Repeated syncs and checks within the backoff do not restart dnsmasq.
    run_sync(rig, config);
    run_check(rig);
    rig.fake.now_ms += kBackoffInitialMs - 10'000;
    run_sync(rig, config);
    run_check(rig);
    CHECK(rig.fake.applies() == 1);
    CHECK(rig.manager.status().state == DnsmasqSyncState::Error);
    CHECK(rig.manager.status().probe_state == DnsmasqProbeState::Missing);

    // Past the backoff exactly one retry runs, and the next backoff doubles.
    rig.fake.now_ms += 20'000;
    run_check(rig);
    CHECK(rig.fake.applies() == 2);
    rig.fake.now_ms += kBackoffInitialMs + 1000;
    run_check(rig);
    CHECK(rig.fake.applies() == 2);
    rig.fake.now_ms += kBackoffInitialMs;
    run_check(rig);
    CHECK(rig.fake.applies() == 3);

    // A changed hash resets the backoff.
    const Config changed = make_config("dnsmasq", "changed.example");
    run_sync(rig, changed);
    CHECK(rig.fake.applies() == 4);

    // So does a successful confirmation.
    rig.fake.on_apply = nullptr;
    rig.fake.now_ms += 100 * 60 * 1000;
    run_sync(rig, changed);
    CHECK(rig.fake.applies() == 5);
    CHECK(rig.manager.status().state == DnsmasqSyncState::Ok);
    rig.fake.serve_missing();
    run_check(rig);
    CHECK(rig.fake.applies() == 6);
    CHECK(rig.manager.status().state == DnsmasqSyncState::Ok);
}

TEST_CASE("dnsmasq manager: check does nothing before a full sync or without dnsmasq mode") {
    Rig rig;
    run_check(rig);
    CHECK(rig.fake.probes == 0);
    CHECK(rig.fake.calls.empty());

    run_sync(rig, make_config("none", "example.com"));
    const int probes = rig.fake.probes;
    run_check(rig);
    CHECK(rig.fake.probes == probes);
}

TEST_CASE("dnsmasq manager: check with an unchanged stamp only records the time") {
    Rig rig;
    const Config config = make_config("dnsmasq", "example.com");
    run_sync(rig, config);
    const int probes = rig.fake.probes;
    run_check(rig);
    CHECK(rig.fake.probes == probes + 1);
    CHECK(rig.fake.applies() == 1);
    const auto status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Ok);
    CHECK_FALSE(status.last_external_reload_ts.has_value());
}

TEST_CASE("dnsmasq manager: check notices an external restart that reloaded our config") {
    Rig rig;
    const Config config = make_config("dnsmasq", "example.com");
    run_sync(rig, config);
    REQUIRE(rig.fake.applies() == 1);

    // The check has no config at all: it cannot stream lists, it only compares
    // the probed stamp with the hash of the last full sync.
    rig.fake.restart_with(config_hash(config));
    const std::int64_t reload_ts = rig.fake.unix_ts;
    run_check(rig);

    CHECK(rig.fake.applies() == 1);
    const auto status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Ok);
    CHECK(status.last_external_reload_ts == reload_ts);
    CHECK(status.loaded_ts == reload_ts);

    // The new stamp is remembered, so the next check is quiet.
    rig.fake.unix_ts += 100;
    run_check(rig);
    CHECK(rig.manager.status().last_external_reload_ts == reload_ts);
}

TEST_CASE("dnsmasq manager: check re-applies after an external restart with another config") {
    Rig rig;
    const Config config = make_config("dnsmasq", "example.com");
    run_sync(rig, config);
    REQUIRE(rig.fake.applies() == 1);

    rig.fake.restart_with("ffffffffffffffffffffffffffffffff");
    rig.fake.on_apply = nullptr;
    rig.fake.target_hash = config_hash(config);
    run_check(rig);

    CHECK(rig.fake.applies() == 2);
    const auto status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Ok);
    CHECK(status.loaded_hash == config_hash(config));
}

TEST_CASE("dnsmasq manager: lifecycle sync re-applies despite the backoff") {
    Rig rig;
    const Config config = make_config("dnsmasq", "example.com");
    rig.fake.on_apply = [](FakeDnsmasq& f) {
        f.now_ms += 1000;
        f.serve_missing();
    };
    run_sync(rig, config);
    REQUIRE(rig.fake.applies() == 1);
    run_sync(rig, config);
    CHECK(rig.fake.applies() == 1);  // automatic sync honors the backoff

    rig.fake.on_apply = nullptr;
    run_lifecycle_sync(rig, config);
    CHECK(rig.fake.applies() == 2);
    CHECK(rig.manager.status().state == DnsmasqSyncState::Ok);
}

TEST_CASE("dnsmasq manager: check tolerates a few unanswered probes") {
    Rig rig;
    const Config config = make_config("dnsmasq", "example.com");
    run_sync(rig, config);
    REQUIRE(rig.fake.applies() == 1);
    const std::string hash = config_hash(config);
    const auto loaded = rig.fake.served;

    // Two lost queries, then an answer: no restart, counter resets.
    rig.fake.serve_failed("timed out");
    run_check(rig);
    run_check(rig);
    rig.fake.served = loaded;
    run_check(rig);
    rig.fake.serve_failed("timed out");
    run_check(rig);
    run_check(rig);
    CHECK(rig.fake.applies() == 1);
    CHECK(rig.manager.status().state == DnsmasqSyncState::Ok);

    // The third unanswered check in a row re-applies.
    run_check(rig);
    CHECK(rig.fake.applies() == 2);
    CHECK(rig.manager.status().state == DnsmasqSyncState::Ok);
}

TEST_CASE("dnsmasq manager: check re-applies when the TXT disappears") {
    Rig rig;
    const Config config = make_config("dnsmasq", "example.com");
    run_sync(rig, config);
    rig.fake.restart_with("x");
    rig.fake.serve_missing();
    run_check(rig);
    CHECK(rig.fake.applies() == 2);
    CHECK(rig.manager.status().state == DnsmasqSyncState::Ok);
}

TEST_CASE("dnsmasq manager: a queued sync supersedes a check and checks coalesce") {
    Rig rig;
    CacheManager cache("/nonexistent/cache");
    std::vector<std::function<void()>> queue;
    const DnsmasqPostFn post = [&queue](std::function<void()> task) {
        queue.push_back(std::move(task));
        return true;
    };
    const Config config = make_config("dnsmasq", "example.com");
    run_sync(rig, config);
    rig.fake.probes = 0;

    rig.manager.request_check(post);
    rig.manager.request_check(post);
    REQUIRE(queue.size() == 1);
    rig.manager.request_sync(config, cache, post);  // joins the active worker
    CHECK(queue.size() == 1);
    queue[0]();
    // Sync ran (one probe) and the superseded check did not add another.
    CHECK(rig.fake.probes == 1);
    CHECK(rig.fake.applies() == 1);
}
