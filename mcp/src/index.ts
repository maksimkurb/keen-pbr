#!/usr/bin/env node
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { KeenPbrClient } from "./client.js";
import { loadConfig } from "./config.js";
import { describeError } from "./errors.js";
import { createServer } from "./server.js";

async function main(): Promise<void> {
  let config;
  try {
    config = loadConfig();
  } catch (error) {
    // stderr only: stdout carries the MCP protocol.
    process.stderr.write(`keen-pbr MCP server: ${describeError(error)}\n`);
    process.exit(1);
  }

  const client = new KeenPbrClient(config);
  const server = createServer({ client, allowWrite: config.allowWrite });
  const transport = new StdioServerTransport();
  await server.connect(transport);

  process.stderr.write(
    `keen-pbr MCP server ready: ${config.baseUrl} (write ${config.allowWrite ? "enabled" : "disabled"})\n`,
  );
}

main().catch((error: unknown) => {
  process.stderr.write(`keen-pbr MCP server failed: ${describeError(error)}\n`);
  process.exit(1);
});
