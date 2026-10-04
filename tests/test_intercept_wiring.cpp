#include <doctest/doctest.h>

#include "../src/api/handler_health_service.hpp"
#include "../src/cache/cache_manager.hpp"
#include "../src/config/config.hpp"
#include "../src/firewall/firewall_runtime.hpp"
#include "../src/intercept/intercept_capabilities.hpp"
#include "../src/intercept/intercept_report.hpp"
#include "../src/intercept/intercept_settings.hpp"
#include "../src/intercept/intercept_snapshot_builder.hpp"
#include "../src/lists/list_streamer.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unistd.h>
#include <string>
#include <vector>

using namespace keen_pbr3;

namespace {

struct FakeProc {
    std::filesystem::path dir;
    InterceptProbeEnv env;
    std::vector<std::string> modprobed;  // table modules only (see record())
    std::vector<std::string> nfnl_modprobed;  // nfnetlink_*/nft_* preloads

    void record(const std::string& module) {
        if (module.rfind("nfnetlink_", 0) == 0 || module.rfind("nft_", 0) == 0) {
            nfnl_modprobed.push_back(module);
        } else {
            modprobed.push_back(module);
        }
    }

    FakeProc() {
        dir = std::filesystem::temp_directory_path() /
              ("keen-pbr-intercept-probe-" + std::to_string(::getpid()) + "-" +
               std::to_string(counter()++));
        std::filesystem::create_directories(dir);
        env.ip_targets = (dir / "ip_targets").string();
        env.ip6_targets = (dir / "ip6_targets").string();
        env.ip_matches = (dir / "ip_matches").string();
        env.ip6_matches = (dir / "ip6_matches").string();
        env.modprobe = [this](const std::string& module) { record(module); };
        env.runtime_probes = false;  // these tests cover the /proc logic only
    }
    ~FakeProc() { std::filesystem::remove_all(dir); }

    static int& counter() {
        static int value = 0;
        return value;
    }

    static void write(const std::string& path, const std::vector<std::string>& names) {
        std::ofstream out(path);
        for (const auto& name : names) out << name << "\n";
    }
    void fill_all() {
        for (const auto* targets : {&env.ip_targets, &env.ip6_targets}) {
            write(*targets, {"ERROR", "NFQUEUE", "NFLOG", "MARK"});
        }
        for (const auto* matches : {&env.ip_matches, &env.ip6_matches}) {
            write(*matches, {"conntrack", "connbytes", "mark"});
        }
    }
};

InterceptCapabilities all_caps() {
    InterceptCapabilities caps;
    caps.nfqueue = caps.nflog = caps.connbytes = true;
    return caps;
}

Config config_from(const std::string& json) { return parse_config(json); }

} // namespace

TEST_CASE("intercept probe: iptables with everything loaded does not modprobe") {
    FakeProc proc;
    proc.fill_all();
    const auto caps = probe_intercept_capabilities(FirewallBackend::iptables, true, proc.env);
    CHECK(caps.nfqueue);
    CHECK(caps.nflog);
    CHECK(caps.connbytes);
    CHECK(caps.reason.empty());
    CHECK(proc.modprobed.empty());
}

TEST_CASE("intercept probe: nfnetlink queue/log modules are preloaded per backend") {
    FakeProc proc;
    proc.fill_all();
    (void)probe_intercept_capabilities(FirewallBackend::iptables, true, proc.env);
    CHECK(proc.nfnl_modprobed ==
          std::vector<std::string>{"nfnetlink_queue", "nfnetlink_log"});
    proc.nfnl_modprobed.clear();
    (void)probe_intercept_capabilities(FirewallBackend::nftables, true, proc.env);
    CHECK(proc.nfnl_modprobed ==
          std::vector<std::string>{"nfnetlink_queue", "nfnetlink_log", "nft_queue",
                                   "nft_log", "nft_ct"});
}

