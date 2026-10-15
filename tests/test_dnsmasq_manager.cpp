#include <doctest/doctest.h>

#include "../src/daemon/dnsmasq_manager.hpp"
#include "../src/dns/dnsmasq_gen.hpp"

#include <fstream>

#include <unistd.h>

#include <deque>
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
    // Answers handed out before `served` (one per probe).
    std::deque<DnsTxtProbeResult> probe_script;
    // The hook `alive` is no longer called; kept to assert exactly that.
    int alives = 0;
    int probes = 0;
    // Detection fake.
    bool running = true;
    DnsmasqEndpoint endpoint{"127.0.0.1", 53};
    std::string exe = "/proc/self/exe";
    DnsmasqErrorCode detect_error = DnsmasqErrorCode::None;
    DnsmasqErrorCode instance_problem = DnsmasqErrorCode::None;
    int detects = 0;
    std::vector<DnsmasqEndpoint> probed_endpoints;
    // Capability fake.
    int capability_calls = 0;
    DnsmasqCapabilityResult capability_result = [] {
        DnsmasqCapabilityResult r;
        r.status = DnsmasqCapabilityStatus::Ok;
        return r;
    }();
    // Preflight fake.
    int preflight_calls = 0;
    DnsmasqPreflightResult preflight_result = [] {
        DnsmasqPreflightResult r;
        r.ok = true;
        return r;
    }();
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
    static DnsTxtProbeResult failed_result(const std::string& error) {
        DnsTxtProbeResult result;
        result.status = DnsTxtProbeStatus::QueryFailed;
        result.error = error;
        return result;
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
            if (args.size() > 1 && args[1] == "alive") {
                ++alives;
            }
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
        return [this](const DnsmasqEndpoint& ep) {
            ++probes;
            probed_endpoints.push_back(ep);
            if (!probe_script.empty()) {
                DnsTxtProbeResult next = probe_script.front();
                probe_script.pop_front();
                return next;
            }
            return served;
        };
    }
    DnsmasqDetectFn detect() {
        return [this](const DnsmasqDetectOptions& options) {
            ++detects;
            DnsmasqDetection det;
            det.inspect.binary = exe;
            if (detect_error != DnsmasqErrorCode::None) {
                det.error = detect_error;
                det.message = "fake detection error";
                return det;
            }
            DnsmasqInstance inst;
            inst.selector = DnsmasqSelector{"uci", "test"};
            inst.hook.selector = inst.selector.str();
            if (running) {
                ProcessInfo process;
                process.pid = 100;
                process.exe = exe;
                inst.process = process;
                inst.endpoint = endpoint;
                inst.evidence = EndpointEvidence::Responding;
                if (served.status == DnsTxtProbeStatus::Ok) {
                    const auto stamp = parse_dnsmasq_config_stamp(served.txt);
                    if (stamp && !options.expected_hash.empty() &&
                        stamp->hash == options.expected_hash) {
                        inst.evidence = EndpointEvidence::StampVerified;
                        inst.stamp = stamp;
                    }
                }
            } else {
                inst.problem = DnsmasqErrorCode::NotRunning;
                inst.problem_detail = "dnsmasq is not running";
            }
            if (instance_problem != DnsmasqErrorCode::None) {
                inst.problem = instance_problem;
                inst.problem_detail = "fake instance problem";
            }
            det.instances.push_back(inst);
            det.selected = 0;
            det.selection = DnsmasqSelectionKind::Explicit;
            det.hook_ok = true;
            return det;
        };
    }
    DnsmasqCapabilityFn capability() {
        return [this](const std::string&) {
            ++capability_calls;
            return capability_result;
        };
    }
    DnsmasqPreflightFn preflight() {
        return [this](const DnsmasqInstance&) {
            ++preflight_calls;
            return preflight_result;
        };
    }
    DnsmasqManagerDeps deps() {
        DnsmasqManagerDeps d;
        d.exec = exec();
        d.probe = probe();
        d.clock = clock();
        d.detect = detect();
        d.capability = capability();
        d.preflight = preflight();
        return d;
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
        : manager(hook, fake.deps(), fake.timing()) {}
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

// Runs one blocking sync the way lifecycle operations do (explicit apply).
void run_lifecycle_sync(Rig& rig, const Config& config) {
    rig.fake.target_hash = config_hash(config);
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);
    const DnsServerRegistry registry(config.dns.value_or(DnsConfig{}));
    rig.manager.sync(config, registry, streamer, /*explicit_apply=*/true, "config applied");
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

constexpr std::int64_t kRepairDelayMs = 5 * 60 * 1000;

// A current-version config with no resolver_integration: the module is off.
Config make_config_without_dns_rules() {
    return parse_config(R"({
        "version": "3.4.0",
        "lists": {"l": {"domains": ["example.com"]}},
        "dns": {"servers": [{"tag":"up","address":"1.1.1.1"}]}
    })");
}

