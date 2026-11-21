import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { InMemoryTransport } from "@modelcontextprotocol/sdk/inMemory.js";
import { afterEach, describe, expect, it } from "vitest";
import { KeenPbrClient } from "../src/client.js";
import { createServer } from "../src/server.js";
import type { DraftConfig } from "../src/types.js";
import {
  baseConfig,
  configState,
  healthyHealth,
  healthyInterfaces,
  healthyIssues,
  healthyOutbounds,
  healthyRouting,
} from "./fixtures.js";
import { MockHttp } from "./mock-http.js";

let closeServer: (() => Promise<void>) | undefined;

afterEach(async () => {
  await closeServer?.();
  closeServer = undefined;
});

async function connect(allowWrite: boolean, config: DraftConfig = baseConfig()): Promise<Client> {
  const mock = new MockHttp()
    .onJson("GET", "/api/health/service", 200, healthyHealth())
    .onJson("GET", "/api/health/routing", 200, healthyRouting())
    .onJson("GET", "/api/health/issues", 200, healthyIssues())
    .onJson("GET", "/api/runtime/outbounds", 200, healthyOutbounds())
    .onJson("GET", "/api/runtime/interfaces", 200, healthyInterfaces())
    .onJson("GET", "/api/config", 200, configState(config))
    .onJson("POST", "/api/config", 200, { status: "ok", message: "Config staged in memory" });
  const pbr = new KeenPbrClient(
    { baseUrl: "http://router.test:12121", token: "test-token", timeoutMs: 5_000 },
    mock.fetch,
  );
  const server = createServer({ client: pbr, allowWrite });
  const [clientTransport, serverTransport] = InMemoryTransport.createLinkedPair();
  await server.connect(serverTransport);
  const client = new Client({ name: "test-client", version: "1.0.0" });
  await client.connect(clientTransport);
  closeServer = async () => {
    await client.close();
    await server.close();
  };
  return client;
}

describe("MCP server registration", () => {
  it("exposes the expected read and write tools", async () => {
    const client = await connect(false);
    const { tools } = await client.listTools();
    const names = tools.map((tool) => tool.name).sort();
    expect(names).toEqual([
      "add_domain_to_list",
      "create_list",
      "create_rule",
      "delete_rule",
      "diagnose",
      "discard_config",
      "dns_check_servers",
      "dns_test",
      "get_command_failure",
      "get_config",
      "get_health",
      "get_metrics",
      "get_runtime_interfaces",
      "get_runtime_outbounds",
      "list_rules",
      "refresh_lists",
      "remove_domain_from_list",
      "rollback_config",
      "routing_test",
      "save_config",
      "update_rule",
    ]);

    const readOnly = tools.find((tool) => tool.name === "diagnose");
    expect(readOnly?.annotations?.readOnlyHint).toBe(true);
    const write = tools.find((tool) => tool.name === "add_domain_to_list");
    expect(write?.annotations?.readOnlyHint).toBe(false);
  });

  it("returns a structured diagnosis over the MCP protocol", async () => {
    const client = await connect(false);
    const result = await client.callTool({ name: "diagnose", arguments: {} });
    expect(result.isError).toBeFalsy();
    const content = result.content as Array<{ type: string; text?: string }>;
    expect(content[0]?.type).toBe("text");
    const diagnosis = JSON.parse(content[0]?.text ?? "{}") as { verdict: string };
    expect(diagnosis.verdict).toBe("healthy");
  });

  it("blocks mutating tools without KEEN_PBR_ALLOW_WRITE=1", async () => {
    const client = await connect(false);
    const result = await client.callTool({
      name: "add_domain_to_list",
      arguments: { list: "existing", domains: ["example.com"] },
    });
    expect(result.isError).toBe(true);
    const content = result.content as Array<{ type: string; text?: string }>;
    expect(content[0]?.text).toContain("KEEN_PBR_ALLOW_WRITE=1");
  });

  it("accepts null for optional update_rule fields through the MCP schema", async () => {
    const config = baseConfig();
    config.route = {
      rules: [{ enabled: true, outbound: "vpn", list: ["existing"], default_gateway: "ipv4" }],
    };
    const client = await connect(true, config);

    const result = await client.callTool({
      name: "update_rule",
      arguments: { index: 0, default_gateway: null },
    });
    expect(result.isError, JSON.stringify(result.content)).toBeFalsy();
    const content = result.content as Array<{ type: string; text?: string }>;
    const update = JSON.parse(content[0]?.text ?? "{}") as {
      change_summary?: { before?: string; after?: string };
      changes?: Array<{ path: string; before?: unknown; after?: unknown }>;
    };
    expect(update.change_summary?.before).toContain("default gateway ipv4");
    expect(update.change_summary?.after).not.toContain("default gateway");
    expect(update.changes).toEqual(
      expect.arrayContaining([{ path: "route.rules[0].default_gateway", before: "ipv4" }]),
    );
  });

  it("does not expose secrets or forbidden service/auth endpoints", async () => {
    const client = await connect(false);
    const { tools } = await client.listTools();
    const names = tools.map((tool) => tool.name);
    for (const forbidden of ["auth", "service_start", "service_stop", "service_restart", "status_events"]) {
      expect(names.some((name) => name.includes(forbidden))).toBe(false);
    }
    expect(JSON.stringify(tools)).not.toContain("test-token");
  });
});