TEST_CASE("intercept bind EINVAL names the missing nfnetlink kernel module") {
    const std::string absent = "/nonexistent/nfnetlink_queue";
    const auto queue = nfnl::nfnl_module_missing_hint(true, EINVAL, absent);
    CHECK(queue.find("nfnetlink_queue") != std::string::npos);
    CHECK(queue.find("kmod-nfnetlink-queue") != std::string::npos);
    CHECK(nfnl::nfnl_module_missing_hint(false, EINVAL, absent).find("kmod-nfnetlink-log") !=
          std::string::npos);
    CHECK(nfnl::nfnl_module_missing_hint(true, EPERM, absent).empty());
    CHECK(nfnl::nfnl_module_missing_hint(true, EINVAL, "/proc/self/status").empty());
}

TEST_CASE("intercept probe: missing modules are modprobed once and re-read") {
    FakeProc proc;
    proc.fill_all();
    FakeProc::write(proc.env.ip_targets, {"NFQUEUE"});
    FakeProc::write(proc.env.ip6_targets, {"NFQUEUE"});
    proc.env.modprobe = [&proc](const std::string& module) {
        proc.record(module);
        if (module == "xt_NFLOG") {
            FakeProc::write(proc.env.ip_targets, {"NFQUEUE", "NFLOG"});
            FakeProc::write(proc.env.ip6_targets, {"NFQUEUE", "NFLOG"});
        }
    };
    const auto caps = probe_intercept_capabilities(FirewallBackend::iptables, true, proc.env);
    CHECK(caps.nflog);
    CHECK(caps.nfqueue);
    CHECK(proc.modprobed == std::vector<std::string>{"xt_NFLOG"});
}

TEST_CASE("intercept probe: modules that stay missing are reported") {
    FakeProc proc;
    proc.fill_all();
    FakeProc::write(proc.env.ip_matches, {"conntrack"});
    FakeProc::write(proc.env.ip6_matches, {"conntrack"});
    const auto caps = probe_intercept_capabilities(FirewallBackend::iptables, true, proc.env);
    CHECK(caps.nfqueue);
    CHECK(caps.nflog);
    CHECK_FALSE(caps.connbytes);
    CHECK(caps.reason.find("connbytes") != std::string::npos);
    CHECK(proc.modprobed == std::vector<std::string>{"xt_connbytes"});
}

TEST_CASE("intercept probe: NFQUEUE needs the conntrack match") {
    FakeProc proc;
    proc.fill_all();
    FakeProc::write(proc.env.ip_matches, {"connbytes"});
    FakeProc::write(proc.env.ip6_matches, {"connbytes"});
    const auto caps = probe_intercept_capabilities(FirewallBackend::iptables, true, proc.env);
    CHECK_FALSE(caps.nfqueue);
    CHECK(caps.nflog);
}

TEST_CASE("intercept probe: IPv6 tables are only required when IPv6 is enabled") {
    FakeProc proc;
    proc.fill_all();
    FakeProc::write(proc.env.ip6_targets, {"MARK"});
    const auto v6 = probe_intercept_capabilities(FirewallBackend::iptables, true, proc.env);
    CHECK_FALSE(v6.nfqueue);
    CHECK_FALSE(v6.nflog);
    proc.modprobed.clear();
    const auto v4 = probe_intercept_capabilities(FirewallBackend::iptables, false, proc.env);
    CHECK(v4.nfqueue);
    CHECK(v4.nflog);
    CHECK(proc.modprobed.empty());
}

TEST_CASE("intercept probe: missing proc files mean nothing is available") {
    FakeProc proc;
    const auto caps = probe_intercept_capabilities(FirewallBackend::iptables, true, proc.env);
    CHECK_FALSE(caps.nfqueue);
    CHECK_FALSE(caps.nflog);
    CHECK_FALSE(caps.connbytes);
    CHECK_FALSE(caps.reason.empty());
}

TEST_CASE("intercept probe: nftables is assumed capable without reading proc") {
    FakeProc proc;
    const auto caps = probe_intercept_capabilities(FirewallBackend::nftables, true, proc.env);
    CHECK(caps.nfqueue);
    CHECK(caps.nflog);
    CHECK(caps.connbytes);
    CHECK(proc.modprobed.empty());
}

