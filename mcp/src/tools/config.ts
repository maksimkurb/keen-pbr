import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import type { KeenPbrClient } from "../client.js";
import { countInlineDomains, performSave, summarizeRouteRule } from "../config-edit.js";
import type { DraftConfig } from "../types.js";
import { guarded, requireWrite } from "./common.js";

const SECTIONS = [
  "daemon",
  "outbounds",
  "lists",
  "dns",
  "route",
  "fwmark",
  "iproute",
  "lists_autoupdate",
  "intercept",
] as const;

export function registerConfigTools(
  server: McpServer,
  client: KeenPbrClient,
  options: { allowWrite: boolean },
): void {
  server.registerTool(
    "get_config",
    {
      title: "Read config",
      description:
        "Reads the editable config from GET /api/config. Without a section it returns the whole document plus is_draft. With a section it returns only that part. Edits are draft-then-save: this reflects the staged draft when one exists.",
      inputSchema: {
        section: z
          .enum(SECTIONS)
          .optional()
          .describe("Optional config section to return instead of the whole document"),
      },
      annotations: { readOnlyHint: true },
    },
    async (args) =>
      guarded(async () => {
        const state = await client.getConfig();
        if (args.section) {
          return {
            is_draft: state.is_draft,
            section: args.section,
            value: state.config[args.section] ?? null,
            ...(state.list_refresh_state ? { list_refresh_state: state.list_refresh_state } : {}),
          };
        }
        return {
          is_draft: state.is_draft,
          config: state.config,
          ...(state.list_refresh_state ? { list_refresh_state: state.list_refresh_state } : {}),
        };
      }),
  );

  server.registerTool(
    "list_rules",
    {
      title: "List routing rules and lists",
      description:
        "Compact, indexed view of route rules, DNS rules, lists and outbound tags. Rule indexes are 0-based and are what update_rule and delete_rule expect. Use this before creating or editing a rule.",
      inputSchema: {},
      annotations: { readOnlyHint: true },
    },
    async () =>
      guarded(async () => {
        const state = await client.getConfig();
        return summarizeRules(state.config, state.is_draft);
      }),
  );

  server.registerTool(
    "save_config",
    {
      title: "Save and apply config",
      description:
        "Persists the staged draft to disk and applies it to the running routing runtime (POST /api/config/save). This is the only step that makes earlier list/rule edits take effect. Waits for the apply lifecycle by default and reports the final status.",
      inputSchema: {
        wait_seconds: z
          .number()
          .int()
          .min(0)
          .max(120)
          .default(20)
          .describe("How long to wait for the async apply to finish; 0 returns immediately"),
      },
      annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: false },
    },
    async (args) =>
      guarded(async () => {
        requireWrite(options.allowWrite);
        return performSave(client, args.wait_seconds);
      }),
  );

  server.registerTool(
    "discard_config",
    {
      title: "Discard config draft",
      description:
        "Throws away the staged in-memory draft and restores the last saved config (POST /api/config/discard). The saved file and running routing are not changed. Fails when there is no draft.",
      inputSchema: {},
      annotations: { readOnlyHint: false, destructiveHint: true, idempotentHint: true },
    },
    async () =>
      guarded(async () => {
        requireWrite(options.allowWrite);
        const response = await client.discardConfig();
        return { ...response, message: response.message ?? "Staged config discarded" };
      }),
  );

  server.registerTool(
    "rollback_config",
    {
      title: "Roll back failed apply",
      description:
        "Restores the previous config inode retained by the daemon after a failed apply (POST /api/config/rollback). Only available when get_health reports rollback_available=true. The rollback itself is asynchronous.",
      inputSchema: {},
      annotations: { readOnlyHint: false, destructiveHint: true, idempotentHint: false },
    },
    async () =>
      guarded(async () => {
        requireWrite(options.allowWrite);
        const response = await client.rollbackConfig();
        return {
          ...response,
          next_step: "Poll get_health and check lifecycle_operation until it is no longer running.",
        };
      }),
  );

  server.registerTool(
    "refresh_lists",
    {
      title: "Refresh URL lists",
      description:
        "Re-downloads one URL-backed list, or every URL-backed list when name is omitted, from the applied config (POST /api/lists/refresh). When contents change and the runtime is active it is rebuilt automatically. Fails while a config draft exists, so save or discard the draft first.",
      inputSchema: {
        name: z.string().min(1).optional().describe("List name; omit to refresh all URL-backed lists"),
      },
      annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: true },
    },
    async (args) =>
      guarded(async () => {
        requireWrite(options.allowWrite);
        return client.refreshLists(args.name);
      }),
  );
}

export function summarizeRules(config: DraftConfig, isDraft: boolean): Record<string, unknown> {
  const rules = config.route?.rules ?? [];
  const dnsRules = config.dns?.rules ?? [];
  return {
    is_draft: isDraft,
    route: {
      inbound_interfaces: config.route?.inbound_interfaces ?? [],
      rule_count: rules.length,
      rules: rules.map((rule, index) => ({
        index,
        enabled: rule.enabled !== false,
        outbound: rule.outbound,
        lists: rule.list ?? [],
        match: {
          ...(rule.proto ? { proto: rule.proto } : {}),
          ...(rule.dscp === undefined ? {} : { dscp: rule.dscp }),
          ...(rule.src_port ? { src_port: rule.src_port } : {}),
          ...(rule.dest_port ? { dest_port: rule.dest_port } : {}),
          ...(rule.src_addr ? { src_addr: rule.src_addr } : {}),
          ...(rule.dest_addr ? { dest_addr: rule.dest_addr } : {}),
          ...(rule.default_gateway ? { default_gateway: rule.default_gateway } : {}),
        },
        summary: summarizeRouteRule(rule),
      })),
    },
    dns: {
      rule_count: dnsRules.length,
      rules: dnsRules.map((rule, index) => ({ index, ...rule })),
    },
    lists: Object.entries(config.lists ?? {})
      .sort(([left], [right]) => left.localeCompare(right))
      .map(([name, list]) => ({
        name,
        domains: countInlineDomains(list),
        ip_cidrs: list.ip_cidrs?.length ?? 0,
        ...(list.url ? { url: list.url } : {}),
        ...(list.file ? { file: list.file } : {}),
        ...(list.detour ? { detour: list.detour } : {}),
      })),
    outbounds: (config.outbounds ?? []).map((outbound) => ({
      tag: outbound.tag,
      type: outbound.type,
      ...(outbound.interface ? { interface: outbound.interface } : {}),
      ...(outbound.table === undefined ? {} : { table: outbound.table }),
    })),
  };
}
