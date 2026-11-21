import type { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";
import type { KeenPbrClient } from "../client.js";
import {
  assertListName,
  assertOutboundExists,
  cloneConfig,
  commitDraftChange,
  domainKey,
  isValidIpOrCidr,
  normalizeDomain,
} from "../config-edit.js";
import { ToolInputError } from "../errors.js";
import type { ListConfig } from "../types.js";
import type { DraftChangeResult } from "../config-edit.js";
import { Mutex } from "../mutex.js";
import { guarded, requireWrite } from "./common.js";

export interface AddDomainsArgs {
  list: string;
  domains: string[];
  createIfMissing: boolean;
  save: boolean;
  saveWaitSeconds: number;
}

export interface RemoveDomainsArgs {
  list: string;
  domains: string[];
  save: boolean;
  saveWaitSeconds: number;
}

export interface CreateListArgs {
  name: string;
  domains?: string[];
  ipCidrs?: string[];
  url?: string;
  file?: string;
  ttlMs?: number;
  detour?: string;
  save: boolean;
  saveWaitSeconds: number;
}

/** Read-modify-write the config draft. Exported for unit tests. */
export async function addDomainsToDraft(
  client: KeenPbrClient,
  args: AddDomainsArgs,
): Promise<DraftChangeResult> {
  const state = await client.getConfig();
  const before = state.config;
  const after = cloneConfig(before);
  const name = assertListName(args.list);

  let list = after.lists?.[name];
  let created = false;
  if (!list) {
    if (!args.createIfMissing) {
      throw new ToolInputError(
        `List "${name}" does not exist. Pass create_if_missing=true or create it with create_list. Available lists: ${
          after.lists && Object.keys(after.lists).length > 0 ? Object.keys(after.lists).join(", ") : "(none)"
        }.`,
      );
    }
    list = {};
    after.lists ??= {};
    after.lists[name] = list;
    created = true;
  }

  const existing = new Set((list.domains ?? []).map(domainKey));
  const added: string[] = [];
  const alreadyPresent: string[] = [];
  for (const input of args.domains) {
    const domain = normalizeDomain(input);
    const key = domainKey(domain);
    if (existing.has(key)) {
      alreadyPresent.push(domain);
      continue;
    }
    existing.add(key);
    added.push(domain);
  }

  if (added.length > 0) {
    list.domains = [...(list.domains ?? []), ...added];
  }

  return commitDraftChange(client, {
    before,
    after,
    changeSummary: {
      list: name,
      created_list: created,
      added,
      already_present: alreadyPresent,
    },
    save: args.save,
    saveWaitSeconds: args.saveWaitSeconds,
  });
}

/** Read-modify-write the config draft. Exported for unit tests. */
export async function removeDomainsFromDraft(
  client: KeenPbrClient,
  args: RemoveDomainsArgs,
): Promise<DraftChangeResult> {
  const state = await client.getConfig();
  const before = state.config;
  const after = cloneConfig(before);
  const name = assertListName(args.list);
  const list = after.lists?.[name];
  if (!list) {
    throw new ToolInputError(
      `List "${name}" does not exist. Available lists: ${
        after.lists && Object.keys(after.lists).length > 0 ? Object.keys(after.lists).join(", ") : "(none)"
      }.`,
    );
  }
  if (!list.domains || list.domains.length === 0) {
    throw new ToolInputError(
      `List "${name}" has no inline domains (it is backed by ${
        [list.url ? "a URL" : "", list.file ? "a file" : "", list.ip_cidrs ? "IP CIDRs" : ""]
          .filter(Boolean)
          .join(" and ") || "nothing"
      }). Only inline domains can be removed with this tool.`,
    );
  }

  const wanted = new Map<string, string>();
  for (const input of args.domains) {
    const domain = normalizeDomain(input);
    wanted.set(domainKey(domain), domain);
  }

  const existingKeys = new Set(list.domains.map(domainKey));
  const removed: string[] = [];
  const kept: string[] = [];
  for (const entry of list.domains) {
    if (wanted.has(domainKey(entry))) {
      removed.push(entry);
    } else {
      kept.push(entry);
    }
  }
  const notFound = [...wanted.entries()]
    .filter(([key]) => !existingKeys.has(key))
    .map(([, domain]) => domain);

  if (kept.length === 0 && !list.url && !list.file && !(list.ip_cidrs && list.ip_cidrs.length > 0)) {
    throw new ToolInputError(
      `Removing all inline domains would leave list "${name}" empty and invalid. Remove or repoint the route rule that uses it first, or keep at least one domain.`,
    );
  }

  if (kept.length === 0) {
    delete list.domains;
  } else {
    list.domains = kept;
  }

  return commitDraftChange(client, {
    before,
    after,
    changeSummary: { list: name, removed, not_found: notFound },
    save: args.save,
    saveWaitSeconds: args.saveWaitSeconds,
  });
}

/** Read-modify-write the config draft. Exported for unit tests. */
export async function createListInDraft(
  client: KeenPbrClient,
  args: CreateListArgs,
): Promise<DraftChangeResult> {
  const state = await client.getConfig();
  const before = state.config;
  const after = cloneConfig(before);
  const name = assertListName(args.name);

  if (after.lists?.[name]) {
    throw new ToolInputError(
      `List "${name}" already exists. Use add_domain_to_list or get_config to inspect it.`,
    );
  }
  if (!args.domains?.length && !args.ipCidrs?.length && !args.url && !args.file) {
    throw new ToolInputError("Provide at least one of domains, ip_cidrs, url or file.");
  }
  if (args.detour) assertOutboundExists(after, args.detour);

  const list: ListConfig = {};
  if (args.domains?.length) {
    const seen = new Set<string>();
    const domains: string[] = [];
    for (const input of args.domains) {
      const domain = normalizeDomain(input);
      const key = domainKey(domain);
      if (seen.has(key)) continue;
      seen.add(key);
      domains.push(domain);
    }
    list.domains = domains;
  }
  if (args.ipCidrs?.length) {
    const invalid = args.ipCidrs.filter((cidr) => !isValidIpOrCidr(cidr));
    if (invalid.length > 0) {
      throw new ToolInputError(`Invalid IP addresses or CIDRs: ${invalid.join(", ")}`);
    }
    list.ip_cidrs = [...args.ipCidrs];
  }
  if (args.url) {
    let parsed: URL;
    try {
      parsed = new URL(args.url);
    } catch {
      throw new ToolInputError(`url "${args.url}" is not a valid URL`);
    }
    if (parsed.protocol !== "http:" && parsed.protocol !== "https:") {
      throw new ToolInputError("url must use http:// or https://");
    }
    list.url = args.url;
  }
  if (args.file) list.file = args.file;
  if (args.ttlMs !== undefined) list.ttl_ms = args.ttlMs;
  if (args.detour) list.detour = args.detour;

  after.lists ??= {};
  after.lists[name] = list;

  return commitDraftChange(client, {
    before,
    after,
    changeSummary: { list: name, created: true, sources: list },
    save: args.save,
    saveWaitSeconds: args.saveWaitSeconds,
  });
}

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

export function registerListTools(
  server: McpServer,
  client: KeenPbrClient,
  options: { allowWrite: boolean },
  mutex: Mutex,
): void {
  server.registerTool(
    "add_domain_to_list",
    {
      title: "Add domains to a list",
      description:
        "Adds one or more domains to a named list in the config draft (read-modify-write). Creates an inline list when it does not exist and create_if_missing is true. Duplicates are skipped; wildcards such as *.example.com are accepted. Nothing is persisted until save_config is called, unless save=true.",
      inputSchema: {
        list: z.string().min(1).describe("List name (lowercase, digits, underscore; max 24 chars)"),
        domains: z.array(z.string().min(1)).min(1).max(500).describe("Domains to add"),
        create_if_missing: z
          .boolean()
          .default(true)
          .describe("Create an inline list when it does not exist"),
        ...saveArgs,
      },
      annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: true },
    },
    async (args) =>
      guarded(async () => {
        requireWrite(options.allowWrite);
        return mutex.run(() =>
          addDomainsToDraft(client, {
            list: args.list,
            domains: args.domains,
            createIfMissing: args.create_if_missing,
            save: args.save,
            saveWaitSeconds: args.save_wait_seconds,
          }),
        );
      }),
  );

  server.registerTool(
    "remove_domain_from_list",
    {
      title: "Remove domains from a list",
      description:
        "Removes domains from a list's inline domains in the config draft (read-modify-write). A wildcard entry *.example.com and example.com are treated as the same domain. Lists backed only by a URL or file cannot be edited this way. Nothing is persisted until save_config, unless save=true.",
      inputSchema: {
        list: z.string().min(1).describe("List name"),
        domains: z.array(z.string().min(1)).min(1).max(500).describe("Domains to remove"),
        ...saveArgs,
      },
      annotations: { readOnlyHint: false, destructiveHint: true, idempotentHint: true },
    },
    async (args) =>
      guarded(async () => {
        requireWrite(options.allowWrite);
        return mutex.run(() =>
          removeDomainsFromDraft(client, {
            list: args.list,
            domains: args.domains,
            save: args.save,
            saveWaitSeconds: args.save_wait_seconds,
          }),
        );
      }),
  );

  server.registerTool(
    "create_list",
    {
      title: "Create a list",
      description:
        "Creates a new named list in the config draft from inline domains, IP CIDRs, a local file or a URL. At least one source is required. URL lists can be populated with refresh_lists after saving. Nothing is persisted until save_config, unless save=true.",
      inputSchema: {
        name: z.string().min(1).describe("New list name (lowercase, digits, underscore; max 24 chars)"),
        domains: z.array(z.string().min(1)).max(5000).optional().describe("Inline domains or *.wildcards"),
        ip_cidrs: z.array(z.string().min(1)).max(5000).optional().describe("Inline IP addresses or CIDRs"),
        url: z.string().min(1).optional().describe("HTTP(S) URL of a remote list"),
        file: z.string().min(1).optional().describe("Path to a local list file on the router"),
        ttl_ms: z.number().int().min(0).optional().describe("Per-list dynamic set TTL floor in milliseconds"),
        detour: z.string().min(1).optional().describe("Outbound tag used to download a URL list"),
        ...saveArgs,
      },
      annotations: { readOnlyHint: false, destructiveHint: false, idempotentHint: false },
    },
    async (args) =>
      guarded(async () => {
        requireWrite(options.allowWrite);
        return mutex.run(() =>
          createListInDraft(client, {
            name: args.name,
            ...(args.domains ? { domains: args.domains } : {}),
            ...(args.ip_cidrs ? { ipCidrs: args.ip_cidrs } : {}),
            ...(args.url ? { url: args.url } : {}),
            ...(args.file ? { file: args.file } : {}),
            ...(args.ttl_ms === undefined ? {} : { ttlMs: args.ttl_ms }),
            ...(args.detour ? { detour: args.detour } : {}),
            save: args.save,
            saveWaitSeconds: args.save_wait_seconds,
          }),
        );
      }),
  );
}
