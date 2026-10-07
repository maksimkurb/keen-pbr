#ifdef WITH_API

#include <doctest/doctest.h>

#include "../src/api/prometheus_metrics.hpp"

namespace keen_pbr3 {

TEST_CASE("Prometheus exports cumulative counters and probe observations") {
    InterceptCounters counters;
    counters.dns_packets.store(7);
    counters.dns_write_latency.record(1000, 2);
    counters.dns_hold_latency.record(25000, 0);
    counters.dns_write_latency.max_elements.store(2);
    counters.netlink_write_metrics.total.record(2500);

    NetlinkMetricsSnapshot netlink;
    netlink.errors = 3;
    ControlRuntimeSnapshot runtime;
    runtime.realized_rules.push_back({0, {}, "wan", RuleActionType::Mark, 1});
    OutboundRuntimeSnapshot outbounds;
    UrltestState test_state;
    test_state.config.type = OutboundType::ICMPTEST;
    ProbeMetrics probe;
    probe.attempts = 2;
    probe.successes = 1;
    probe.latency_count = 1;
    probe.latency_sum_ms = 25;
    probe.latency_buckets[5] = 1;
    probe.packets_attempted = 3;
    probe.packets_sent = 3;
    probe.packets_received = 2;
    probe.packets_failed = 1;
    test_state.probe_metrics["wan\"a"] = probe;
    test_state.probe_metrics["wan_b"] = probe;
    outbounds.urltest_states["healthcheck"] = test_state;
    Config config;
    Outbound child;
    child.tag = "wan\"a";
    child.type = OutboundType::INTERFACE;
    child.interface = "eth0\nbond";
    Outbound second_child;
    second_child.tag = "wan_b";
    second_child.type = OutboundType::INTERFACE;
    second_child.interface = "eth1";
    config.outbounds = std::vector<Outbound>{child, second_child};

    const auto text = prometheus_metrics(&counters, netlink, runtime, outbounds, config, 4,
                                         "nftables");
    CHECK(prometheus_metrics(&counters, netlink, runtime, outbounds, config, 4,
                             "nftables") == text);
    CHECK(text.find("keen_pbr_active_rules 1\n") != std::string::npos);
    CHECK(text.find("keen_pbr_intercept_dns_packets_total 7\n") != std::string::npos);
    CHECK(text.find("keen_pbr_dns_write_duration_seconds_bucket{le=\"0.001000\"} 1\n") !=
          std::string::npos);
    CHECK(text.find("keen_pbr_dns_write_duration_seconds_count 1\n") != std::string::npos);
    CHECK(text.find("keen_pbr_dns_hold_duration_seconds_sum 0.025\n") != std::string::npos);
    CHECK(text.find("keen_pbr_set_write_max_elements{path=\"dns\"} 2\n") != std::string::npos);
    CHECK(text.find("keen_pbr_errors_total{category=\"firewall\"} 4\n") != std::string::npos);
    CHECK(text.find("keen_pbr_errors_total{category=\"kernel\"} 3\n") != std::string::npos);
    CHECK(text.find("keen_pbr_probe_success_ratio{outbound=\"wan\\\"a\",test_outbound=\"healthcheck\",interface=\"eth0\\nbond\",type=\"icmptest\"} 0.5") !=
          std::string::npos);
    CHECK(text.find("keen_pbr_probe_packets_received_total{outbound=\"wan\\\"a\",test_outbound=\"healthcheck\",interface=\"eth0\\nbond\",type=\"icmptest\"} 2\n") !=
          std::string::npos);
    CHECK(text.find("keen_pbr_probe_packets_failed_total{outbound=\"wan\\\"a\",test_outbound=\"healthcheck\",interface=\"eth0\\nbond\",type=\"icmptest\"} 1\n") !=
          std::string::npos);
    const auto attempts_header = text.find("# TYPE keen_pbr_probe_attempts_total counter");
    const auto success_header = text.find("# TYPE keen_pbr_probe_successes_total counter");
    REQUIRE(attempts_header != std::string::npos);
    REQUIRE(success_header != std::string::npos);
    CHECK(text.find("keen_pbr_probe_attempts_total{outbound=\"wan_b\"") < success_header);
    CHECK(text.find("keen_pbr_netlink_write_total_duration_seconds_count 1\n") !=
          std::string::npos);

    OutboundRuntimeSnapshot no_observations;
    const auto empty = prometheus_metrics(nullptr, {}, {}, no_observations, Config{}, 0,
                                          "iptables");
    CHECK(empty.find("keen_pbr_probe_success_ratio{") == std::string::npos);
    CHECK(empty.find("keen_pbr_probe_latency_seconds_bucket{") == std::string::npos);
    CHECK(empty.find("keen_pbr_errors_total{category=\"firewall\"} 0\n") !=
          std::string::npos);
}

} // namespace keen_pbr3

#endif // WITH_API