// dnsmasq lost the keen-pbr config (e.g. restarted without the drop-in).
void lose_config(Rig& rig) { rig.fake.serve_missing(); }

} // namespace

TEST_CASE("dnsmasq manager: first sync applies once without writing files") {
    TempDir dir;
    Rig rig;
    auto& manager = rig.manager;
    auto& hook = rig.fake;
    run_sync(rig, make_config("dnsmasq", "example.com"));

    REQUIRE(hook.calls.size() == 1);
    CHECK(hook.calls[0] == std::vector<std::string>{"/hook", "apply", "uci:test"});
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

TEST_CASE("dnsmasq manager: failed explicit apply does not use the repair budget") {
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
    CHECK(status.repair_attempt == 0);
    CHECK(status.repair_max_attempts == 3);

    // The next sync with the same hash finds dnsmasq out of sync: repair 1/3.
    hook.hook_result.exit_code = 0;
    hook.hook_result.stdout_output.clear();
    run_sync(rig, config);
    CHECK(hook.applies() == 2);
    status = manager.status();
    CHECK(status.state == DnsmasqSyncState::Ok);
    CHECK(status.last_error.empty());
    CHECK(status.last_apply_ts.has_value());
    CHECK(status.repair_attempt == 1);
    CHECK(status.repair_reason ==
          "the keen-pbr config is missing from dnsmasq (TXT config-hash.keen.pbr not found)");
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
    CHECK(hook.calls[1] == std::vector<std::string>{"/hook", "remove", "uci:test"});
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

TEST_CASE("dnsmasq manager: none never touches dnsmasq on the first sync") {
    TempDir dir;
    Rig rig;
    auto& manager = rig.manager;
    auto& hook = rig.fake;
    run_sync(rig, make_config("none", "example.com"));
    CHECK(hook.calls.empty());
    CHECK(hook.probes == 0);
    CHECK(manager.status().state == DnsmasqSyncState::Disabled);

    run_sync(rig, make_config("none", "example.com"));
    CHECK(hook.calls.empty());
    CHECK(dir_is_empty(dir.path));
}

TEST_CASE("dnsmasq manager: module off (none or no dns.rules) makes no hook calls or probes") {
    for (const Config& config : {make_config("none", "example.com"),
                                 make_config_without_dns_rules()}) {
        Rig rig;
        CHECK_FALSE(dnsmasq_integration_enabled(config));
        run_sync(rig, config);
        run_lifecycle_sync(rig, config);
        for (int i = 0; i < 5; ++i) {
            rig.fake.now_ms += 10 * 60 * 1000;
            run_check(rig);
        }
        run_sync(rig, config);
        CHECK(rig.fake.calls.empty());
        CHECK(rig.fake.probes == 0);
        CHECK(rig.fake.alives == 0);
        const auto status = rig.manager.status();
        CHECK(status.state == DnsmasqSyncState::Disabled);
        CHECK(status.mode == ResolverIntegrationMode::NONE);
    }
}

TEST_CASE("dnsmasq manager: switching the module off removes once and stops probing") {
    Rig rig;
    run_sync(rig, make_config("dnsmasq", "example.com"));
    REQUIRE(rig.fake.applies() == 1);

    run_sync(rig, make_config_without_dns_rules());
    int removes = 0;
    for (const auto& call : rig.fake.calls) {
        if (call.size() > 1 && call[1] == "remove") ++removes;
    }
    CHECK(removes == 1);
    run_sync(rig, make_config("none", "example.com"));

    const int probes = rig.fake.probes;
    const std::size_t calls = rig.fake.calls.size();
    run_check(rig);
    run_check(rig);
    CHECK(rig.fake.probes == probes);
    CHECK(rig.fake.calls.size() == calls);
    CHECK(rig.manager.status().state == DnsmasqSyncState::Disabled);
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

TEST_CASE("dnsmasq manager: check does nothing before a full sync") {
    Rig rig;
    run_check(rig);
    CHECK(rig.fake.probes == 0);
    CHECK(rig.fake.calls.empty());
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

TEST_CASE("dnsmasq manager: a lost probe answered on the retry changes nothing") {
    Rig rig;
    run_sync(rig, make_config("dnsmasq", "example.com"));
    REQUIRE(rig.fake.applies() == 1);
    const auto loaded = rig.fake.served;

    rig.fake.probe_script.push_back(FakeDnsmasq::failed_result("timed out"));
    rig.fake.probe_script.push_back(loaded);
    run_check(rig);
    CHECK(rig.fake.alives == 0);
    CHECK(rig.fake.applies() == 1);
    CHECK(rig.manager.status().state == DnsmasqSyncState::Ok);
}

TEST_CASE("dnsmasq manager: dnsmasq that is running but silent is not restarted") {
    Rig rig;
    run_sync(rig, make_config("dnsmasq", "example.com"));
    REQUIRE(rig.fake.applies() == 1);

    rig.fake.serve_failed("timed out");
    for (int i = 0; i < 4; ++i) {
        rig.fake.now_ms += 30'000;
        run_check(rig);
    }
    CHECK(rig.fake.applies() == 1);
    CHECK(rig.fake.alives == 0);
    const auto status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Error);
    CHECK(status.error_code == DnsmasqErrorCode::ProbeFailed);
    CHECK(status.probe_state == DnsmasqProbeState::QueryFailed);
    CHECK(status.repair_attempt == 0);
    CHECK(status.dnsmasq_alive == DnsmasqLiveness::Alive);
    CHECK(status.last_error ==
          "dnsmasq uci:test is running but does not answer the config check on 127.0.0.1:53: "
          "timed out; not restarting it");

    // It answers again with our config: back to ok without any restart.
    rig.fake.restart_with(config_hash(make_config("dnsmasq", "example.com")));
    run_check(rig);
    CHECK(rig.fake.applies() == 1);
    CHECK(rig.manager.status().state == DnsmasqSyncState::Ok);
    CHECK(rig.manager.status().error_code == DnsmasqErrorCode::None);
}

TEST_CASE("dnsmasq manager: dnsmasq that disappeared is reported, not restarted") {
    Rig rig;
    run_sync(rig, make_config("dnsmasq", "example.com"));
    REQUIRE(rig.fake.applies() == 1);

    rig.fake.serve_failed("connection refused");
    rig.fake.running = false;
    run_check(rig);
    CHECK(rig.fake.applies() == 1);
    CHECK(rig.fake.alives == 0);
    const auto status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Error);
    CHECK(status.error_code == DnsmasqErrorCode::NotRunning);
    CHECK(status.repair_attempt == 0);
    CHECK(status.dnsmasq_alive == DnsmasqLiveness::Dead);
}

TEST_CASE("dnsmasq manager: flapping dnsmasq is repaired with growing waits, then paused") {
    Rig rig;
    const Config config = make_config("dnsmasq", "example.com");
    run_sync(rig, config);
    REQUIRE(rig.fake.applies() == 1);

    // Attempt 1 runs immediately and is confirmed (not a budget reset).
    lose_config(rig);
    std::int64_t t1 = rig.fake.now_ms;
    run_check(rig);
    CHECK(rig.fake.applies() == 2);
    auto status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Ok);
    CHECK(status.repair_attempt == 1);
    CHECK_FALSE(status.next_repair_ts.has_value());

    // Lost again: reconciling until 5 min after attempt 1 started.
    lose_config(rig);
    rig.fake.now_ms += 30'000;
    run_check(rig);
    status = rig.manager.status();
    CHECK(rig.fake.applies() == 2);
    CHECK(status.state == DnsmasqSyncState::Reconciling);
    CHECK(status.repair_attempt == 1);
    CHECK(status.next_repair_ts.has_value());
    CHECK(status.repair_reason ==
          "the keen-pbr config is missing from dnsmasq (TXT config-hash.keen.pbr not found)");
    rig.fake.now_ms = t1 + kRepairDelayMs - 1;
    run_check(rig);
    CHECK(rig.fake.applies() == 2);
    CHECK(rig.manager.status().state == DnsmasqSyncState::Reconciling);

    // Attempt 2.
    rig.fake.now_ms = t1 + kRepairDelayMs;
    const std::int64_t t2 = rig.fake.now_ms;
    run_check(rig);
    CHECK(rig.fake.applies() == 3);
    CHECK(rig.manager.status().repair_attempt == 2);
    CHECK(rig.manager.status().state == DnsmasqSyncState::Ok);

    // Attempt 3 waits twice as long.
    lose_config(rig);
    rig.fake.now_ms = t2 + 2 * kRepairDelayMs - 1;
    run_check(rig);
    CHECK(rig.fake.applies() == 3);
    CHECK(rig.manager.status().state == DnsmasqSyncState::Reconciling);
    rig.fake.now_ms = t2 + 2 * kRepairDelayMs;
    run_check(rig);
    CHECK(rig.fake.applies() == 4);
    CHECK(rig.manager.status().repair_attempt == 3);
    CHECK(rig.manager.status().state == DnsmasqSyncState::Ok);

    // The next loss exhausts the budget: error + paused, no more applies.
    lose_config(rig);
    for (int i = 0; i < 400; ++i) {
        rig.fake.now_ms += 30'000;  // ~3.3 hours of periodic checks
        run_check(rig);
    }
    CHECK(rig.fake.applies() == 4);
    status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Error);
    CHECK(status.repair_paused);
    CHECK(status.repair_attempt == 3);
    CHECK(status.probe_state == DnsmasqProbeState::Missing);
    CHECK(status.last_error.find("after 3 repair attempts; automatic repair is paused until "
                                 "the next config apply or runtime restart (last problem: ") !=
          std::string::npos);

    // dnsmasq recovers by itself: ok again, but still paused until a reset.
    rig.fake.restart_with(config_hash(config));
    run_check(rig);
    status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Ok);
    CHECK(status.repair_paused);
    CHECK(status.repair_attempt == 3);
    CHECK(rig.fake.applies() == 4);
}

