import { isIP } from "node:net";
import type { KeenPbrClient } from "./client.js";
import { ToolInputError } from "./errors.js";
import type { DraftConfig, ListConfig, Outbound, RouteRule } from "./types.js";

export interface ConfigChange {
  path: string;
  before?: unknown;
  after?: unknown;
}

export interface ConfigDiff {
  changes: ConfigChange[];
  truncated: boolean;
}

export interface DraftChangeResult {
  changed: boolean;
  is_draft?: boolean;
  change_summary?: Record<string, unknown>;
  changes?: ConfigChange[];
  changes_truncated?: boolean;
  message?: string;
  next_step?: string;
  saved?: Record<string, unknown>;
}

const LIST_NAME_RE = /^[a-z][a-z0-9_]*$/;
const DOMAIN_RE =
  /^(?=.{1,253}$)[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?(?:\.[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?)*$/;

export function assertListName(name: string): string {
  const value = name.trim();
  if (!LIST_NAME_RE.test(value) || value.length > 24) {
    throw new ToolInputError(
      "List names must match ^[a-z][a-z0-9_]*$ and be at most 24 characters (lowercase ASCII, digits, underscore).",
    );
  }
  return value;
}

export function normalizeDomain(input: string): string {
  let value = input.trim().toLowerCase();
  if (value.endsWith(".")) value = value.slice(0, -1);
  if (value.length === 0) throw new ToolInputError("A domain must not be empty");

  if (isIP(value) !== 0) {
    throw new ToolInputError(
      `"${input}" is an IP address, not a domain. Use create_list with ip_cidrs for addresses.`,
    );
  }

  const wildcard = value.startsWith("*.");
  const base = wildcard ? value.slice(2) : value;
  if (!DOMAIN_RE.test(base)) {
    throw new ToolInputError(
      `"${input}" is not a valid domain. Use a name such as example.com or a wildcard such as *.example.com.`,
    );
  }
  return wildcard ? `*.${base}` : base;
}

/** Wildcards are normalized to their base domain by the daemon. */
export function domainKey(domain: string): string {
  return domain.trim().toLowerCase().replace(/^\.+/, "").replace(/^\*\./, "").replace(/\.$/, "");
}

export function isValidIpOrCidr(input: string): boolean {
  const value = input.trim();
  if (!value) return false;
  const slash = value.indexOf("/");
  if (slash === -1) return isIP(value) !== 0;
  if (value.indexOf("/", slash + 1) !== -1) return false;
  const address = value.slice(0, slash);
  const prefix = value.slice(slash + 1);
  if (!/^\d{1,3}$/.test(prefix)) return false;
  const family = isIP(address);
  if (family === 0) return false;
  const maximum = family === 4 ? 32 : 128;
  const length = Number(prefix);
  return length >= 0 && length <= maximum;
}

export function assertPortSpec(input: string): string {
  const value = input.trim();
  if (!/^!?\d+(?:-\d+)?(?:,\d+(?:-\d+)?)*$/.test(value)) {
    throw new ToolInputError(
      `"${input}" is not a valid port specification. Use 443, 80,443, 8000-9000 or !443.`,
    );
  }
  const body = value.startsWith("!") ? value.slice(1) : value;
  for (const part of body.split(",")) {
    const [first, last] = part.split("-");
    const from = Number(first);
    const to = last === undefined ? from : Number(last);
    if (from < 1 || from > 65535 || to < 1 || to > 65535 || from > to) {
      throw new ToolInputError(`Port range "${part}" is outside 1-65535 or reversed.`);
    }
  }
  return value;
}

export function outboundTags(config: DraftConfig): string[] {
  return (config.outbounds ?? []).map((outbound: Outbound) => outbound.tag);
}

export function assertOutboundExists(config: DraftConfig, tag: string): void {
  const tags = outboundTags(config);
  if (!tags.includes(tag)) {
    throw new ToolInputError(
      `Outbound "${tag}" does not exist. Available outbounds: ${tags.length > 0 ? tags.join(", ") : "(none)"}.`,
    );
  }
}

export function assertListExists(config: DraftConfig, name: string): ListConfig {
  const list = config.lists?.[name];
  if (!list) {
    throw new ToolInputError(
      `List "${name}" does not exist. Available lists: ${
        config.lists && Object.keys(config.lists).length > 0 ? Object.keys(config.lists).join(", ") : "(none)"
      }.`,
    );
  }
  return list;
}

export function cloneConfig(config: DraftConfig): DraftConfig {
  return structuredClone(config);
}

export function countInlineDomains(list: ListConfig | undefined): number {
  return list?.domains?.length ?? 0;
}

export function diffConfigs(before: unknown, after: unknown, limit = 100): ConfigDiff {
  const changes: ConfigChange[] = [];
  let truncated = false;

  const visit = (previous: unknown, next: unknown, path: string): void => {
    if (truncated) return;
    if (Object.is(previous, next)) return;

    if (isPlainObject(previous) && isPlainObject(next)) {
      const keys = new Set([...Object.keys(previous), ...Object.keys(next)]);
      for (const key of [...keys].sort()) {
        const previousValue = previous[key];
        const nextValue = next[key];
        if (!(key in previous) && nextValue === undefined) continue;
        visit(previousValue, nextValue, path ? `${path}.${key}` : key);
        if (truncated) return;
      }
      return;
    }

    if (Array.isArray(previous) && Array.isArray(next)) {
      const length = Math.max(previous.length, next.length);
      for (let index = 0; index < length; index += 1) {
        visit(previous[index], next[index], `${path}[${index}]`);
        if (truncated) return;
      }
      return;
    }

    if (changes.length >= limit) {
      truncated = true;
      return;
    }
    changes.push({
      path: path || "$",
      ...(previous === undefined ? {} : { before: previous }),
      ...(next === undefined ? {} : { after: next }),
    });
  };

  visit(before, after, "");
  return { changes, truncated };
}

export function summarizeRouteRule(rule: RouteRule): string {
  const parts: string[] = [];
  if (rule.list && rule.list.length > 0) parts.push(`list ${rule.list.join(", ")}`);
  if (rule.proto) parts.push(`proto ${rule.proto}`);
  if (rule.dest_port) parts.push(`dst port ${rule.dest_port}`);
  if (rule.src_port) parts.push(`src port ${rule.src_port}`);
  if (rule.src_addr) parts.push(`src ${rule.src_addr}`);
  if (rule.dest_addr) parts.push(`dst ${rule.dest_addr}`);
  if (rule.default_gateway) parts.push(`default gateway ${rule.default_gateway}`);
  return `${parts.length > 0 ? parts.join(" + ") : "(no match conditions)"} -> ${rule.outbound}${
    rule.enabled === false ? " (disabled)" : ""
  }`;
}

/**
 * Applies a read-modify-write change to the staged draft and, when asked, saves
 * it. The caller must have checked write access first.
 */
export async function commitDraftChange(
  client: KeenPbrClient,
  options: {
    before: DraftConfig;
    after: DraftConfig;
    changeSummary: Record<string, unknown>;
    save: boolean;
    saveWaitSeconds: number;
  },
): Promise<DraftChangeResult> {
  const diff = diffConfigs(options.before, options.after);
  if (diff.changes.length === 0) {
    return {
      changed: false,
      message: "Nothing changed: the requested entries already match the config.",
    };
  }

  await client.stageConfig(options.after);
  const result: DraftChangeResult = {
    changed: true,
    is_draft: true,
    change_summary: options.changeSummary,
    changes: diff.changes,
    ...(diff.truncated ? { changes_truncated: true } : {}),
    next_step:
      "The change is staged in the daemon's in-memory draft only. Call save_config to persist and apply it, or discard_config to drop it.",
  };

  if (options.save) {
    result.saved = await performSave(client, options.saveWaitSeconds);
    result.next_step = "Saved and applied.";
  }
  return result;
}

export async function performSave(
  client: KeenPbrClient,
  waitSeconds: number,
): Promise<Record<string, unknown>> {
  const accepted = await client.saveConfig();
  const operationId = accepted.operation_id;
  if (waitSeconds <= 0) {
    return {
      accepted: true,
      operation_id: operationId,
      waited: false,
      note: "The apply runs asynchronously. Poll get_health and inspect lifecycle_operation.",
    };
  }

  const waited = await client.waitForLifecycle(operationId, waitSeconds * 1_000);
  if (!waited.done) {
    return {
      accepted: true,
      operation_id: operationId,
      waited: true,
      finished: false,
      note: `The apply did not finish within ${waitSeconds}s. Check get_health for lifecycle_operation.`,
    };
  }

  const operation = waited.health?.lifecycle_operation;
  const reported = operation?.id === operationId;
  const failed = reported && operation.status === "failed";
  return {
    accepted: true,
    operation_id: operationId,
    waited: true,
    finished: true,
    status: failed ? "failed" : (reported ? operation.status : "unknown"),
    ...(failed && operation?.error ? { error: operation.error } : {}),
    ...(waited.health?.runtime_state ? { runtime_state: waited.health.runtime_state } : {}),
    ...(reported
      ? {}
      : { note: "The apply is no longer reported in health; check get_health for the current runtime state." }),
  };
}

function isPlainObject(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}