TEST_CASE("conntrack accounting sysctl is written, failure is reported") {
    FakeProc proc;
    const auto path = (proc.dir / "nf_conntrack_acct").string();
    CHECK(enable_conntrack_accounting(path));
    std::ifstream in(path);
    std::string value;
    in >> value;
    CHECK(value == "1");
    CHECK_FALSE(enable_conntrack_accounting((proc.dir / "missing" / "acct").string()));
}

TEST_CASE("effective intercept: defaults with full capabilities enable both parts") {
    const auto eff = resolve_effective_intercept(config_from("{}"), FirewallBackend::iptables,
                                                 all_caps());
    CHECK(eff.config_enabled);
    CHECK(eff.dns_hold);
    CHECK(eff.l7);
    CHECK(eff.reasons.empty());
    CHECK(eff.queue_num == 9053);
    CHECK(eff.nflog_group == 9054);
    CHECK(eff.hold_timeout_ms == 30);
    CHECK(eff.min_ttl_s == 300);
    CHECK(eff.max_ttl_s == 86400);
    CHECK(eff.marker_domain == "check.keen.pbr");
    CHECK(eff.marker_ipv4 == std::array<uint8_t, 4>{127, 0, 0, 88});
    const auto settings = eff.firewall_settings();
    REQUIRE(settings.has_value());
    CHECK(settings->dns_hold);
    CHECK(settings->l7_sniff);
    CHECK(settings->queue_num == 9053);
    CHECK(settings->nflog_group == 9054);
}

TEST_CASE("effective intercept: capabilities restrict the active parts") {
    InterceptCapabilities no_connbytes = all_caps();
    no_connbytes.connbytes = false;
    no_connbytes.reason = "kernel lacks: connbytes match";
    auto eff = resolve_effective_intercept(config_from("{}"), FirewallBackend::iptables,
                                           no_connbytes);
    CHECK(eff.dns_hold);
    CHECK_FALSE(eff.l7);
    REQUIRE(eff.reasons.size() == 1);
    CHECK(eff.reasons[0].find("connbytes") != std::string::npos);
    CHECK(eff.firewall_settings()->dns_hold);
    CHECK_FALSE(eff.firewall_settings()->l7_sniff);

    InterceptCapabilities no_queue = all_caps();
    no_queue.nfqueue = false;
    eff = resolve_effective_intercept(config_from("{}"), FirewallBackend::iptables, no_queue);
    CHECK_FALSE(eff.dns_hold);
    CHECK(eff.l7);
    CHECK(eff.reasons.size() == 1);

    eff = resolve_effective_intercept(config_from("{}"), FirewallBackend::iptables,
                                      InterceptCapabilities{});
    CHECK_FALSE(eff.active());
    CHECK_FALSE(eff.firewall_settings().has_value());
    CHECK(eff.reasons.size() == 2);
}

TEST_CASE("effective intercept: config switches and overrides are honoured") {
    auto eff = resolve_effective_intercept(config_from(R"({"intercept":{"enabled":false}})"),
                                           FirewallBackend::nftables, all_caps());
    CHECK_FALSE(eff.config_enabled);
    CHECK_FALSE(eff.active());
    CHECK(eff.reasons.empty());

    eff = resolve_effective_intercept(
        config_from(R"({"intercept":{"min_ttl_s":60,"max_ttl_s":120,
          "dns":{"enabled":false,"queue_num":100,"hold_timeout_ms":50,
                 "marker":{"domain":"m.example","answer_ipv4":"10.1.2.3"}},
          "l7":{"nflog_group":200,"tls":true,"http":false,"quic":false}}})"),
        FirewallBackend::nftables, all_caps());
    CHECK_FALSE(eff.dns_hold);
    CHECK(eff.l7);
    CHECK(eff.min_ttl_s == 60);
    CHECK(eff.max_ttl_s == 120);
    CHECK(eff.marker_domain == "m.example");
    CHECK(eff.marker_ipv4 == std::array<uint8_t, 4>{10, 1, 2, 3});
    const auto settings = eff.firewall_settings();
    REQUIRE(settings.has_value());
    CHECK_FALSE(settings->dns_hold);
    CHECK(settings->nflog_group == 200);
    CHECK(settings->tls);
    CHECK_FALSE(settings->http);
    CHECK_FALSE(settings->quic);
}

