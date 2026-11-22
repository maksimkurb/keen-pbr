import { describe, expect, it } from "vitest";
import { parseMetrics, summarizeMetrics } from "../src/metrics.js";

const METRICS = `# HELP keen_pbr_build_info Build identity.
# TYPE keen_pbr_build_info gauge
keen_pbr_build_info{version="3.4.0",commit="abc123"} 1
# HELP keen_pbr_active_rules Number of realized route rules.
# TYPE keen_pbr_active_rules gauge
keen_pbr_active_rules 3
# HELP keen_pbr_config_reload_errors_total Config reload errors.
# TYPE keen_pbr_config_reload_errors_total counter
keen_pbr_config_reload_errors_total 1
# HELP keen_pbr_errors_total Errors by subsystem.
# TYPE keen_pbr_errors_total counter
keen_pbr_errors_total{subsystem="firewall"} 2
keen_pbr_errors_total{subsystem="dns"} 0
# HELP keen_pbr_probe_up Probe health.
# TYPE keen_pbr_probe_up gauge
keen_pbr_probe_up{outbound="vpn",interface="tun0"} 0
keen_pbr_probe_up{outbound="wan",interface="eth0"} 1
# HELP keen_pbr_dns_proxy_upstream_up Upstream health.
# TYPE keen_pbr_dns_proxy_upstream_up gauge
keen_pbr_dns_proxy_upstream_up{server="vpn_dns",upstream="10.8.0.1"} 0
# HELP keen_pbr_list_update_errors_total List update errors.
# TYPE keen_pbr_list_update_errors_total counter
keen_pbr_list_update_errors_total{list="apple"} 1
keen_pbr_list_update_errors_total{list="google"} 0
# HELP keen_pbr_dns_proxy_queries_total DNS queries.
# TYPE keen_pbr_dns_proxy_queries_total counter
keen_pbr_dns_proxy_queries_total{transport="udp"} 42
keen_pbr_dns_proxy_queries_total{transport="tcp"} 7
`;

describe("metrics summary", () => {
  it("parses families and samples", () => {
    const parsed = parseMetrics(METRICS);
    expect(parsed.parseWarnings).toBe(0);
    expect(parsed.families.length).toBe(8);
    expect(parsed.families.find((family) => family.name === "keen_pbr_dns_proxy_queries_total")?.samples).toHaveLength(2);
  });

  it("summarises without dumping every series", () => {
    const summary = summarizeMetrics(METRICS);
    expect(summary.family_count).toBe(8);
    expect(summary.sample_count).toBe(12);
    expect(summary.build).toEqual({ version: "3.4.0", commit: "abc123" });
    expect(summary.highlights.active_rules).toBe(3);
    expect(summary.highlights.config_reload_errors_total).toBe(1);
    expect(summary.highlights.errors_total_nonzero).toEqual([
      {
        name: "keen_pbr_errors_total",
        labels: { subsystem: "firewall" },
        value: 2,
      },
    ]);
    expect(summary.highlights.probes_down).toEqual([
      {
        name: "keen_pbr_probe_up",
        labels: { outbound: "vpn", interface: "tun0" },
        value: 0,
      },
    ]);
    expect(summary.highlights.dns_upstreams_down).toHaveLength(1);
    expect(summary.highlights.list_update_errors).toHaveLength(1);
    expect(summary.filter).toBeUndefined();
  });

  it("returns matching samples for a filter and honours the cap", () => {
    const all = summarizeMetrics(METRICS, { filter: "dns_proxy", maxSamples: 10 });
    expect(all.filter?.matched_families).toEqual([
      "keen_pbr_dns_proxy_upstream_up",
      "keen_pbr_dns_proxy_queries_total",
    ]);
    expect(all.filter?.samples).toHaveLength(3);
    expect(all.filter?.truncated).toBe(false);

    const capped = summarizeMetrics(METRICS, { filter: "dns_proxy", maxSamples: 1 });
    expect(capped.filter?.samples).toHaveLength(1);
    expect(capped.filter?.truncated).toBe(true);
  });

  it("counts unparseable sample lines instead of failing", () => {
    const summary = summarizeMetrics(`${METRICS}\nthis is not a metric\n`);
    expect(summary.parse_warnings).toBe(1);
  });
});
