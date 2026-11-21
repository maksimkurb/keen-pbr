import type {
  HealthIssue,
  HealthIssuesResponse,
  HealthResponse,
  RoutingHealthResponse,
  RoutingHealthWarning,
  RoutingHealthWarningCode,
  RuntimeInterfaceInventoryResponse,
  RuntimeOutboundsResponse,
} from "./types.js";

export type EndpointResult<T> = { ok: true; value: T } | { ok: false; error: string };

export interface DiagnosisInput {
  health: EndpointResult<HealthResponse>;
  routing: EndpointResult<RoutingHealthResponse>;
  issues: EndpointResult<HealthIssuesResponse>;
  outbounds: EndpointResult<RuntimeOutboundsResponse>;
  interfaces: EndpointResult<RuntimeInterfaceInventoryResponse>;
}

export type DiagnosisVerdict = "healthy" | "warning" | "critical" | "unreachable";
export type ProblemSeverity = "error" | "warning";

export interface DiagnosisProblem {
  severity: ProblemSeverity;
  source: string;
  title: string;
  detail?: string;
  code?: string;
  outbound?: string;
  interface?: string;
  list?: string;
  entries?: string[];
}

export interface Diagnosis {
  generated_at: string;
  verdict: DiagnosisVerdict;
  headline: string;
  service: {
    reachable: boolean;
    status?: string;
    runtime_state?: string;
    runtime_state_reason?: string;
    version?: string;
    os_type?: string;
    os_version?: string;
    config_is_draft?: boolean;
    rollback_available?: boolean;
    lifecycle_operation?: { id: string; type: string; status: string; error?: string };
  };
  routing: {
    reachable: boolean;
    overall?: string;
    firewall_backend?: string;
    failed_checks: string[];
    warnings: Array<{
      code: string;
      title: string;
      message: string;
      interface?: string;
      outbound?: string;
    }>;
  };
  outbounds: {
    reachable: boolean;
    total?: number;
    healthy?: number;
    degraded?: number;
    unavailable?: number;
    unknown?: number;
    failing: Array<{
      tag: string;
      type: string;
      status: string;
      detail?: string;
      interfaces: Array<{ outbound_tag: string; interface_name?: string; status: string; detail?: string }>;
    }>;
  };
  interfaces: {
    reachable: boolean;
    total?: number;
    up?: number;
    down?: number;
    down_names: string[];
  };
  issues: {
    reachable: boolean;
    errors: number;
    warnings: number;
  };
  problems: DiagnosisProblem[];
  suggested_next_step: string;
}

const WARNING_TITLES: Record<RoutingHealthWarningCode, string> = {
  nat_missing: "NAT is missing for an outbound interface",
  nat_partial: "NAT only covers part of the outbound traffic",
  rp_filter_strict: "Strict reverse-path filtering can drop policy-routed traffic",
  fwmark_mask_conflict: "Another firewall rule uses keen-pbr fwmark bits",
  stale_policy_rule: "A stale keen-pbr policy rule is still installed",
  foreign_rule_precedence: "A foreign policy rule runs before keen-pbr rules",
};

const ISSUE_SOURCE: Record<string, string> = {
  kernel_capability: "kernel",
  xt_extension: "kernel",
  kernel_probe: "kernel",
  intercept_limited: "intercept",
  intercept_warning: "intercept",
  routing_check_failed: "routing",
  firewall_chain_missing: "firewall",
  firewall_prerouting_missing: "firewall",
  firewall_rules: "firewall",
  route_tables: "routing",
  policy_rules: "routing",
  host_warning: "host",
  dnsmasq_error: "dnsmasq",
  dnsmasq_reconciling: "dnsmasq",
  outbound_unavailable: "outbound",
  outbound_degraded: "outbound",
  group_members_failing: "outbound",
  doh_unavailable: "dns",
  list_not_downloaded: "list",
  dns_server_failing: "dns",
  dns_upstream_missing: "dns",
  doh_bootstrap_missing: "dns",
};

export function settled<T>(result: PromiseSettledResult<T>): EndpointResult<T> {
  if (result.status === "fulfilled") return { ok: true, value: result.value };
  return { ok: false, error: result.reason instanceof Error ? result.reason.message : String(result.reason) };
}