TEST_CASE("effective intercept settings drive the firewall plan") {
    const Config config = config_from(R"({
      "outbounds":[{"type":"table","tag":"wan","table":100}],
      "lists":{"a":{"domains":["a.example"]}},
      "route":{"rules":[{"list":["a"],"outbound":"wan"}]}})");
    const std::map<std::string, ListSetUsage> usage{{"a", ListSetUsage{false, true, 0}}};
    const OutboundMarkMap marks{{"wan", 0x10000U}};
    const auto count_module = [](const FirewallPlan& plan, const std::string& module) {
        return std::count_if(plan.rules.begin(), plan.rules.end(),
                             [&](const FirewallRuleInstance& rule) {
                                 return rule.key.module_id == module;
                             });
    };
    const auto eff = resolve_effective_intercept(config, FirewallBackend::iptables, all_caps());
    FirewallPlanBuildInputs inputs{config, marks, usage, {}, {}};
    inputs.intercept = eff.firewall_settings();
    const auto with = build_firewall_plan(inputs);
    CHECK(count_module(with, "dns.intercept_hold") > 0);
    CHECK(count_module(with, "l7.sniff") > 0);

    inputs.intercept = resolve_effective_intercept(config_from(R"({"intercept":{"enabled":false}})"),
                                                   FirewallBackend::iptables, all_caps())
                           .firewall_settings();
    const auto without = build_firewall_plan(inputs);
    CHECK(count_module(without, "dns.intercept_hold") == 0);
    CHECK(count_module(without, "l7.sniff") == 0);
}

TEST_CASE("snapshot builder: lists become targets with ttl floors") {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);
    const Config config = config_from(R"({
      "lists":{
        "ttl":{"domains":["Example.COM","*.cdn.example.org"],"ttl_ms":7200000},
        "plain":{"domains":["plain.example"]},
        "short":{"domains":["short.example"],"ttl_ms":500},
        "ips":{"ip_cidrs":["10.0.0.0/8"]},
        "off":{"domains":["off.example"]},
        "unused":{"domains":["unused.example"]}},
      "route":{"rules":[
        {"list":["ttl","plain","ips"],"outbound":"wan"},
        {"list":["short"],"outbound":"wan"},
        {"enabled":false,"list":["off"],"outbound":"wan"}]}})");
    std::vector<FirewallSetDeclaration> sets;
    for (const std::string name : {"ttl", "plain", "short", "unused", "off"}) {
        sets.push_back({Firewall::dynamic_set_name(name, AF_INET), FirewallFamily::ipv4, 0});
        sets.push_back({Firewall::dynamic_set_name(name, AF_INET6), FirewallFamily::ipv6, 0});
    }
    sets.push_back({Firewall::static_set_name("ips", AF_INET), FirewallFamily::ipv4, 0});

    InterceptEffective eff;
    eff.min_ttl_s = 600;
    eff.max_ttl_s = 1234;
    eff.marker_domain = "m.example";
    eff.tls = false;
    const auto snapshot = build_intercept_snapshot(config, sets, true, eff, streamer);
    REQUIRE(snapshot);
    REQUIRE(snapshot->index);
    CHECK(snapshot->max_ttl_s == 1234);
    CHECK(snapshot->marker_domain == "m.example");
    CHECK_FALSE(snapshot->tls);

    // Only lists of enabled rules with dynamic sets: ttl, plain, short.
    const auto& names = snapshot->index->list_names();
    REQUIRE(names.size() == 3);
    const auto id_of = [&](const std::string& name) {
        return static_cast<std::size_t>(std::find(names.begin(), names.end(), name) - names.begin());
    };
    REQUIRE(snapshot->targets.size() == 3);
    const auto& ttl = snapshot->targets[id_of("ttl")];
    CHECK(ttl.set_v4 == "kpbr4d_ttl");
    CHECK(ttl.set_v6 == "kpbr6d_ttl");
    CHECK(ttl.min_ttl_s == 7200);
    CHECK(snapshot->targets[id_of("plain")].min_ttl_s == 600);
    CHECK(snapshot->targets[id_of("short")].min_ttl_s == 600);  // < 1s: no per-list ttl

    std::vector<DomainIndex::ListId> ids;
    snapshot->index->lookup("www.example.com", ids);
    REQUIRE(ids.size() == 1);
    CHECK(names[ids[0]] == "ttl");
    snapshot->index->lookup("a.cdn.example.org", ids);
    CHECK(ids.size() == 1);
    snapshot->index->lookup("off.example", ids);
    CHECK(ids.empty());
    snapshot->index->lookup("unused.example", ids);
    CHECK(ids.empty());
}

