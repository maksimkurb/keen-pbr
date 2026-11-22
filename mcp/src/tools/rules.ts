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
  type DraftChangeResult,
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

export interface UpdateRuleArgs {
  index: number;
  outbound?: string;
  lists?: string[] | null;
  enabled?: boolean | null;
  proto?: "tcp" | "udp" | "tcp/udp" | null;
  dscp?: number | null;
  srcPort?: string | null;
  destPort?: string | null;
  srcAddr?: string | null;
  destAddr?: string | null;
  defaultGateway?: "ipv4" | "ipv6" | null;
  save: boolean;
  saveWaitSeconds: number;
}

export function hasRuleCondition(rule: RouteRule): boolean {
  return (
    (rule.list?.length ?? 0) > 0 ||
    Boolean(rule.proto) ||
    rule.dscp !== undefined ||
    Boolean(rule.src_port) ||
    Boolean(rule.dest_port) ||
    Boolean(rule.src_addr) ||
    Boolean(rule.dest_addr) ||
    Boolean(rule.default_gateway)
  );
}

/** Read-modify-write the config draft. Exported for unit tests. */
export async function updateRuleInDraft(
  client: KeenPbrClient,
  args: UpdateRuleArgs,
): Promise<DraftChangeResult> {
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
    args.srcPort !== undefined ||
    args.destPort !== undefined ||
    args.srcAddr !== undefined ||
    args.destAddr !== undefined ||
    args.defaultGateway !== undefined;
  if (!hasUpdate) throw new ToolInputError("Provide at least one field to update.");

  if (args.outbound) assertOutboundExists(after, args.outbound);
  if (args.lists) {
    for (const list of args.lists) assertListExists(after, list);
  }
  if (typeof args.srcPort === "string") assertPortSpec(args.srcPort);
  if (typeof args.destPort === "string") assertPortSpec(args.destPort);

  const beforeSummary = summarizeRouteRule(rule);
  if (args.outbound) rule.outbound = args.outbound;
  if (args.lists !== undefined) {
    if (args.lists === null) delete rule.list;
    else rule.list = args.lists;
  }
  if (args.enabled !== undefined) {
    if (args.enabled === null) delete rule.enabled;
    else rule.enabled = args.enabled;
  }
  if (args.proto !== undefined) {
    if (args.proto === null) delete rule.proto;
    else rule.proto = args.proto;
  }
  if (args.dscp !== undefined) {
    if (args.dscp === null) delete rule.dscp;
    else rule.dscp = args.dscp;
  }
  if (args.srcPort !== undefined) {
    if (args.srcPort === null) delete rule.src_port;
    else rule.src_port = args.srcPort;
  }
  if (args.destPort !== undefined) {
    if (args.destPort === null) delete rule.dest_port;
    else rule.dest_port = args.destPort;
  }
  if (args.srcAddr !== undefined) {
    if (args.srcAddr === null) delete rule.src_addr;
    else rule.src_addr = args.srcAddr;
  }
  if (args.destAddr !== undefined) {
    if (args.destAddr === null) delete rule.dest_addr;
    else rule.dest_addr = args.destAddr;
  }
  if (args.defaultGateway !== undefined) {
    if (args.defaultGateway === null) delete rule.default_gateway;
    else rule.default_gateway = args.defaultGateway;
  }

  if (!hasRuleCondition(rule)) {
    throw new ToolInputError(
      "This change would leave the rule without match conditions. Keep at least one of lists, default_gateway, src_addr, dest_addr, src_port, dest_port, proto or dscp.",
    );
  }

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
    saveWaitSeconds: args.saveWaitSeconds,
  });
}

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

          if (!hasRuleCondition(rule)) {
            throw new ToolInputError(
              "A route rule needs at least one match condition: lists, default_gateway, src_addr, dest_addr, src_port, dest_port, proto or dscp.",
            );
          }

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
        "Updates fields of an existing route rule in the config draft by 0-based index (see list_rules). Only supplied fields are changed; pass null for an optional match field to remove it from the rule. Outbound tags, list names and the remaining match conditions are validated. Nothing is persisted until save_config, unless save=true.",
      inputSchema: {
        index: z.number().int().min(0).describe("0-based rule index from list_rules"),
        outbound: z.string().min(1).optional().describe("New outbound tag"),
        lists: z
          .array(z.string().min(1))
          .min(1)
          .nullable()
          .optional()
          .describe("New list names; null removes the list condition"),
        enabled: z.boolean().nullable().optional().describe("Enable or disable the rule; null restores the active default"),
        proto: protoSchema.nullable().optional().describe("New protocol match; null removes it"),
        dscp: z.number().int().min(1).max(63).nullable().optional().describe("New DSCP match; null removes it"),
        src_port: portSchema.nullable().optional().describe("New source port specification; null removes it"),
        dest_port: portSchema.nullable().optional().describe("New destination port specification; null removes it"),
        src_addr: z.string().min(1).nullable().optional().describe("New source CIDR(s); null removes it"),
        dest_addr: z.string().min(1).nullable().optional().describe("New destination CIDR(s); null removes it"),
        default_gateway: gatewaySchema
          .nullable()
          .optional()
          .describe("New default-gateway match; null removes it"),
        ...saveArgs,
      },
      annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: true },
    },
    async (args) =>
      guarded(async () => {
        requireWrite(options.allowWrite);
        return mutex.run(() =>
          updateRuleInDraft(client, {
            index: args.index,
            ...(args.outbound === undefined ? {} : { outbound: args.outbound }),
            ...(args.lists === undefined ? {} : { lists: args.lists }),
            ...(args.enabled === undefined ? {} : { enabled: args.enabled }),
            ...(args.proto === undefined ? {} : { proto: args.proto }),
            ...(args.dscp === undefined ? {} : { dscp: args.dscp }),
            ...(args.src_port === undefined ? {} : { srcPort: args.src_port }),
            ...(args.dest_port === undefined ? {} : { destPort: args.dest_port }),
            ...(args.src_addr === undefined ? {} : { srcAddr: args.src_addr }),
            ...(args.dest_addr === undefined ? {} : { destAddr: args.dest_addr }),
            ...(args.default_gateway === undefined ? {} : { defaultGateway: args.default_gateway }),
            save: args.save,
            saveWaitSeconds: args.save_wait_seconds,
          }),
        );
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
