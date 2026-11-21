import type { CallToolResult } from "@modelcontextprotocol/sdk/types.js";
import { ToolInputError, describeError } from "../errors.js";

export function textResult(value: unknown): CallToolResult {
  const text = typeof value === "string" ? value : JSON.stringify(value, null, 2);
  return { content: [{ type: "text", text }] };
}

export function errorResult(error: unknown): CallToolResult {
  return { content: [{ type: "text", text: describeError(error) }], isError: true };
}

/** Runs a tool body and converts any failure into an MCP tool error. */
export async function guarded(body: () => Promise<unknown>): Promise<CallToolResult> {
  try {
    return textResult(await body());
  } catch (error) {
    return errorResult(error);
  }
}

export function requireWrite(allowWrite: boolean): void {
  if (allowWrite) return;
  throw new ToolInputError(
    "Write access is disabled. Config-changing tools require KEEN_PBR_ALLOW_WRITE=1 in the MCP server environment. " +
      "Add that variable to the keen-pbr MCP server configuration, restart the server, and retry. Read-only diagnostics are still available.",
  );
}

export function summarizeRoutingTest(result: {
  target: string;
  is_domain: boolean;
  resolved_ips: string[];
  warnings: string[];
  dns_error?: string | null;
  no_matching_rule: boolean;
  ipv6_enabled: boolean;
  results: Array<Record<string, unknown>>;
  rule_diagnostics?: Array<Record<string, unknown>>;
}): Record<string, unknown> {
  const failed = result.results.filter((entry) => entry.ok === false || entry.actual_outbound === "(unknown)");
  return {
    target: result.target,
    is_domain: result.is_domain,
    resolved_ips: result.resolved_ips,
    no_matching_rule: result.no_matching_rule,
    ipv6_enabled: result.ipv6_enabled,
    warnings: result.warnings,
    ...(result.dns_error ? { dns_error: result.dns_error } : {}),
    summary:
      failed.length === 0
        ? "Expected and live routing agree for every evaluated address."
        : `${failed.length} of ${result.results.length} address(es) differ from the expected routing.`,
    results: result.results,
    ...(result.rule_diagnostics ? { rule_diagnostics: result.rule_diagnostics } : {}),
  };
}