TEST_CASE("snapshot builder: IPv6 disabled leaves set_v6 empty") {
    CacheManager cache("/nonexistent/cache");
    ListStreamer streamer(cache);
    const Config config = config_from(R"({
      "lists":{"a":{"domains":["a.example"]}},
      "route":{"rules":[{"list":["a"],"outbound":"wan"}]}})");
    // With IPv6 off the plan declares no v6 set; a stray v6 declaration is
    // ignored as well.
    std::vector<FirewallSetDeclaration> sets{
        {"kpbr4d_a", FirewallFamily::ipv4, 0}, {"kpbr6d_a", FirewallFamily::ipv6, 0}};
    const auto snapshot = build_intercept_snapshot(config, sets, false, InterceptEffective{}, streamer);
    REQUIRE(snapshot->targets.size() == 1);
    CHECK(snapshot->targets[0].set_v4 == "kpbr4d_a");
    CHECK(snapshot->targets[0].set_v6.empty());
    CHECK(snapshot->targets[0].min_ttl_s == 300);

    sets.erase(sets.begin());
    const auto v6_only = build_intercept_snapshot(config, sets, true, InterceptEffective{}, streamer);
    REQUIRE(v6_only->targets.size() == 1);
    CHECK(v6_only->targets[0].set_v4.empty());
    CHECK(v6_only->targets[0].set_v6 == "kpbr6d_a");

    const auto none = build_intercept_snapshot(config, {}, true, InterceptEffective{}, streamer);
    CHECK(none->index->list_names().empty());
    CHECK(none->targets.empty());
}

TEST_CASE("intercept event JSON carries the documented fields") {
    InterceptEvent event;
    event.seq = 7;
    event.ts_ms = 1712345678123;
    event.source = InterceptSource::sni;
    event.client_ip = "192.168.1.10";
    event.domain = "example.com";
    event.lists = {"streaming"};
    event.ips = {"203.0.113.7"};
    event.added = 1;
    event.refreshed = 2;
    event.errors = 3;
    event.hold_us = 180;
    event.parse_us = 12;
    event.set_write_us = 95;
    event.timed_out = true;
    const auto json = intercept_event_to_json(event);
    CHECK(json["parse_us"] == 12);
    CHECK(json["set_write_us"] == 95);
    CHECK(json["type"] == "INTERCEPT");
    CHECK(json["seq"] == 7);
    CHECK(json["ts_ms"] == 1712345678123);
    CHECK(json["source"] == "sni");
    CHECK(json["client_ip"] == "192.168.1.10");
    CHECK(json["domain"] == "example.com");
    CHECK(json["lists"] == nlohmann::json::array({"streaming"}));
    CHECK(json["ips"] == nlohmann::json::array({"203.0.113.7"}));
    CHECK(json["added"] == 1);
    CHECK(json["refreshed"] == 2);
    CHECK(json["errors"] == 3);
    CHECK(json["hold_us"] == 180);
    CHECK(json["timed_out"] == true);
    // Round-trips through the generated schema type.
    CHECK_NOTHROW(json.get<api::DnsTestInterceptEvent>());
    for (const auto source : {InterceptSource::dns, InterceptSource::http, InterceptSource::quic,
                              InterceptSource::marker}) {
        event.source = source;
        CHECK_NOTHROW(intercept_event_to_json(event).get<api::DnsTestInterceptEvent>());
    }
}

