import { describe, expect, it } from "vitest";
import { KeenPbrClient } from "../src/client.js";
import { updateRuleInDraft } from "../src/tools/rules.js";
import type { RouteRule } from "../src/types.js";
import { baseConfig, configState } from "./fixtures.js";
import { MockHttp } from "./mock-http.js";

function clientFor(mock: MockHttp): KeenPbrClient {
  return new KeenPbrClient(
    { baseUrl: "http://router.test:12121", token: "test-token", timeoutMs: 5_000 },
    mock.fetch,
  );
}

function ruleServer(config = baseConfig()): MockHttp {
  return new MockHttp()
    .onJson("GET", "/api/config", 200, configState(config))
    .onJson("POST", "/api/config", 200, { status: "ok", message: "Config staged in memory" });
}

function configWithRule(rule: RouteRule) {
  const config = baseConfig();
  config.route = { rules: [rule] };
  return config;
}

function postedRule(mock: MockHttp, index = 0): Record<string, unknown> {
  const body = mock.requestsFor("POST", "/api/config")[0]?.body as {
    route: { rules: Array<Record<string, unknown>> };
  };
  return body.route.rules[index] ?? {};
}

describe("update_rule nullable fields", () => {
  it("clears default_gateway with null and shows the removal in the diff", async () => {
    const mock = ruleServer(
      configWithRule({
        enabled: true,
        outbound: "vpn",
        default_gateway: "ipv4",
        dscp: 46,
        src_port: "1024-65535",
        proto: "tcp",
      }),
    );

    const result = await updateRuleInDraft(clientFor(mock), {
      index: 0,
      defaultGateway: null,
      save: false,
      saveWaitSeconds: 20,
    });

    expect(result.changed).toBe(true);
    expect(result.change_summary?.before).toContain("default gateway ipv4");
    expect(result.change_summary?.after).not.toContain("default gateway");
    expect(result.changes).toEqual(
      expect.arrayContaining([
        { path: "route.rules[0].default_gateway", before: "ipv4" },
      ]),
    );

    const rule = postedRule(mock);
    expect(rule).not.toHaveProperty("default_gateway");
    expect(rule.dscp).toBe(46);
    expect(rule.src_port).toBe("1024-65535");
    expect(rule.proto).toBe("tcp");
  });

  it("clears enabled with null while keeping the rule active by default", async () => {
    const mock = ruleServer(
      configWithRule({
        enabled: false,
        outbound: "vpn",
        default_gateway: "ipv4",
      }),
    );

    await updateRuleInDraft(clientFor(mock), {
      index: 0,
      enabled: null,
      save: false,
      saveWaitSeconds: 20,
    });

    const rule = postedRule(mock);
    expect(rule).not.toHaveProperty("enabled");
    expect(rule.default_gateway).toBe("ipv4");
  });

  it("leaves omitted optional fields unchanged", async () => {
    const mock = ruleServer(
      configWithRule({
        enabled: true,
        outbound: "vpn",
        default_gateway: "ipv4",
        dscp: 46,
      }),
    );

    await updateRuleInDraft(clientFor(mock), {
      index: 0,
      enabled: false,
      save: false,
      saveWaitSeconds: 20,
    });

    const rule = postedRule(mock);
    expect(rule.enabled).toBe(false);
    expect(rule.default_gateway).toBe("ipv4");
    expect(rule.dscp).toBe(46);
  });

  it("clears several optional fields while keeping a remaining list condition", async () => {
    const mock = ruleServer(
      configWithRule({
        enabled: true,
        outbound: "vpn",
        list: ["existing"],
        default_gateway: "ipv4",
        dscp: 46,
        src_port: "1024-65535",
        dest_port: "443",
        src_addr: "192.168.1.0/24",
        dest_addr: "10.0.0.0/8",
        proto: "tcp",
      }),
    );

    await updateRuleInDraft(clientFor(mock), {
      index: 0,
      defaultGateway: null,
      dscp: null,
      srcPort: null,
      destPort: null,
      srcAddr: null,
      destAddr: null,
      proto: null,
      save: false,
      saveWaitSeconds: 20,
    });

    expect(postedRule(mock)).toEqual({
      enabled: true,
      outbound: "vpn",
      list: ["existing"],
    });
  });

  it("clears the list condition with null when another condition remains", async () => {
    const mock = ruleServer(
      configWithRule({
        enabled: true,
        outbound: "vpn",
        list: ["existing"],
        default_gateway: "ipv6",
      }),
    );

    await updateRuleInDraft(clientFor(mock), {
      index: 0,
      lists: null,
      save: false,
      saveWaitSeconds: 20,
    });

    const rule = postedRule(mock);
    expect(rule).not.toHaveProperty("list");
    expect(rule.default_gateway).toBe("ipv6");
  });

  it("refuses to leave a rule without any match condition", async () => {
    const mock = ruleServer(
      configWithRule({
        enabled: true,
        outbound: "vpn",
        default_gateway: "ipv4",
      }),
    );

    await expect(
      updateRuleInDraft(clientFor(mock), {
        index: 0,
        defaultGateway: null,
        save: false,
        saveWaitSeconds: 20,
      }),
    ).rejects.toThrow(/without match conditions/);
    expect(mock.requestsFor("POST", "/api/config")).toHaveLength(0);
  });

  it("still rejects unknown outbounds and list names", async () => {
    const mock = ruleServer(
      configWithRule({ enabled: true, outbound: "vpn", default_gateway: "ipv4" }),
    );

    await expect(
      updateRuleInDraft(clientFor(mock), {
        index: 0,
        outbound: "nope",
        save: false,
        saveWaitSeconds: 20,
      }),
    ).rejects.toThrow(/Outbound "nope" does not exist/);

    await expect(
      updateRuleInDraft(clientFor(mock), {
        index: 0,
        lists: ["nope"],
        save: false,
        saveWaitSeconds: 20,
      }),
    ).rejects.toThrow(/List "nope" does not exist/);
  });
});
