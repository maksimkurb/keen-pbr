import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import type { KeenPbrClient } from "../client.js";
import { collectDiagnosis } from "../diagnose.js";
import { summarizeMetrics } from "../metrics.js";
import { guarded, summarizeRoutingTest } from "./common.js";

export function registerDiagnosticsTools(server: McpServer, client: KeenPbrClient): void {
  server.registerTool(
    "diagnose",
    {
      title: "Diagnose keen-pbr",
      description:
        "One-call health check. Reads service health, routing health, classified issues, runtime outbounds and interfaces, then returns a compact prioritised verdict: what is failing, which outbound/interface/list is affected, warning codes with human titles, and a suggested next step. Start here when routing is broken.",
      inputSchema: {},
      annotations: { readOnlyHint: true },
    },
    async () => guarded(() => collectDiagnosis(client)),
  );

  server.registerTool(
    "get_health",
    {
      title: "Service health",
      description:
        "Raw service health from GET /api/health/service: daemon version, OS, runtime state and reason, draft/rollback flags, lifecycle operation, interception and dnsmasq state. Use diagnose first for a prioritised summary.",
      inputSchema: {},
      annotations: { readOnlyHint: true },
    },
    async () => guarded(() => client.getServiceHealth()),
  );

  server.registerTool(
    "get_runtime_outbounds",
    {
      title: "Runtime outbounds",
      description:
        "Live outbound and candidate-interface state from GET /api/runtime/outbounds: which outbound is healthy, degraded or unavailable, its active/backup members, latencies and mismatch details.",
      inputSchema: {},
      annotations: { readOnlyHint: true },
    },
    async () => guarded(() => client.getRuntimeOutbounds()),
  );

  server.registerTool(
    "get_runtime_interfaces",
    {
      title: "Runtime interfaces",
      description:
        "System interface inventory from GET /api/runtime/interfaces: up/down state, carrier, operstate, IPv4/IPv6 addresses and Keenetic descriptions. Use it to check whether an outbound's interface exists and is up.",
      inputSchema: {},
      annotations: { readOnlyHint: true },
    },
    async () => guarded(() => client.getRuntimeInterfaces()),
  );

  server.registerTool(
    "get_command_failure",
    {
      title: "Last failed commands",
      description:
        "Most recent privileged-command failures from GET /api/diagnostics/command-failure. Returns a message when the daemon has no failure log. Useful after routing checks fail or a config apply reports an error.",
      inputSchema: {},
      annotations: { readOnlyHint: true },
    },
    async () =>
      guarded(async () => {
        const text = await client.getCommandFailure();
        return text === null ? "No command failure log exists on the router." : text;
      }),
  );

  server.registerTool(
    "routing_test",
    {
      title: "Test routing for a host",
      description:
        "Ask the daemon where a domain or IP actually goes: POST /api/routing/test returns expected outbound (from config and cached lists), actual outbound (from live kernel sets), the matched list entry and rule index. Set include_rule_diagnostics to get the full rule-by-rule matrix.",
      inputSchema: {
        target: z.string().min(1).describe("Domain name or IP address to test, for example youtube.com or 1.1.1.1"),
        proto: z.enum(["tcp", "udp", "other"]).optional().describe("Optional packet protocol used to evaluate rules"),
        dest_port: z.number().int().min(1).max(65535).optional().describe("Optional destination port"),
        src_addr: z.string().min(1).optional().describe("Optional source IP address"),
        src_port: z.number().int().min(1).max(65535).optional().describe("Optional source port"),
        dscp: z.number().int().min(0).max(63).optional().describe("Optional DSCP value"),
        include_rule_diagnostics: z
          .boolean()
          .default(false)
          .describe("Include the full per-rule diagnostics matrix (larger response)"),
      },
      annotations: { readOnlyHint: true },
    },
    async (args) =>
      guarded(async () => {
        const response = await client.routingTest({
          target: args.target,
          ...(args.proto ? { proto: args.proto } : {}),
          ...(args.dest_port === undefined ? {} : { dest_port: args.dest_port }),
          ...(args.src_addr ? { src_addr: args.src_addr } : {}),
          ...(args.src_port === undefined ? {} : { src_port: args.src_port }),
          ...(args.dscp === undefined ? {} : { dscp: args.dscp }),
        });
        const result = summarizeRoutingTest(response);
        if (!args.include_rule_diagnostics) delete result.rule_diagnostics;
        return result;
      }),
  );

  server.registerTool(
    "get_metrics",
    {
      title: "Summarised Prometheus metrics",
      description:
        "Fetches /metrics and returns a summary instead of the raw dump: family/sample counts, build info, active rules, reload errors, and non-zero error counters or down probes. Use filter to inspect families whose name contains a substring (for example 'dns_proxy_upstream' or 'list'); matching samples are capped by max_samples.",
      inputSchema: {
        filter: z
          .string()
          .min(1)
          .optional()
          .describe("Case-insensitive substring of metric family names to include"),
        max_samples: z
          .number()
          .int()
          .min(1)
          .max(2000)
          .default(200)
          .describe("Maximum number of matching samples to return when filter is set"),
      },
      annotations: { readOnlyHint: true },
    },
    async (args) =>
      guarded(async () => {
        const text = await client.getMetrics();
        return summarizeMetrics(text, {
          ...(args.filter ? { filter: args.filter } : {}),
          maxSamples: args.max_samples,
        });
      }),
  );
}
