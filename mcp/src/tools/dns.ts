import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import type { KeenPbrClient } from "../client.js";
import type { DnsCheckResponse } from "../types.js";
import { guarded } from "./common.js";

export function registerDnsTools(server: McpServer, client: KeenPbrClient): void {
  server.registerTool(
    "dns_test",
    {
      title: "Observe live DNS interception",
      description:
        "Collects events from the daemon's DNS test stream (GET /api/dns/test) for a bounded time window. Shows DNS/TLS-SNI/HTTP-Host/QUIC events and whether a domain matched a configured list, including list names, resolved IPs, set-add counts and hold time. An empty result means nothing matched in that window; test the domain from a LAN client while this runs. show=all requires API authentication.",
      inputSchema: {
        show: z
          .enum(["keen-pbr", "all", "full"])
          .default("keen-pbr")
          .describe("keen-pbr: marker events only; all (alias full): every observed event"),
        domain: z
          .string()
          .min(1)
          .optional()
          .describe("Optional marker domain to observe for the default view"),
        duration_ms: z
          .number()
          .int()
          .min(500)
          .max(30_000)
          .default(5_000)
          .describe("How long to listen, in milliseconds"),
        max_events: z.number().int().min(1).max(2_000).default(200).describe("Stop after this many events"),
      },
      annotations: { readOnlyHint: true },
    },
    async (args) =>
      guarded(async () => {
        const result = await client.collectDnsEvents({
          durationMs: args.duration_ms,
          maxEvents: args.max_events,
          show: args.show,
          ...(args.domain ? { domain: args.domain } : {}),
        });
        return {
          show: args.show,
          duration_ms: args.duration_ms,
          event_count: result.events.length,
          truncated: result.truncated,
          ...(result.events.length === 0
            ? { note: "No events in this window. Generate traffic from a LAN client while dns_test is running." }
            : {}),
          events: result.events,
        };
      }),
  );

  server.registerTool(
    "dns_check_servers",
    {
      title: "Check DNS servers",
      description:
        "Probes every DNS server of the applied config (not the staged draft) for known domains and reports working servers, servers that do not answer, and servers whose answers look tampered with (block-page addresses, false NXDOMAIN, made-up names resolving). Use it when DNS-based lists are empty or domains resolve to wrong addresses.",
      inputSchema: {
        domains: z
          .array(z.string().min(1))
          .max(5)
          .optional()
          .describe("Domains that certainly exist; an NXDOMAIN for them is suspicious"),
        custom_domains: z
          .array(z.string().min(1))
          .max(5)
          .optional()
          .describe("Your own domains; missing records are not warned about"),
        servers: z.array(z.string().min(1)).optional().describe("Server tags to check; default all dns.servers"),
      },
      annotations: { readOnlyHint: true },
    },
    async (args) =>
      guarded(async () => {
        const response = await client.dnsCheckServers({
          ...(args.domains ? { domains: args.domains } : {}),
          ...(args.custom_domains ? { custom_domains: args.custom_domains } : {}),
          ...(args.servers ? { servers: args.servers } : {}),
        });
        return summarizeDnsCheck(response);
      }),
  );
}

function summarizeDnsCheck(response: DnsCheckResponse): Record<string, unknown> {
  let checked = 0;
  let failing = 0;
  let suspicious = 0;
  const servers = response.servers.map((server) => {
    const endpoints = server.endpoints.map((endpoint) => {
      const results = endpoint.results ?? [];
      checked += results.length;
      const problems = results.filter((result) => result.severity !== "ok");
      failing += results.filter((result) => result.severity === "error").length;
      suspicious += results.filter((result) => result.severity === "warning").length;
      return {
        address: endpoint.address,
        status: endpoint.status,
        ...(endpoint.reason ? { reason: endpoint.reason } : {}),
        checked: results.length,
        problems,
      };
    });
    return {
      tag: server.tag,
      type: server.type,
      ...(server.detour ? { detour: server.detour } : {}),
      ...(server.error ? { error: server.error } : {}),
      endpoints,
    };
  });

  return {
    overall: response.overall,
    summary: { servers: response.servers.length, queries: checked, errors: failing, warnings: suspicious },
    next_step:
      response.overall === "ok"
        ? "All checked DNS servers answered normally."
        : response.overall === "warning"
          ? "At least one DNS answer looks tampered with. Consider routing that server through a VPN detour or replacing it."
          : "At least one DNS server did not answer. Check the server address, its detour outbound and dnsmasq connectivity.",
    servers,
  };
}