export async function collectDiagnosis(client: {
  getServiceHealth(): Promise<HealthResponse>;
  getRoutingHealth(): Promise<RoutingHealthResponse>;
  getHealthIssues(): Promise<HealthIssuesResponse>;
  getRuntimeOutbounds(): Promise<RuntimeOutboundsResponse>;
  getRuntimeInterfaces(): Promise<RuntimeInterfaceInventoryResponse>;
}): Promise<Diagnosis> {
  const [health, routing, issues, outbounds, interfaces] = await Promise.allSettled([
    client.getServiceHealth(),
    client.getRoutingHealth(),
    client.getHealthIssues(),
    client.getRuntimeOutbounds(),
    client.getRuntimeInterfaces(),
  ]);
  return buildDiagnosis({
    health: settled(health),
    routing: settled(routing),
    issues: settled(issues),
    outbounds: settled(outbounds),
    interfaces: settled(interfaces),
  });
}

export function buildDiagnosis(input: DiagnosisInput): Diagnosis {
  const problems: DiagnosisProblem[] = [];
  const failedEndpoints: string[] = [];

  for (const [name, result, path] of [
    ["health", input.health, "/api/health/service"],
    ["routing health", input.routing, "/api/health/routing"],
    ["health issues", input.issues, "/api/health/issues"],
    ["runtime outbounds", input.outbounds, "/api/runtime/outbounds"],
    ["runtime interfaces", input.interfaces, "/api/runtime/interfaces"],
  ] as const) {
    if (!result.ok) {
      failedEndpoints.push(path);
      problems.push({
        severity: "error",
        source: "api",
        title: `Cannot read ${name} (${path})`,
        detail: result.error,
      });
    }
  }

  // Issues are the server's authoritative, prioritised problem list.
  if (input.issues.ok) {
    for (const issue of input.issues.value.issues) {
      problems.push(issueToProblem(issue));
    }
  } else {
    // Fall back to the individual endpoints so a broken issues endpoint still
    // produces actionable diagnostics.
    if (input.routing.ok) {
      problems.push(...routingProblems(input.routing.value));
    }
    if (input.outbounds.ok) {
      problems.push(...outboundProblems(input.outbounds.value));
    }
  }

  if (input.health.ok) {
    problems.push(...healthProblems(input.health.value, !input.issues.ok));
  }

  const routingWarnings = input.routing.ok
    ? (input.routing.value.warnings ?? []).map((warning: RoutingHealthWarning) => ({
        code: warning.code,
        title: WARNING_TITLES[warning.code] ?? warning.code,
        message: warning.message,
        ...(warning.interface ? { interface: warning.interface } : {}),
        ...(warning.outbound ? { outbound: warning.outbound } : {}),
      }))
    : [];

  const outbounds = summarizeOutbounds(input.outbounds);
  const interfaces = summarizeInterfaces(input.interfaces);
  const issueCounts = input.issues.ok
    ? {
        reachable: true,
        errors: input.issues.value.issues.filter((issue) => issue.severity === "error").length,
        warnings: input.issues.value.issues.filter((issue) => issue.severity === "warning").length,
      }
    : { reachable: false, errors: 0, warnings: 0 };

  const failedChecks = input.routing.ok
    ? routingFailedChecks(input.routing.value)
    : [];
  const routingSummary = {
    reachable: input.routing.ok,
    ...(input.routing.ok
      ? {
          overall: input.routing.value.overall,
          firewall_backend: input.routing.value.firewall_backend,
        }
      : {}),
    failed_checks: failedChecks,
    warnings: routingWarnings,
  };

  const sortedProblems = sortProblems(dedupeProblems(problems));
  const errorCount = sortedProblems.filter((problem) => problem.severity === "error").length;
  const warningCount = sortedProblems.filter((problem) => problem.severity === "warning").length;

  let verdict: DiagnosisVerdict;
  if (failedEndpoints.length === 5) {
    verdict = "unreachable";
  } else if (errorCount > 0) {
    verdict = "critical";
  } else if (warningCount > 0) {
    verdict = "warning";
  } else {
    verdict = "healthy";
  }

  const headline =
    verdict === "unreachable"
      ? `Cannot reach the keen-pbr API at all: ${failedEndpoints.join(", ")} failed`
      : verdict === "healthy"
        ? "No service, routing or outbound problems were reported"
        : `${errorCount} error${errorCount === 1 ? "" : "s"}, ${warningCount} warning${warningCount === 1 ? "" : "s"}: ${sortedProblems
            .slice(0, 3)
            .map((problem) => problem.title)
            .join("; ")}`;

  return {
    generated_at: new Date().toISOString(),
    verdict,
    headline,
    service: summarizeService(input.health),
    routing: routingSummary,
    outbounds,
    interfaces,
    issues: issueCounts,
    problems: sortedProblems,
    suggested_next_step: suggestNextStep({
      input,
      problems: sortedProblems,
      failedEndpoints,
      outbounds,
      interfaces,
      routingWarnings,
    }),
  };
}