#ifdef WITH_API
TEST_CASE("health service JSON contains the intercept object") {
    InterceptCounters counters;
    counters.dns_packets = 5;
    counters.set_added = 3;
    counters.set_errors = 1;
    InterceptCapabilities caps = all_caps();
    caps.connbytes = false;
    auto eff = resolve_effective_intercept(config_from("{}"), FirewallBackend::iptables, caps);

    ServiceHealthState state;
    state.intercept = make_intercept_health(eff, /*running=*/true, &counters, 42);
    const auto json = nlohmann::json(build_health_response(state));
    REQUIRE(json.contains("intercept"));
    const auto& intercept = json["intercept"];
    CHECK(intercept["enabled"] == true);
    CHECK(intercept["running"] == true);
    CHECK(intercept["dns_hold_active"] == true);
    CHECK(intercept["l7_active"] == false);
    CHECK(intercept["queue_num"] == 9053);
    CHECK(intercept["nflog_group"] == 9054);
    CHECK(intercept["events_seq"] == 42);
    CHECK(intercept["capabilities"]["connbytes"] == false);
    CHECK(intercept["capabilities"]["nfqueue"] == true);
    REQUIRE(intercept["reasons"].size() == 1);
    CHECK(intercept["counters"]["dns_packets"] == 5);
    CHECK(intercept["counters"]["set_added"] == 3);
    CHECK(intercept["counters"]["set_errors"] == 1);

    // Not running: nothing is reported active even if configured.
    state.intercept = make_intercept_health(eff, false, nullptr, 0);
    const auto stopped = nlohmann::json(build_health_response(state));
    CHECK(stopped["intercept"]["dns_hold_active"] == false);
    CHECK(stopped["intercept"]["counters"].is_null());

    // Without interception data the field is null (generated optional style).
    CHECK(nlohmann::json(build_health_response(ServiceHealthState{}))["intercept"].is_null());
}
#endif

TEST_CASE("intercept: event gap detection and notices") {
    std::vector<InterceptEvent> events(2);
    events[0].seq = 11;
    events[1].seq = 12;
    CHECK_FALSE(detect_event_gap(10, events).has_value());  // contiguous
    CHECK_FALSE(detect_event_gap(10, {}).has_value());
    const auto gap = detect_event_gap(4, events);
    REQUIRE(gap.has_value());
    CHECK(gap->from_seq == 5);
    CHECK(gap->to_seq == 10);
    const auto json = event_gap_to_json(*gap);
    CHECK(json["type"] == "GAP");
    CHECK_NOTHROW(json.get<api::DnsTestGapEvent>());

    // Dropped subscriber messages: INTERCEPT events and an earlier GAP.
    CHECK(nlohmann::json::parse(gap_notice_for_dropped(R"({"type":"INTERCEPT","seq":20})",
                                                       R"({"type":"INTERCEPT","seq":25})")) ==
          nlohmann::json::parse(R"({"type":"GAP","from_seq":20,"to_seq":25})"));
    CHECK(nlohmann::json::parse(gap_notice_for_dropped(R"({"type":"GAP","from_seq":3,"to_seq":9})",
                                                       R"({"type":"INTERCEPT","seq":30})")) ==
          nlohmann::json::parse(R"({"type":"GAP","from_seq":3,"to_seq":30})"));
    CHECK(gap_notice_for_dropped("garbage", "{}").empty());
}

TEST_CASE("intercept: nfnetlink_queue parsing") {
    const std::string text =
        "    0  -4242     1  2  65531     0     0     55  1\n"
        " 9053   4321     7  2  65535    13     5  98765  1\n";
    const auto q = parse_nfnetlink_queue(text, 9053);
    REQUIRE(q.has_value());
    CHECK(q->queue_total == 7);
    CHECK(q->queue_dropped == 13);
    CHECK(q->user_dropped == 5);
    CHECK(q->id_sequence == 98765);
    CHECK_FALSE(parse_nfnetlink_queue(text, 9054).has_value());
    CHECK_FALSE(parse_nfnetlink_queue("", 0).has_value());
    CHECK_FALSE(parse_nfnetlink_queue("junk line\n", 0).has_value());
}
