import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import type { KeenPbrClient } from "../client.js";
import {
  assertListExists,
  assertOutboundExists,
  assertPortSpec,
  cloneConfig,
  commitDraftChange,
  summarizeRouteRule,
} from "../config-edit.js";
import { ToolInputError } from "../errors.js";
import type { RouteRule } from "../types.js";
import { Mutex } from "../mutex.js";
import { guarded, requireWrite } from "./common.js";

const protoSchema = z.enum(["tcp", "udp", "tcp/udp"]);
const gatewaySchema = z.enum(["ipv4", "ipv6"]);
const portSchema = z.string().min(1);

const saveArgs = {
  save: z
    .boolean()
    .default(false)
    .describe("Save and apply immediately. Default false: stage the draft and call save_config afterwards."),
  save_wait_seconds: z
    .number()
    .int()
    .min(0)
    .max(120)
    .default(20)
    .describe("When save=true, how long to wait for the async apply"),
};

export function registerRuleTools(
  server: McpServer,
  client: KeenPbrClient,
  options: { allowWrite: boolean },
  mutex: Mutex,
): void {
  server.registerTool(
    "create_rule",
    {
      title: "Create a route rule",
      description:
        "Appends (or prepends) a route rule to route.rules in the config draft. Typical use: route list Y via outbound Z. Rules are evaluated in order, so put catch-all default_gateway rules last. Outbound tags and list names are validated. Nothing is persisted until save_config, unless save=true.",
      inputSchema: {
        outbound: z.string().min(1).describe("Outbound tag that matched traffic is routed through"),
        lists: z
          .array(z.string().min(1))
          .min(1)
          .optional()
          .describe("List names this rule matches; required unless another match condition is given"),
        enabled: z.boolean().default(true).describe("Whether the rule is active"),
        proto: protoSchema.optional().describe("Transport protocol to match"),
        dscp: z.number().int().min(1).max(63).optional().describe("DSCP value to match"),
        src_port: portSchema.optional().describe('Source port spec: "1024", "80,443", "8000-9000", "!443"'),
        dest_port: portSchema.optional().describe('Destination port spec: "443", "80,443", "8000-9000", "!80"'),
        src_addr: z.string().min(1).optional().describe("Source CIDR(s), comma separated or !-negated"),
        dest_addr: z.string().min(1).optional().describe("Destination CIDR(s), comma separated or !-negated"),
        default_gateway: gatewaySchema
          .optional()
          .describe("Match traffic leaving through the default route of this IP family"),
        position: z
          .enum(["start", "end"])
          .default("end")
          .describe("Insert at the start or append at the end of route.rules"),
        ...saveArgs,
      },
      annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: false },
    },
    async (args) =>
      guarded(async () => {
        requireWrite(options.allowWrite);
        return mutex.run(async () => {
          const state = await client.getConfig();
          const before = state.config;
          const after = cloneConfig(before);

          assertOutboundExists(after, args.outbound);
          for (const list of args.lists ?? []) assertListExists(after, list);
          const hasCondition =
            (args.lists?.length ?? 0) > 0 ||
            Boolean(args.default_gateway) ||
            Boolean(args.src_addr) ||
            Boolean(args.dest_addr) ||
            Boolean(args.src_port) ||
            Boolean(args.dest_port) ||
            Boolean(args.proto) ||
            args.dscp !== undefined;
          if (!hasCondition) {
            throw new ToolInputError(
              "A route rule needs at least one match condition: lists, default_gateway, src_addr, dest_addr, src_port, dest_port, proto or dscp.",
            );
          }
          if (args.src_port) assertPortSpec(args.src_port);
          if (args.dest_port) assertPortSpec(args.dest_port);

          const rule: RouteRule = { enabled: args.enabled, outbound: args.outbound };
          if (args.lists?.length) rule.list = args.lists;
          if (args.proto) rule.proto = args.proto;
          if (args.dscp !== undefined) rule.dscp = args.dscp;
          if (args.src_port) rule.src_port = args.src_port;
          if (args.dest_port) rule.dest_port = args.dest_port;
          if (args.src_addr) rule.src_addr = args.src_addr;
          if (args.dest_addr) rule.dest_addr = args.dest_addr;
          if (args.default_gateway) rule.default_gateway = args.default_gateway;

          after.route ??= {};
          after.route.rules ??= [];
          const index = args.position === "start" ? 0 : after.route.rules.length;
          if (args.position === "start") {
            after.route.rules.unshift(rule);
          } else {
            after.route.rules.push(rule);
          }

          return commitDraftChange(client, {
            before,
            after,
            changeSummary: {
              created_rule_index: index,
              rule,
              summary: summarizeRouteRule(rule),
            },
            save: args.save,
            saveWaitSeconds: args.save_wait_seconds,
          });
        });
      }),
  );

  server.registerTool(
    "update_rule",
    {
      title: "Update a route rule",
      description:
        "Updates fields of an existing route rule in the config draft by 0-based index (see list_rules). Only supplied fields are changed; outbound tags and list names are validated. Nothing is persisted until save_config, unless save=true.",
      inputSchema: {
        index: z.number().int().min(0).describe("0-based rule index from list_rules"),
        outbound: z.string().min(1).optional().describe("New outbound tag"),
        lists: z.array(z.string().min(1)).min(1).optional().describe("New list names"),
        enabled: z.boolean().optional().describe("Enable or disable the rule"),
        proto: protoSchema.optional().describe("New transport protocol match"),
        dscp: z.number().int().min(1).max(63).optional().describe("New DSCP value match"),
        src_port: portSchema.optional().describe("New source port specification"),
        dest_port: portSchema.optional().describe("New destination port specification"),
        src_addr: z.string().min(1).optional().describe("New source CIDR(s)"),
        dest_addr: z.string().min(1).optional().describe("New destination CIDR(s)"),
        default_gateway: gatewaySchema.optional().describe("New default-gateway match"),
        ...saveArgs,
      },
      annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: true },
    },
    async (args) =>
      guarded(async () => {
        requireWrite(options.allowWrite);
        return mutex.run(async () => {
          const state = await client.getConfig();
          const before = state.config;
          const after = cloneConfig(before);
          const rules = after.route?.rules;
          if (!rules || args.index >= rules.length) {
            throw new ToolInputError(
              `Rule index ${args.index} does not exist; there are ${rules?.length ?? 0} route rule(s). Use list_rules.`,
            );
          }
          const rule = rules[args.index];
          if (!rule) throw new ToolInputError(`Rule index ${args.index} does not exist.`);

          const hasUpdate =
            args.outbound !== undefined ||
            args.lists !== undefined ||
            args.enabled !== undefined ||
            args.proto !== undefined ||
            args.dscp !== undefined ||
            args.src_port !== undefined ||
            args.dest_port !== undefined ||
            args.src_addr !== undefined ||
            args.dest_addr !== undefined ||
            args.default_gateway !== undefined;
          if (!hasUpdate) throw new ToolInputError("Provide at least one field to update.");

          if (args.outbound) assertOutboundExists(after, args.outbound);
          for (const list of args.lists ?? []) assertListExists(after, list);
          if (args.src_port) assertPortSpec(args.src_port);
          if (args.dest_port) assertPortSpec(args.dest_port);

          const beforeSummary = summarizeRouteRule(rule);
          if (args.outbound) rule.outbound = args.outbound;
          if (args.lists) rule.list = args.lists;
          if (args.enabled !== undefined) rule.enabled = args.enabled;
          if (args.proto) rule.proto = args.proto;
          if (args.dscp !== undefined) rule.dscp = args.dscp;
          if (args.src_port) rule.src_port = args.src_port;
          if (args.dest_port) rule.dest_port = args.dest_port;
          if (args.src_addr) rule.src_addr = args.src_addr;
          if (args.dest_addr) rule.dest_addr = args.dest_addr;
          if (args.default_gateway) rule.default_gateway = args.default_gateway;

          return commitDraftChange(client, {
            before,
            after,
            changeSummary: {
              rule_index: args.index,
              before: beforeSummary,
              after: summarizeRouteRule(rule),
              rule,
            },
            save: args.save,
            saveWaitSeconds: args.save_wait_seconds,
          });
        });
      }),
  );

  server.registerTool(
    "delete_rule",
    {
      title: "Delete a route rule",
      description:
        "Removes a route rule from the config draft by 0-based index (see list_rules). Later rule indexes shift down by one. Lists and outbounds are left untouched. Nothing is persisted until save_config, unless save=true.",
      inputSchema: {
        index: z.number().int().min(0).describe("0-based rule index from list_rules"),
        ...saveArgs,
      },
      annotations: { readOnlyHint: false, destructiveHint: true, idempotentHint: false },
    },
    async (args) =>
      guarded(async () => {
        requireWrite(options.allowWrite);
        return mutex.run(async () => {
          const state = await client.getConfig();
          const before = state.config;
          const after = cloneConfig(before);
          const rules = after.route?.rules;
          if (!rules || args.index >= rules.length) {
            throw new ToolInputError(
              `Rule index ${args.index} does not exist; there are ${rules?.length ?? 0} route rule(s). Use list_rules.`,
            );
          }
          const [removed] = rules.splice(args.index, 1);
          if (!removed) throw new ToolInputError(`Rule index ${args.index} does not exist.`);

          return commitDraftChange(client, {
            before,
            after,
            changeSummary: {
              deleted_rule_index: args.index,
              removed,
              summary: summarizeRouteRule(removed),
            },
            save: args.save,
            saveWaitSeconds: args.save_wait_seconds,
          });
        });
      }),
  );
}