function summarizeService(result: EndpointResult<HealthResponse>): Diagnosis["service"] {
  if (!result.ok) return { reachable: false };
  const health = result.value;
  return {
    reachable: true,
    status: health.status,
    ...(health.runtime_state ? { runtime_state: health.runtime_state } : {}),
    ...(health.runtime_state_reason ? { runtime_state_reason: health.runtime_state_reason } : {}),
    version: health.version,
    os_type: health.os_type,
    os_version: health.os_version,
    config_is_draft: health.config_is_draft,
    rollback_available: health.rollback_available,
    ...(health.lifecycle_operation
      ? {
          lifecycle_operation: {
            id: health.lifecycle_operation.id,
            type: health.lifecycle_operation.type,
            status: health.lifecycle_operation.status,
            ...(health.lifecycle_operation.error
              ? { error: health.lifecycle_operation.error }
              : {}),
          },
        }
      : {}),
  };
}

function summarizeOutbounds(result: EndpointResult<RuntimeOutboundsResponse>): Diagnosis["outbounds"] {
  if (!result.ok) return { reachable: false, failing: [] };
  let healthy = 0;
  let degraded = 0;
  let unavailable = 0;
  let unknown = 0;
  const failing: Diagnosis["outbounds"]["failing"] = [];
  for (const outbound of result.value.outbounds) {
    switch (outbound.status) {
      case "healthy":
        healthy += 1;
        break;
      case "degraded":
        degraded += 1;
        break;
      case "unavailable":
        unavailable += 1;
        break;
      default:
        unknown += 1;
    }
    if (outbound.status === "degraded" || outbound.status === "unavailable") {
      failing.push({
        tag: outbound.tag,
        type: outbound.type,
        status: outbound.status,
        ...(outbound.detail ? { detail: outbound.detail } : {}),
        interfaces: outbound.interfaces.map((candidate) => ({
          outbound_tag: candidate.outbound_tag,
          ...(candidate.interface_name ? { interface_name: candidate.interface_name } : {}),
          status: candidate.status,
          ...(candidate.detail ? { detail: candidate.detail } : {}),
        })),
      });
    }
  }
  return {
    reachable: true,
    total: result.value.outbounds.length,
    healthy,
    degraded,
    unavailable,
    unknown,
    failing,
  };
}

function summarizeInterfaces(
  result: EndpointResult<RuntimeInterfaceInventoryResponse>,
): Diagnosis["interfaces"] {
  if (!result.ok) return { reachable: false, down_names: [] };
  const down = result.value.interfaces.filter((entry) => entry.status === "down");
  return {
    reachable: true,
    total: result.value.interfaces.length,
    up: result.value.interfaces.length - down.length,
    down: down.length,
    down_names: down.map((entry) => entry.name).slice(0, 20),
  };
}

function issueToProblem(issue: HealthIssue): DiagnosisProblem {
  const params = issue.params ?? {};
  const outbound = issue.outbound ?? (typeof params.tag === "string" ? params.tag : undefined);
  const iface = typeof params.interface === "string" ? params.interface : undefined;
  const list = typeof params.list === "string" ? params.list : undefined;
  return {
    severity: issue.severity,
    source: ISSUE_SOURCE[issue.code] ?? "health",
    title: issue.title,
    ...(issue.detail ? { detail: issue.detail } : {}),
    code: issue.code,
    ...(outbound ? { outbound } : {}),
    ...(iface ? { interface: iface } : {}),
    ...(list ? { list } : {}),
    ...(issue.entries && issue.entries.length > 0 ? { entries: issue.entries.slice(0, 10) } : {}),
  };
}

function routingProblems(health: RoutingHealthResponse): DiagnosisProblem[] {
  const problems: DiagnosisProblem[] = [];
  if (health.overall === "error") {
    problems.push({
      severity: "error",
      source: "routing",
      title: "Routing health check could not complete",
      detail: "GET /api/health/routing reported overall=error",
      code: "routing_check_failed",
    });
  }
  if (health.firewall.verification_state === "failed") {
    problems.push({
      severity: "error",
      source: "firewall",
      title: health.firewall.chain_present
        ? "Firewall chain is misconfigured"
        : "keen-pbr firewall chain is missing",
      ...(health.firewall.detail ? { detail: health.firewall.detail } : {}),
      code: health.firewall.chain_present ? "firewall_rules" : "firewall_chain_missing",
    });
  }
  for (const rule of health.firewall_rules) {
    if (rule.status !== "ok") {
      problems.push({
        severity: "error",
        source: "firewall",
        title: `Firewall rule for set ${rule.set_name} is ${rule.status}`,
        ...(rule.detail ? { detail: rule.detail } : {}),
        code: "firewall_rules",
      });
    }
  }
  for (const table of health.route_tables) {
    if (table.status !== "ok") {
      problems.push({
        severity: "error",
        source: "routing",
        title: `Route table ${table.table_id} for outbound ${table.outbound_tag} is ${table.status}`,
        ...(table.detail ? { detail: table.detail } : {}),
        outbound: table.outbound_tag,
        code: "route_tables",
      });
    }
  }
  for (const rule of health.policy_rules) {
    if (rule.status !== "ok") {
      problems.push({
        severity: "error",
        source: "routing",
        title: `Policy rule for fwmark ${rule.fwmark} is ${rule.status}`,
        ...(rule.detail ? { detail: rule.detail } : {}),
        code: "policy_rules",
      });
    }
  }
  return problems;
}