TEST_CASE("dnsmasq manager: an explicit lifecycle sync re-arms paused repairs") {
    Rig rig;
    const Config config = make_config("dnsmasq", "example.com");
    run_sync(rig, config);
    // Exhaust the budget: three repairs, then pause.
    for (int i = 0; i < 3; ++i) {
        lose_config(rig);
        rig.fake.now_ms += 3 * 60 * 60 * 1000;
        run_check(rig);
    }
    lose_config(rig);
    run_check(rig);
    REQUIRE(rig.fake.applies() == 4);
    REQUIRE(rig.manager.status().repair_paused);

    run_lifecycle_sync(rig, config);
    CHECK(rig.fake.applies() == 5);
    auto status = rig.manager.status();
    CHECK_FALSE(status.repair_paused);
    CHECK(status.repair_attempt == 0);
    CHECK(status.state == DnsmasqSyncState::Ok);

    // A fresh budget: the next loss repairs immediately as attempt 1.
    lose_config(rig);
    run_check(rig);
    CHECK(rig.fake.applies() == 6);
    CHECK(rig.manager.status().repair_attempt == 1);
}

TEST_CASE("dnsmasq manager: a changed config re-arms paused repairs") {
    Rig rig;
    run_sync(rig, make_config("dnsmasq", "example.com"));
    for (int i = 0; i < 3; ++i) {
        lose_config(rig);
        rig.fake.now_ms += 3 * 60 * 60 * 1000;
        run_check(rig);
    }
    lose_config(rig);
    run_check(rig);
    REQUIRE(rig.fake.applies() == 4);
    REQUIRE(rig.manager.status().repair_paused);

    run_sync(rig, make_config("dnsmasq", "changed.example"));
    CHECK(rig.fake.applies() == 5);
    const auto status = rig.manager.status();
    CHECK_FALSE(status.repair_paused);
    CHECK(status.repair_attempt == 0);
    CHECK(status.state == DnsmasqSyncState::Ok);
}

