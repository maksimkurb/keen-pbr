import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import type { KeenPbrClient } from "./client.js";
import { Mutex } from "./mutex.js";
import { registerConfigTools } from "./tools/config.js";
import { registerDiagnosticsTools } from "./tools/diagnostics.js";
import { registerDnsTools } from "./tools/dns.js";
import { registerListTools } from "./tools/lists.js";
import { registerRuleTools } from "./tools/rules.js";

export const SERVER_NAME = "keen-pbr";
export const SERVER_VERSION = "0.1.0";

export interface ServerOptions {
  client: KeenPbrClient;
  allowWrite: boolean;
}

export function createServer(options: ServerOptions): McpServer {
  const server = new McpServer(
    { name: SERVER_NAME, version: SERVER_VERSION },
    {
      instructions:
        "keen-pbr policy-based routing on a Keenetic/OpenWrt router. Start with diagnose when something is broken. " +
        "Config changes are draft-then-save: editing tools stage an in-memory draft only; call save_config to persist and apply, " +
        "or discard_config to drop it. Mutating tools require KEEN_PBR_ALLOW_WRITE=1 in the server environment. " +
        "Never guess list or outbound names: call list_rules/get_config first.",
    },
  );

  const mutex = new Mutex();
  registerDiagnosticsTools(server, options.client);
  registerDnsTools(server, options.client);
  registerConfigTools(server, options.client, { allowWrite: options.allowWrite });
  registerListTools(server, options.client, { allowWrite: options.allowWrite }, mutex);
  registerRuleTools(server, options.client, { allowWrite: options.allowWrite }, mutex);

  return server;
}