function outboundProblems(response: RuntimeOutboundsResponse): DiagnosisProblem[] {
  const problems: DiagnosisProblem[] = [];
  for (const outbound of response.outbounds) {
    if (outbound.status === "unavailable" || outbound.status === "degraded") {
      problems.push({
        severity: outbound.status === "unavailable" ? "error" : "warning",
        source: "outbound",
        title: `Outbound ${outbound.tag} is ${outbound.status}`,
        ...(outbound.detail ? { detail: outbound.detail } : {}),
        outbound: outbound.tag,
        code: outbound.status === "unavailable" ? "outbound_unavailable" : "outbound_degraded",
        entries: outbound.interfaces.map(
          (candidate) =>
            `${candidate.outbound_tag}${candidate.interface_name ? ` (${candidate.interface_name})` : ""}: ${candidate.status}${
              candidate.detail ? ` — ${candidate.detail}` : ""
            }`,
        ),
      });
    }
  }
  return problems;
}

function healthProblems(health: HealthResponse, includeSubsystemChecks: boolean): DiagnosisProblem[] {
  const problems: DiagnosisProblem[] = [];
  if (health.status !== "running") {
    problems.push({
      severity: health.status === "stopped" ? "error" : "warning",
      source: "service",
      title: `Routing runtime is ${health.status}`,
      ...(health.runtime_state_reason ? { detail: health.runtime_state_reason } : {}),
      ...(health.runtime_state ? { code: health.runtime_state } : {}),
    });
  }
  if (health.runtime_state === "broken" || health.runtime_state === "restart_required") {
    problems.push({
      severity: "error",
      source: "service",
      title: `Runtime state is ${health.runtime_state}`,
      ...(health.runtime_state_reason ? { detail: health.runtime_state_reason } : {}),
    });
  }
  // /api/health/issues already classifies interception and dnsmasq problems.
  // Only derive them from the health payload when that endpoint is unavailable.
  const intercept = includeSubsystemChecks ? health.intercept : undefined;
  if (intercept && intercept.enabled && !intercept.running) {
    problems.push({
      severity: "error",
      source: "intercept",
      title: "Traffic interception is enabled but not running",
      ...(intercept.reasons.length > 0 ? { detail: intercept.reasons.join("; ") } : {}),
    });
  }
  if (intercept && intercept.enabled && !intercept.dns_hold_active) {
    problems.push({
      severity: "warning",
      source: "intercept",
      title: "DNS interception hold is not active",
      detail: "Domain-based lists are not filled from DNS responses; see intercept.reasons.",
    });
  }
  const dnsmasq = includeSubsystemChecks ? health.dnsmasq : undefined;
  if (dnsmasq && dnsmasq.state === "error") {
    problems.push({
      severity: "error",
      source: "dnsmasq",
      title: "dnsmasq integration failed",
      ...(dnsmasq.last_error ? { detail: dnsmasq.last_error } : {}),
      ...(dnsmasq.error_code ? { code: dnsmasq.error_code } : {}),
    });
  }
  if (health.lifecycle_operation?.status === "failed") {
    problems.push({
      severity: "error",
      source: "lifecycle",
      title: `${health.lifecycle_operation.type} failed`,
      ...(health.lifecycle_operation.error ? { detail: health.lifecycle_operation.error } : {}),
    });
  }
  return problems;
}

function routingFailedChecks(health: RoutingHealthResponse): string[] {
  const failed: string[] = [];
  if (health.firewall.verification_state === "failed") failed.push("firewall chain");
  for (const rule of health.firewall_rules) {
    if (rule.status !== "ok") failed.push(`firewall rule ${rule.set_name}: ${rule.status}`);
  }
  for (const table of health.route_tables) {
    if (table.status !== "ok") failed.push(`route table ${table.table_id} (${table.outbound_tag}): ${table.status}`);
  }
  for (const rule of health.policy_rules) {
    if (rule.status !== "ok") failed.push(`policy rule ${rule.fwmark}: ${rule.status}`);
  }
  return failed;
}