TEST_CASE("dnsmasq manager: staying in sync for an hour refills the repair budget") {
    Rig rig;
    run_sync(rig, make_config("dnsmasq", "example.com"));
    lose_config(rig);
    run_check(rig);
    REQUIRE(rig.fake.applies() == 2);
    REQUIRE(rig.manager.status().repair_attempt == 1);

    for (int i = 0; i < 100; ++i) {  // 50 minutes
        rig.fake.now_ms += 30'000;
        run_check(rig);
    }
    CHECK(rig.manager.status().repair_attempt == 1);
    for (int i = 0; i < 40; ++i) {  // past one hour
        rig.fake.now_ms += 30'000;
        run_check(rig);
    }
    CHECK(rig.manager.status().repair_attempt == 0);
    CHECK_FALSE(rig.manager.status().repair_paused);

    lose_config(rig);
    run_check(rig);
    CHECK(rig.fake.applies() == 3);
    CHECK(rig.manager.status().repair_attempt == 1);
}

TEST_CASE("dnsmasq manager: unchanged-hash sync that finds dnsmasq out of sync is a repair") {
    Rig rig;
    const Config config = make_config("dnsmasq", "example.com");
    run_sync(rig, config);
    REQUIRE(rig.fake.applies() == 1);

    lose_config(rig);
    run_sync(rig, config);  // e.g. lists autoupdate with the same resulting hash
    CHECK(rig.fake.applies() == 2);
    CHECK(rig.manager.status().repair_attempt == 1);
}

