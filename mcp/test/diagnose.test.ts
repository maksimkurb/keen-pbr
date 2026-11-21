import { describe, expect, it } from "vitest";
import { KeenPbrClient } from "../src/client.js";
import { collectDiagnosis } from "../src/diagnose.js";
import {
  degradedHealth,
  degradedInterfaces,
  degradedIssues,
  degradedOutbounds,
  degradedRouting,
  healthyHealth,
  healthyInterfaces,
  healthyIssues,
  healthyOutbounds,
  healthyRouting,
} from "./fixtures.js";
import { MockHttp, json } from "./mock-http.js";

function clientFor(mock: MockHttp): KeenPbrClient {
  return new KeenPbrClient(
    { baseUrl: "http://router.test:12121", token: "test-token", timeoutMs: 5_000 },
    mock.fetch,
  );
}

describe("diagnose aggregation", () => {
  it("prioritises failing outbounds, issues and warning codes", async () => {
    const mock = new MockHttp()
      .onJson("GET", "/api/health/service", 200, degradedHealth())
      .onJson("GET", "/api/health/routing", 200, degradedRouting())
      .onJson("GET", "/api/health/issues", 200, degradedIssues())
      .onJson("GET", "/api/runtime/outbounds", 200, degradedOutbounds())
      .onJson("GET", "/api/runtime/interfaces", 200, degradedInterfaces());

    const diagnosis = await collectDiagnosis(clientFor(mock));

    expect(diagnosis.verdict).toBe("critical");
    expect(diagnosis.service.status).toBe("degraded");
    expect(diagnosis.service.config_is_draft).toBe(true);

    expect(diagnosis.outbounds).toMatchObject({
      reachable: true,
      total: 2,
      healthy: 1,
      unavailable: 1,
      degraded: 0,
    });
    expect(diagnosis.outbounds.failing[0]).toMatchObject({
      tag: "vpn",
      status: "unavailable",
      interfaces: [{ interface_name: "tun0", status: "unavailable" }],
    });

    expect(diagnosis.interfaces).toMatchObject({ total: 3, up: 2, down: 1, down_names: ["tun0"] });

    const vpnProblem = diagnosis.problems.find((problem) => problem.title === "vpn is unavailable");
    expect(vpnProblem).toMatchObject({ severity: "error", source: "outbound", outbound: "vpn" });
    expect(
      diagnosis.problems.some((problem) => problem.title === "dnsmasq does not serve the keen-pbr config"),
    ).toBe(true);

    expect(diagnosis.routing.failed_checks).toContain("firewall rule keen-pbr-vpn: missing");
    expect(diagnosis.routing.warnings).toEqual([
      {
        code: "nat_missing",
        title: "NAT is missing for an outbound interface",
        message: "no MASQUERADE rule covers eth1",
        interface: "eth1",
        outbound: "wan2",
      },
    ]);

    expect(diagnosis.headline).toContain("error");
    expect(diagnosis.headline).toContain("vpn is unavailable");
    expect(diagnosis.suggested_next_step).toContain("get_runtime_outbounds");
    expect(diagnosis.suggested_next_step).toContain("vpn");
  });

  it("falls back to routing checks when the issues endpoint fails", async () => {
    const mock = new MockHttp()
      .onJson("GET", "/api/health/service", 200, healthyHealth())
      .on("GET", "/api/health/routing", () => json({ error: "boom" }, 500))
      .onJson("GET", "/api/health/issues", 200, healthyIssues())
      .onJson("GET", "/api/runtime/outbounds", 200, healthyOutbounds())
      .onJson("GET", "/api/runtime/interfaces", 200, healthyInterfaces());

    // Healthy issues + unavailable routing endpoint: the failure is reported.
    const diagnosis = await collectDiagnosis(clientFor(mock));
    expect(diagnosis.verdict).toBe("critical");
    expect(diagnosis.problems).toEqual(
      expect.arrayContaining([
        expect.objectContaining({
          source: "api",
          title: "Cannot read routing health (/api/health/routing)",
          severity: "error",
        }),
      ]),
    );
    expect(diagnosis.routing.reachable).toBe(false);
  });

  it("derives routing problems when the issues endpoint is unavailable", async () => {
    const mock = new MockHttp()
      .onJson("GET", "/api/health/service", 200, healthyHealth())
      .onJson("GET", "/api/health/routing", 200, {
        ...degradedRouting(),
        overall: "degraded",
        firewall: { chain_present: false, prerouting_hook_present: false, verification_state: "failed" },
      })
      .on("GET", "/api/health/issues", () => json({ error: "issues unavailable" }, 500))
      .onJson("GET", "/api/runtime/outbounds", 200, healthyOutbounds())
      .onJson("GET", "/api/runtime/interfaces", 200, healthyInterfaces());

    const diagnosis = await collectDiagnosis(clientFor(mock));
    expect(diagnosis.verdict).toBe("critical");
    expect(diagnosis.problems).toEqual(
      expect.arrayContaining([
        expect.objectContaining({
          source: "firewall",
          title: "keen-pbr firewall chain is missing",
        }),
      ]),
    );
    expect(diagnosis.suggested_next_step).toContain("routing_test");
  });

  it("reports an unreachable API when every endpoint fails", async () => {
    const mock = new MockHttp();
    const diagnosis = await collectDiagnosis(clientFor(mock));

    expect(diagnosis.verdict).toBe("unreachable");
    expect(diagnosis.problems).toHaveLength(5);
    expect(diagnosis.problems.every((problem) => problem.source === "api")).toBe(true);
    expect(diagnosis.suggested_next_step).toContain("KEEN_PBR_URL");
    expect(diagnosis.suggested_next_step).toContain("KEEN_PBR_TOKEN");
  });

  it("reports healthy routing without inventing problems", async () => {
    const mock = new MockHttp()
      .onJson("GET", "/api/health/service", 200, healthyHealth())
      .onJson("GET", "/api/health/routing", 200, healthyRouting())
      .onJson("GET", "/api/health/issues", 200, healthyIssues())
      .onJson("GET", "/api/runtime/outbounds", 200, healthyOutbounds())
      .onJson("GET", "/api/runtime/interfaces", 200, healthyInterfaces());

    const diagnosis = await collectDiagnosis(clientFor(mock));
    expect(diagnosis.verdict).toBe("healthy");
    expect(diagnosis.problems).toEqual([]);
    expect(diagnosis.headline).toContain("No service, routing or outbound problems");
    expect(diagnosis.suggested_next_step).toContain("routing_test");
  });

  it("sends the bearer token and never leaks it into the diagnosis", async () => {
    const mock = new MockHttp()
      .onJson("GET", "/api/health/service", 200, healthyHealth())
      .onJson("GET", "/api/health/routing", 200, healthyRouting())
      .onJson("GET", "/api/health/issues", 200, healthyIssues())
      .onJson("GET", "/api/runtime/outbounds", 200, healthyOutbounds())
      .onJson("GET", "/api/runtime/interfaces", 200, healthyInterfaces());

    const diagnosis = await collectDiagnosis(clientFor(mock));
    expect(mock.requests).toHaveLength(5);
    expect(mock.requests.every((request) => request.headers.authorization === "Bearer test-token")).toBe(true);
    expect(JSON.stringify(diagnosis)).not.toContain("test-token");
  });
});