function sortProblems(problems: DiagnosisProblem[]): DiagnosisProblem[] {
  const rank: Record<ProblemSeverity, number> = { error: 0, warning: 1 };
  return [...problems].sort((a, b) => rank[a.severity] - rank[b.severity]);
}

function dedupeProblems(problems: DiagnosisProblem[]): DiagnosisProblem[] {
  const seen = new Set<string>();
  return problems.filter((problem) => {
    const key = `${problem.severity}|${problem.source}|${problem.title}|${problem.detail ?? ""}`;
    if (seen.has(key)) return false;
    seen.add(key);
    return true;
  });
}

function suggestNextStep(context: {
  input: DiagnosisInput;
  problems: DiagnosisProblem[];
  failedEndpoints: string[];
  outbounds: Diagnosis["outbounds"];
  interfaces: Diagnosis["interfaces"];
  routingWarnings: Array<{ code: string; title: string; message: string }>;
}): string {
  const { input, problems, failedEndpoints, outbounds, interfaces, routingWarnings } = context;

  if (failedEndpoints.length === 5) {
    return "The API is unreachable. Check KEEN_PBR_URL, that api.enabled is true, and that KEEN_PBR_TOKEN is valid.";
  }

  const service = input.health.ok ? input.health.value : undefined;
  if (service && (service.status === "stopped" || service.runtime_state === "stopped")) {
    return "The routing runtime is stopped. Start keen-pbr from the router CLI or web UI (this MCP server does not expose service start/stop), then run diagnose again.";
  }
  const errorCodes = new Set(problems.filter((p) => p.severity === "error").map((p) => p.code ?? ""));
  if (
    errorCodes.has("routing_check_failed") ||
    errorCodes.has("firewall_chain_missing") ||
    errorCodes.has("firewall_prerouting_missing") ||
    errorCodes.has("firewall_rules") ||
    errorCodes.has("route_tables") ||
    errorCodes.has("policy_rules")
  ) {
    return "Kernel routing or firewall state diverged from the config. Run routing_test for a known affected host; if it confirms the mismatch, re-apply the config (save_config) or restart the keen-pbr runtime from the router CLI.";
  }
  if (
    errorCodes.has("outbound_unavailable") ||
    errorCodes.has("outbound_degraded") ||
    errorCodes.has("group_members_failing") ||
    (outbounds.unavailable ?? 0) > 0
  ) {
    const tag = problems.find((p) => p.source === "outbound" && (p.code?.startsWith("outbound") || p.outbound))?.outbound;
    return `An outbound is failing${tag ? ` (${tag})` : ""}. Run get_runtime_outbounds and get_runtime_interfaces, then check that its interface is up, has a carrier and the configured gateway is reachable.`;
  }
  if ((interfaces.down ?? 0) > 0 && outbounds.failing.length > 0) {
    return `Interfaces are down (${interfaces.down_names.join(", ")}). Bring the WAN/VPN interface up and check its cable or tunnel.`;
  }
  if (errorCodes.has("dnsmasq_error") || errorCodes.has("dnsmasq_reconciling")) {
    return "dnsmasq integration is failing. Run get_health for dnsmasq details, then check the dnsmasq instance selector and re-save the config.";
  }
  if (errorCodes.has("doh_unavailable") || errorCodes.has("dns_server_failing")) {
    return "A DNS upstream is failing. Run dns_check_servers to identify the bad server, then adjust dns.servers or its detour with get_config.";
  }
  if (errorCodes.has("list_not_downloaded")) {
    return "A URL-backed list has not been downloaded. Run refresh_lists (write access required) and check the list URL.";
  }
  if (service?.config_is_draft) {
    return "A config draft is staged. Review it with get_config, then call save_config to apply it or discard_config to throw it away.";
  }
  if (failedEndpoints.length > 0) {
    return `Some endpoints failed (${failedEndpoints.join(", ")}). Check the daemon log, then retry get_health.`;
  }
  if (routingWarnings.length > 0) {
    return `Routing works, but host warnings need attention: ${routingWarnings.map((warning) => warning.title).slice(0, 3).join("; ")}. Check NAT/firewall and policy-rule precedence on the router.`;
  }
  if (problems.length === 0) {
    return "No action needed. Use routing_test to verify where a specific domain or IP is routed.";
  }
  return "Inspect the highest-severity problem above, then re-run diagnose after fixing it.";
}