TEST_CASE("dnsmasq manager: unconfirmed explicit apply leaves the budget untouched") {
    Rig rig;
    const Config config = make_config("dnsmasq", "example.com");
    rig.fake.on_apply = [](FakeDnsmasq& f) {
        f.now_ms += 1000;
        f.serve_missing();
    };
    run_sync(rig, config);
    REQUIRE(rig.fake.applies() == 1);
    auto status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Error);
    CHECK(status.repair_attempt == 0);

    // The periodic check that still finds the problem runs repair attempt 1.
    rig.fake.on_apply = nullptr;
    rig.fake.target_hash = config_hash(config);
    run_check(rig);
    CHECK(rig.fake.applies() == 2);
    status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Ok);
    CHECK(status.repair_attempt == 1);
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

TEST_CASE("dnsmasq manager: post failure re-arms on next check request") {
    Rig rig;
    CacheManager cache("/nonexistent/cache");
    std::vector<std::function<void()>> queue;
    int post_fail_count = 1;
    const DnsmasqPostFn post = [&queue, &post_fail_count](std::function<void()> task) {
        if (post_fail_count > 0) {
            --post_fail_count;
            return false;  // Simulate executor unavailable
        }
        queue.push_back(std::move(task));
        return true;
    };
    const Config config = make_config("dnsmasq", "example.com");
    run_sync(rig, config);
    rig.fake.probes = 0;

    // Request a sync but the post fails
    rig.manager.request_sync(config, cache, post);
    // Queue should be empty because post failed
    CHECK(queue.size() == 0);

    // Now request a check; this should re-arm the worker and retry the pending sync
    rig.manager.request_check(post);
    // Queue should now have the retried sync
    REQUIRE(queue.size() == 1);
    // Run the sync
    queue[0]();
    // Sync should have run (it probes dnsmasq)
    CHECK(rig.fake.probes >= 1);
}

TEST_CASE("dnsmasq manager: failed preflight refuses the apply") {
    Rig rig;
    rig.fake.preflight_result.ok = false;
    rig.fake.preflight_result.detail = "bad option at line 3";
    run_sync(rig, make_config("dnsmasq", "example.com"));

    CHECK(rig.fake.calls.empty());
    CHECK(rig.fake.preflight_calls == 1);
    const auto status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Error);
    CHECK(status.error_code == DnsmasqErrorCode::PreflightFailed);
    CHECK(status.preflight == "failed");
    CHECK(status.last_error.find("bad option at line 3") != std::string::npos);
}

TEST_CASE("dnsmasq manager: unsupported capability refuses the apply before preflight") {
    Rig rig;
    rig.fake.capability_result.status = DnsmasqCapabilityStatus::Unsupported;
    rig.fake.capability_result.code = DnsmasqErrorCode::ConfScriptUnsupported;
    rig.fake.capability_result.detail = "no conf-script";
    run_sync(rig, make_config("dnsmasq", "example.com"));

    CHECK(rig.fake.calls.empty());
    CHECK(rig.fake.preflight_calls == 0);
    const auto status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Error);
    CHECK(status.error_code == DnsmasqErrorCode::ConfScriptUnsupported);
    CHECK(status.capability == DnsmasqCapabilityStatus::Unsupported);
}

TEST_CASE("dnsmasq manager: capability is probed once per binary identity") {
    TempDir dir;
    const auto exe = dir.path / "dnsmasq";
    {
        std::ofstream out(exe);
        out << "binary one";
    }
    Rig rig;
    rig.fake.exe = exe.string();
    run_sync(rig, make_config("dnsmasq", "example.com"));
    run_sync(rig, make_config("dnsmasq", "changed.example"));
    CHECK(rig.fake.applies() == 2);
    CHECK(rig.fake.capability_calls == 1);
    CHECK(rig.manager.status().capability == DnsmasqCapabilityStatus::Ok);

    {
        std::ofstream out(exe);
        out << "a different, longer binary";
    }
    run_sync(rig, make_config("dnsmasq", "third.example"));
    CHECK(rig.fake.applies() == 3);
    CHECK(rig.fake.capability_calls == 2);
}

