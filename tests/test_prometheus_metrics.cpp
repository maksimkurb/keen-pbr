#ifdef WITH_API

#include <doctest/doctest.h>

#include "../src/api/prometheus_metrics.hpp"

#include <algorithm>
#include <sstream>

namespace keen_pbr3 {
namespace {

bool has(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

std::size_t count_of(const std::string& text, const std::string& needle) {
    std::size_t count = 0;
    for (auto at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) ++count;
    return count;
}

const char* kWanA =
    "outbound=\"wan\\\"a\",test_outbound=\"healthcheck\",interface=\"eth0\\nbond\",type=\"icmptest\"";
const char* kWanB =
    "outbound=\"wan_b\",test_outbound=\"healthcheck\",interface=\"eth1\",type=\"icmptest\"";

struct Fixture {
    InterceptCounters counters;
    NetlinkMetricsSnapshot netlink;
    ControlRuntimeSnapshot runtime;
    OutboundRuntimeSnapshot outbounds;
    Config config;
    DaemonMetrics daemon;

    Fixture() {
        Outbound a;
        a.tag = "wan\"a";
        a.type = OutboundType::INTERFACE;
        a.interface = "eth0\nbond";
        Outbound b;
        b.tag = "wan_b";
        b.type = OutboundType::INTERFACE;
        b.interface = "eth1";
        config.outbounds = std::vector<Outbound>{a, b};
        UrltestState state;
        state.config.type = OutboundType::ICMPTEST;
        OutboundGroup group;
        api::OutboundGroupMemberElement first;
        first.outbound = "wan\"a";
        api::OutboundGroupMemberElement second;
        second.outbound = "wan_b";
        group.members = std::vector<api::OutboundGroupMemberElement>{first, second};
        state.config.outbound_groups = std::vector<OutboundGroup>{group};
        state.selected_outbound = "wan_b";
        state.selection_changes = 3;
        outbounds.urltest_states["healthcheck"] = state;
    }

    ProbeMetrics& probe(const std::string& tag) {
        return outbounds.urltest_states["healthcheck"].probe_metrics[tag];
    }

    std::string render() const {
        return prometheus_metrics(&counters, netlink, runtime, outbounds, config, daemon, "nftables");
    }
};

} // namespace

TEST_CASE("Prometheus exports build, process and labelled error counters") {
    Fixture f;
    f.runtime.realized_rules.push_back({0, {}, "wan", RuleActionType::Mark, 1});
    f.daemon.process_start_unix_s = 1700000000;
    f.daemon.firewall_apply_errors = 4;
    f.netlink.errors = 3;
    f.counters.set_errors.store(5);
    f.counters.conntrack_errors.store(6);
    f.counters.dns_parse_errors.store(7);
    f.counters.dns_tcp_partial.store(8);
    f.counters.dns_late_write_errors.store(9);
    const auto text = f.render();
    CHECK(f.render() == text);
    CHECK(has(text, "keen_pbr_build_info{version=\""));
    CHECK(has(text, "firewall_backend=\"nftables\"} 1\n"));
    CHECK(has(text, "keen_pbr_active_rules 1\n"));
    CHECK(has(text, "keen_pbr_process_start_time_seconds 1700000000\n"));
    CHECK(has(text, "keen_pbr_errors_total{subsystem=\"firewall_apply\"} 4\n"));
    CHECK(has(text, "keen_pbr_errors_total{subsystem=\"netlink\"} 3\n"));
    CHECK(has(text, "keen_pbr_errors_total{subsystem=\"set_write\"} 5\n"));
    CHECK(has(text, "keen_pbr_errors_total{subsystem=\"conntrack\"} 6\n"));
    CHECK(has(text, "keen_pbr_errors_total{subsystem=\"dns_parse\"} 7\n"));
    CHECK(has(text, "keen_pbr_errors_total{subsystem=\"dns_tcp_partial\"} 8\n"));
    CHECK(has(text, "keen_pbr_errors_total{subsystem=\"dns_late_write\"} 9\n"));
    CHECK(count_of(text, "# TYPE keen_pbr_errors_total counter") == 1);
    CHECK_FALSE(has(text, "category=\""));
}

TEST_CASE("Prometheus errors are emitted as zeros without intercept counters") {
    OutboundRuntimeSnapshot none;
    const auto text = prometheus_metrics(nullptr, {}, {}, none, Config{}, {}, "iptables");
    for (const char* subsystem : {"firewall_apply", "netlink", "set_write", "conntrack",
                                  "dns_parse", "dns_tcp_partial", "dns_late_write"}) {
        CHECK(has(text, std::string("keen_pbr_errors_total{subsystem=\"") + subsystem + "\"} 0\n"));
    }
    CHECK_FALSE(has(text, "keen_pbr_intercept_packets_total"));
    CHECK_FALSE(has(text, "keen_pbr_probe_"));
    CHECK_FALSE(has(text, "keen_pbr_urltest_"));
    CHECK_FALSE(has(text, "keen_pbr_set_cache_entries"));
    CHECK_FALSE(has(text, "keen_pbr_config_reload_last_success"));
    CHECK_FALSE(has(text, "keen_pbr_list_"));
}

TEST_CASE("Prometheus intercept counters are labelled") {
    Fixture f;
    f.counters.dns_packets.store(7);
    f.counters.l7_packets.store(2);
    f.counters.dns_matched.store(5);
    f.counters.l7_matched.store(1);
    f.counters.dns_timeout_budget_spent_by_batch.store(1);
    f.counters.dns_timeout_admission_blocked.store(2);
    f.counters.dns_timeout_own_write_slow.store(3);
    f.counters.dns_timeout_late_batch_full.store(4);
    f.counters.dns_timeout_other.store(5);
    f.counters.dns_late_writes.store(6);
    f.counters.queue_overruns.store(7);
    f.counters.log_overruns.store(8);
    f.counters.set_added.store(9);
    f.counters.set_refreshed.store(10);
    f.counters.refresh_skipped.store(11);
    f.counters.dns_refresh_deferred.store(12);
    f.counters.refresh_dropped.store(13);
    f.counters.set_cache_hits.store(14);
    f.counters.set_cache_misses.store(15);
    f.counters.set_cache_entries.store(16);
    f.counters.conntrack_requests.store(17);
    f.counters.conntrack_deleted.store(18);
    const auto text = f.render();
    CHECK(has(text, "keen_pbr_intercept_packets_total{path=\"dns\"} 7\n"));
    CHECK(has(text, "keen_pbr_intercept_packets_total{path=\"l7\"} 2\n"));
    CHECK(has(text, "keen_pbr_intercept_matches_total{path=\"dns\"} 5\n"));
    CHECK(has(text, "keen_pbr_intercept_matches_total{path=\"l7\"} 1\n"));
    CHECK(has(text, "keen_pbr_dns_hold_timeouts_total{cause=\"batch_budget\"} 1\n"));
    CHECK(has(text, "keen_pbr_dns_hold_timeouts_total{cause=\"admission_blocked\"} 2\n"));
    CHECK(has(text, "keen_pbr_dns_hold_timeouts_total{cause=\"own_write_slow\"} 3\n"));
    CHECK(has(text, "keen_pbr_dns_hold_timeouts_total{cause=\"late_batch_full\"} 4\n"));
    CHECK(has(text, "keen_pbr_dns_hold_timeouts_total{cause=\"other\"} 5\n"));
    CHECK(has(text, "keen_pbr_dns_late_writes_total 6\n"));
    CHECK(has(text, "keen_pbr_queue_overruns_total{queue=\"nfqueue\"} 7\n"));
    CHECK(has(text, "keen_pbr_queue_overruns_total{queue=\"nflog\"} 8\n"));
    CHECK(has(text, "keen_pbr_set_writes_total{kind=\"add\"} 9\n"));
    CHECK(has(text, "keen_pbr_set_writes_total{kind=\"refresh\"} 10\n"));
    CHECK(has(text, "keen_pbr_set_refresh_total{result=\"skipped\"} 11\n"));
    CHECK(has(text, "keen_pbr_set_refresh_total{result=\"deferred\"} 12\n"));
    CHECK(has(text, "keen_pbr_set_refresh_total{result=\"dropped\"} 13\n"));
    CHECK(has(text, "keen_pbr_set_cache_lookups_total{result=\"hit\"} 14\n"));
    CHECK(has(text, "keen_pbr_set_cache_lookups_total{result=\"miss\"} 15\n"));
    CHECK(has(text, "keen_pbr_set_cache_entries 16\n"));
    CHECK(has(text, "keen_pbr_conntrack_requests_total 17\n"));
    CHECK(has(text, "keen_pbr_conntrack_deleted_total 18\n"));
    CHECK(count_of(text, "# TYPE keen_pbr_intercept_packets_total counter") == 1);
}

TEST_CASE("Prometheus write histograms share one family with a 0.025 edge") {
    Fixture f;
    f.counters.dns_write_latency.record(1000, 2);
    f.counters.dns_write_latency.record(30000, 2);
    f.counters.late_write_latency.record(25000, 1);
    f.counters.l7_write_latency.record(200, 1);
    f.counters.dns_hold_latency.record(25000, 0);
    f.counters.dns_queue_wait_latency.record(120, 0);
    const auto text = f.render();
    CHECK(count_of(text, "# TYPE keen_pbr_set_write_duration_seconds histogram") == 1);
    CHECK(has(text, "keen_pbr_set_write_duration_seconds_bucket{path=\"dns\",le=\"0.001\"} 1\n"));
    CHECK(has(text, "keen_pbr_set_write_duration_seconds_bucket{path=\"dns\",le=\"0.025\"} 1\n"));
    CHECK(has(text, "keen_pbr_set_write_duration_seconds_bucket{path=\"dns\",le=\"+Inf\"} 2\n"));
    CHECK(has(text, "keen_pbr_set_write_duration_seconds_bucket{path=\"late_dns\",le=\"0.025\"} 1\n"));
    CHECK(has(text, "keen_pbr_set_write_duration_seconds_bucket{path=\"l7\",le=\"0.00025\"} 1\n"));
    CHECK(has(text, "keen_pbr_set_write_duration_seconds_sum{path=\"dns\"} 0.031\n"));
    CHECK(has(text, "keen_pbr_set_write_duration_seconds_count{path=\"dns\"} 2\n"));
    CHECK(has(text, "keen_pbr_dns_hold_duration_seconds_bucket{le=\"0.025\"} 1\n"));
    CHECK(has(text, "keen_pbr_dns_hold_duration_seconds_sum 0.025\n"));
    CHECK(has(text, "keen_pbr_dns_hold_duration_seconds_count 1\n"));
    CHECK(has(text, "keen_pbr_dns_queue_wait_duration_seconds_bucket{le=\"0.00025\"} 1\n"));
}

TEST_CASE("Prometheus drops removed metrics") {
    Fixture f;
    f.counters.dns_write_latency.record(1000, 2);
    f.probe("wan_b").attempts = 1;
    const auto text = f.render();
    for (const char* removed : {
             "keen_pbr_intercept_dns_packets_total", "keen_pbr_intercept_l7_packets_total",
             "keen_pbr_intercept_dns_aaaa_ignored", "keen_pbr_intercept_marker_hits",
             "keen_pbr_intercept_set_write_slow", "keen_pbr_set_write_max_microseconds",
             "keen_pbr_set_write_max_elements", "keen_pbr_netlink_write_",
             "keen_pbr_dns_admission_wait_duration_seconds", "keen_pbr_dns_late_write_duration_seconds",
             "keen_pbr_dns_write_duration_seconds", "keen_pbr_l7_write_duration_seconds",
             "keen_pbr_dns_timeout_", "keen_pbr_firewall_apply_errors_total",
             "keen_pbr_netlink_errors_total", "keen_pbr_intercept_set_cache_entries",
             "keen_pbr_probe_success_ratio", "keen_pbr_probe_packets_attempted_total",
             "keen_pbr_probe_packets_failed_total", "keen_pbr_probe_latency_seconds_bucket",
             "keen_pbr_probe_latency_seconds_sum", "keen_pbr_probe_latency_seconds_count"}) {
        INFO(removed);
        CHECK_FALSE(has(text, removed));
    }
    CHECK_FALSE(has(text, "# TYPE keen_pbr_probe_latency_seconds histogram"));
}

TEST_CASE("Prometheus probe gauges follow the last probe result") {
    Fixture f;
    auto& ok = f.probe("wan\"a");
    ok.attempts = 4;
    ok.successes = 3;
    ok.packets_sent = 12;
    ok.packets_received = 9;
    ok.last_up = true;
    ok.last_success_unix_s = 1700000100;
    ok.latency_us = 345;  // sub-millisecond must not be truncated
    ok.latency_min_us = 300;
    ok.latency_max_us = 420;
    auto& failed = f.probe("wan_b");
    failed.attempts = 2;
    failed.successes = 0;
    failed.packets_sent = 6;
    failed.last_up = false;
    const auto text = f.render();
    const std::string a = kWanA, b = kWanB;
    CHECK(has(text, "keen_pbr_probe_attempts_total{" + a + "} 4\n"));
    CHECK(has(text, "keen_pbr_probe_successes_total{" + a + "} 3\n"));
    CHECK(has(text, "keen_pbr_probe_up{" + a + "} 1\n"));
    CHECK(has(text, "keen_pbr_probe_up{" + b + "} 0\n"));
    CHECK(has(text, "keen_pbr_probe_last_success_timestamp_seconds{" + a + "} 1700000100\n"));
    CHECK_FALSE(has(text, "keen_pbr_probe_last_success_timestamp_seconds{" + b));
    CHECK(has(text, "keen_pbr_probe_latency_seconds{" + a + "} 0.000345\n"));
    CHECK(has(text, "keen_pbr_probe_latency_min_seconds{" + a + "} 0.0003\n"));
    CHECK(has(text, "keen_pbr_probe_latency_max_seconds{" + a + "} 0.00042\n"));
    // A failed probe has no latency series at all (a gap, not a zero).
    CHECK_FALSE(has(text, "keen_pbr_probe_latency_seconds{" + b));
    CHECK_FALSE(has(text, "keen_pbr_probe_latency_min_seconds{" + b));
    CHECK_FALSE(has(text, "keen_pbr_probe_latency_max_seconds{" + b));
    CHECK(has(text, "keen_pbr_probe_attempts_total{" + b + "} 2\n"));
    CHECK(count_of(text, "# TYPE keen_pbr_probe_latency_seconds gauge") == 1);
}

TEST_CASE("Prometheus omits probe gauges before the first probe") {
    Fixture f;
    f.probe("wan_b");  // registered, never completed
    const auto text = f.render();
    CHECK(has(text, "keen_pbr_probe_attempts_total{" + std::string(kWanB) + "} 0\n"));
    CHECK_FALSE(has(text, "keen_pbr_probe_up"));
    CHECK_FALSE(has(text, "keen_pbr_probe_last_success_timestamp_seconds"));
    CHECK_FALSE(has(text, "keen_pbr_probe_latency_seconds"));
    CHECK_FALSE(has(text, "keen_pbr_probe_latency_min_seconds"));
    CHECK_FALSE(has(text, "keen_pbr_probe_latency_max_seconds"));
}

TEST_CASE("Prometheus packet counters and min/max are ICMP-only") {
    Fixture f;
    UrltestState url_state;
    url_state.config.type = OutboundType::URLTEST;
    ProbeMetrics url;
    url.attempts = 2;
    url.successes = 2;
    url.last_up = true;
    url.latency_us = 12345;
    url_state.probe_metrics["wan_b"] = url;
    f.outbounds.urltest_states["web"] = url_state;
    auto& icmp = f.probe("wan_b");
    icmp.attempts = 1;
    icmp.successes = 1;
    icmp.packets_sent = 3;
    icmp.packets_received = 3;
    icmp.last_up = true;
    icmp.latency_us = 1000;
    icmp.latency_min_us = 900;
    icmp.latency_max_us = 1100;
    const auto text = f.render();
    const std::string url_labels = "outbound=\"wan_b\",test_outbound=\"web\",interface=\"eth1\",type=\"urltest\"";
    CHECK(has(text, "keen_pbr_probe_packets_sent_total{" + std::string(kWanB) + "} 3\n"));
    CHECK(has(text, "keen_pbr_probe_packets_received_total{" + std::string(kWanB) + "} 3\n"));
    CHECK_FALSE(has(text, "keen_pbr_probe_packets_sent_total{" + url_labels));
    CHECK_FALSE(has(text, "keen_pbr_probe_packets_received_total{" + url_labels));
    CHECK(has(text, "keen_pbr_probe_latency_seconds{" + url_labels + "} 0.012345\n"));
    CHECK_FALSE(has(text, "keen_pbr_probe_latency_min_seconds{" + url_labels));
    CHECK_FALSE(has(text, "keen_pbr_probe_latency_max_seconds{" + url_labels));
    CHECK(has(text, "keen_pbr_probe_attempts_total{" + url_labels + "} 2\n"));
}

TEST_CASE("Prometheus exports urltest selection and change count") {
    Fixture f;
    const auto text = f.render();
    CHECK(has(text, "keen_pbr_urltest_selected{group=\"healthcheck\",outbound=\"wan\\\"a\"} 0\n"));
    CHECK(has(text, "keen_pbr_urltest_selected{group=\"healthcheck\",outbound=\"wan_b\"} 1\n"));
    CHECK(has(text, "keen_pbr_urltest_selection_changes_total{group=\"healthcheck\"} 3\n"));
    CHECK(count_of(text, "# TYPE keen_pbr_urltest_selected gauge") == 1);
}

TEST_CASE("Prometheus exports config reload and remote list telemetry") {
    Fixture f;
    ListConfig remote;
    remote.url = "https://example.test/list.txt";
    ListConfig local;
    f.config.lists = std::map<std::string, ListConfig>{{"fresh", remote}, {"never", remote}, {"local", local}};
    f.daemon.config_reload_last_success_unix_s = 1700000500;
    f.daemon.config_reload_errors = 2;
    f.daemon.lists["fresh"] = ListRefreshStats{1700000400, 1};
    f.daemon.lists["never"] = ListRefreshStats{std::nullopt, 4};
    const auto text = f.render();
    CHECK(has(text, "keen_pbr_config_reload_last_success_timestamp_seconds 1700000500\n"));
    CHECK(has(text, "keen_pbr_config_reload_errors_total 2\n"));
    CHECK(has(text, "keen_pbr_list_last_update_timestamp_seconds{list=\"fresh\"} 1700000400\n"));
    CHECK_FALSE(has(text, "keen_pbr_list_last_update_timestamp_seconds{list=\"never\"}"));
    CHECK(has(text, "keen_pbr_list_update_errors_total{list=\"fresh\"} 1\n"));
    CHECK(has(text, "keen_pbr_list_update_errors_total{list=\"never\"} 4\n"));
    CHECK_FALSE(has(text, "list=\"local\""));
}

TEST_CASE("Prometheus emits HELP and TYPE once per family and no empty family") {
    Fixture f;
    f.probe("wan_b").attempts = 1;
    const auto text = f.render();
    std::istringstream lines(text);
    std::string line;
    std::vector<std::string> families;
    while (std::getline(lines, line)) {
        if (line.rfind("# TYPE ", 0) != 0) continue;
        const auto name = line.substr(7, line.find(' ', 7) - 7);
        CHECK(std::find(families.begin(), families.end(), name) == families.end());
        families.push_back(name);
    }
    CHECK_FALSE(families.empty());
    // Every TYPE family has at least one sample line.
    for (const auto& name : families) {
        const bool hist = has(text, "# TYPE " + name + " histogram");
        CHECK((hist ? has(text, "\n" + name + "_count") : (has(text, "\n" + name + " ") || has(text, "\n" + name + "{"))));
    }
}

TEST_CASE("Prometheus exports balance classification and skip_marked counters") {
    Fixture f;
    CHECK_FALSE(has(f.render(), "keen_pbr_balance_classifications_total"));
    CHECK_FALSE(has(f.render(), "keen_pbr_skip_marked_packets_total"));

    f.daemon.firewall_counters.balance_classifications = {
        {"lb", "wan1", "ipv4", 400}, {"lb", "wan2", "ipv4", 380}, {"lb", "wan\"1", "ipv6", 7}};
    f.daemon.firewall_counters.skip_marked_packets = {{"ipv4", 905}, {"ipv6", 12}};
    const auto text = f.render();
    CHECK(count_of(text, "# TYPE keen_pbr_balance_classifications_total counter") == 1);
    CHECK(has(text, "keen_pbr_balance_classifications_total{outbound=\"lb\",candidate=\"wan1\",family=\"ipv4\"} 400\n"));
    CHECK(has(text, "keen_pbr_balance_classifications_total{outbound=\"lb\",candidate=\"wan2\",family=\"ipv4\"} 380\n"));
    CHECK(has(text, "keen_pbr_balance_classifications_total{outbound=\"lb\",candidate=\"wan\\\"1\",family=\"ipv6\"} 7\n"));
    CHECK(count_of(text, "# TYPE keen_pbr_skip_marked_packets_total counter") == 1);
    CHECK(has(text, "keen_pbr_skip_marked_packets_total{family=\"ipv4\"} 905\n"));
    CHECK(has(text, "keen_pbr_skip_marked_packets_total{family=\"ipv6\"} 12\n"));
}

} // namespace keen_pbr3

#endif // WITH_API