TEST_CASE("dnsmasq manager: ambiguous detection makes no hook calls") {
    Rig rig;
    rig.fake.detect_error = DnsmasqErrorCode::InstanceAmbiguous;
    run_sync(rig, make_config("dnsmasq", "example.com"));
    run_lifecycle_sync(rig, make_config("dnsmasq", "example.com"));
    run_check(rig);

    CHECK(rig.fake.calls.empty());
    const auto status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Error);
    CHECK(status.error_code == DnsmasqErrorCode::InstanceAmbiguous);
    CHECK_FALSE(status.endpoint.has_value());
}

TEST_CASE("dnsmasq manager: a blocked apply runs as an explicit apply once dnsmasq runs") {
    Rig rig;
    rig.fake.running = false;
    run_sync(rig, make_config("dnsmasq", "example.com"));
    CHECK(rig.fake.calls.empty());
    CHECK(rig.manager.status().error_code == DnsmasqErrorCode::NotRunning);

    rig.fake.running = true;
    run_check(rig);
    CHECK(rig.fake.applies() == 1);
    CHECK(rig.fake.preflight_calls == 1);
    const auto status = rig.manager.status();
    CHECK(status.state == DnsmasqSyncState::Ok);
    CHECK(status.error_code == DnsmasqErrorCode::None);
    CHECK(status.repair_attempt == 0);
}

TEST_CASE("dnsmasq manager: a repair reuses the cached preflight") {
    Rig rig;
    run_sync(rig, make_config("dnsmasq", "example.com"));
    REQUIRE(rig.fake.applies() == 1);
    REQUIRE(rig.fake.preflight_calls == 1);

    lose_config(rig);
    run_check(rig);
    CHECK(rig.fake.applies() == 2);
    CHECK(rig.fake.preflight_calls == 1);
    CHECK(rig.manager.status().repair_attempt == 1);
}

TEST_CASE("dnsmasq manager: the probe goes to the detected endpoint") {
    Rig rig;
    rig.fake.endpoint = DnsmasqEndpoint{"127.0.0.1", 5353};
    run_sync(rig, make_config("dnsmasq", "example.com"));
    REQUIRE_FALSE(rig.fake.probed_endpoints.empty());
    for (const auto& ep : rig.fake.probed_endpoints) {
        CHECK(ep == DnsmasqEndpoint{"127.0.0.1", 5353});
    }
    const auto status = rig.manager.status();
    REQUIRE(status.endpoint.has_value());
    CHECK(status.endpoint->str() == "127.0.0.1:5353");
    CHECK(status.selector == "uci:test");
    CHECK(status.pid == 100);
}

TEST_CASE("dnsmasq manager: routing_resolver follows the integration and the status") {
    Rig rig;
    const Config off = make_config("none", "example.com");
    const Config on = make_config("dnsmasq", "example.com");
    CHECK(rig.manager.routing_resolver(off).source == RoutingDnsSource::System);

    // Enabled but nothing known yet.
    auto resolver = rig.manager.routing_resolver(on);
    CHECK(resolver.source == RoutingDnsSource::Dnsmasq);
    CHECK(resolver.unavailable.has_value());

    run_sync(rig, on);
    resolver = rig.manager.routing_resolver(on);
    CHECK(resolver.source == RoutingDnsSource::Dnsmasq);
    CHECK(resolver.address == "127.0.0.1");
    CHECK(resolver.port == 53);
    CHECK(resolver.instance == "uci:test");
    CHECK_FALSE(resolver.unavailable.has_value());
    CHECK_FALSE(resolver.warning.has_value());
    CHECK(rig.manager.routing_resolver(off).source == RoutingDnsSource::System);

    rig.fake.detect_error = DnsmasqErrorCode::NoDnsInstance;
    run_lifecycle_sync(rig, on);
    resolver = rig.manager.routing_resolver(on);
    REQUIRE(resolver.unavailable.has_value());
    CHECK(resolver.unavailable->find("DNSMASQ_NO_DNS_INSTANCE") != std::string::npos);
}

TEST_CASE("dnsmasq manager: switching off in the daemon removes the selected instance") {
    Rig rig;
    run_sync(rig, make_config("dnsmasq", "example.com"));
    run_sync(rig, make_config("none", "example.com"));
    REQUIRE(rig.fake.calls.size() == 2);
    CHECK(rig.fake.calls[1] == std::vector<std::string>{"/hook", "remove", "uci:test"});
}
