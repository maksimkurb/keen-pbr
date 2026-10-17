import type {
  HealthIssue,
  KernelCheck,
  RoutingHealthResponse,
  RuntimeOutboundState,
} from "@/api/generated/model"

type TranslateFn = (key: string, options?: Record<string, unknown>) => string

export type OverviewIssue = {
  key: string
  tone: "warn" | "bad"
  title: string
  detail?: string
  href?: string
}

export type OverviewHealthySummary = {
  key: string
  title: string
  detail: string
}

const OUTBOUND_ISSUE_CODES = new Set<HealthIssue["code"]>([
  "outbound_unavailable",
  "outbound_degraded",
  "group_members_failing",
])

/**
 * Translates the daemon's classified health issues (GET /api/health/issues).
 * The daemon decides which problems exist and how severe they are, so the
 * overview and `keen-pbr status` agree; this only picks the wording.
 */
export function mapHealthIssues(
  issues: ReadonlyArray<HealthIssue>,
  t: TranslateFn
): OverviewIssue[] {
  return issues.map((issue) => ({
    key: issue.key,
    tone: issue.severity === "error" ? "bad" : "warn",
    title: translateTitle(issue, t),
    detail: translateDetail(issue, t),
    href:
      issue.outbound && OUTBOUND_ISSUE_CODES.has(issue.code)
        ? `/outbounds/${encodeURIComponent(issue.outbound)}/edit`
        : undefined,
  }))
}

/** ok and skipped need no attention; not_run is unknown and stays visible. */
export function isHealthyKernelCheck(check: KernelCheck): boolean {
  return check.status === "ok" || check.status === "skipped"
}

export function collectHealthySummaries({
  routingHealth,
  kernelChecks,
  kernelRelease,
  runtimeOutbounds,
  t,
}: {
  routingHealth?: RoutingHealthResponse
  kernelChecks: ReadonlyArray<KernelCheck>
  kernelRelease?: string
  runtimeOutbounds: RuntimeOutboundState[]
  t: TranslateFn
}): OverviewHealthySummary[] {
  const summaries: OverviewHealthySummary[] = []

  if (routingHealth) {
    const { firewall } = routingHealth
    summaries.push({
      key: "firewall",
      title: routingHealth.firewall_backend,
      detail:
        firewall.chain_present && firewall.prerouting_hook_present
          ? t("overview.healthy.firewallOk")
          : t("overview.healthy.firewallPartial"),
    })
    summaries.push(
      countSummary(
        "firewall-rules",
        t("overview.routing.sections.firewall"),
        routingHealth.firewall_rules,
        t
      ),
      countSummary(
        "routes",
        t("overview.routing.sections.routes"),
        routingHealth.route_tables,
        t
      ),
      countSummary(
        "policies",
        t("overview.routing.sections.policies"),
        routingHealth.policy_rules,
        t
      )
    )
  }

  if (kernelChecks.length > 0) {
    summaries.push({
      key: "kernel",
      title: kernelRelease
        ? t("overview.healthy.kernelWithRelease", { release: kernelRelease })
        : t("overview.intercept.checksTitle"),
      detail: t("overview.healthy.passed", {
        passed: kernelChecks.filter(isHealthyKernelCheck).length,
        total: kernelChecks.length,
      }),
    })
  }

  if (runtimeOutbounds.length > 0) {
    summaries.push({
      key: "outbounds",
      title: t("overview.outbounds.title"),
      detail: t("overview.healthy.passed", {
        passed: runtimeOutbounds.filter(
          (outbound) => outbound.status === "healthy"
        ).length,
        total: runtimeOutbounds.length,
      }),
    })
  }

  return summaries
}

function translateTitle(issue: HealthIssue, t: TranslateFn): string {
  const params = issue.params
  switch (issue.code) {
    case "kernel_capability":
      return t(`overview.intercept.capabilities.${params.feature}`, {
        defaultValue: issue.title,
      })
    case "xt_extension":
      return t("overview.intercept.xtExtension", {
        kind: params.kind,
        name: params.name,
        module: params.module,
      })
    case "kernel_probe":
      return issue.title
    case "intercept_limited":
      return t("overview.issues.interceptLimited")
    case "intercept_warning":
      return t("overview.issues.interceptWarning")
    case "routing_check_failed":
      return t("overview.issues.routingCheckFailed")
    case "firewall_chain_missing":
      return t("overview.issues.chainMissing")
    case "firewall_prerouting_missing":
      return t("overview.issues.preroutingMissing")
    case "firewall_rules":
      return t("overview.issues.firewallRules", { count: Number(params.count) })
    case "route_tables":
      return t("overview.issues.routes", { count: Number(params.count) })
    case "policy_rules":
      return t("overview.issues.policies", { count: Number(params.count) })
    case "host_warning":
      return t(`overview.issues.hostWarning.${params.warning}`, {
        interface: params.interface ?? "",
        defaultValue: issue.title,
      })
    case "dnsmasq_error":
      return t("overview.issues.dnsmasqError")
    case "dnsmasq_reconciling":
      return t("overview.issues.dnsmasqReconciling")
    case "outbound_unavailable":
      return t("overview.issues.outboundUnavailable", { tag: params.tag })
    case "outbound_degraded":
      return t("overview.issues.outboundDegraded", { tag: params.tag })
    case "group_members_failing":
      return t("overview.issues.groupMembersFailing", {
        tag: params.tag,
        count: Number(params.count),
      })
    case "keenetic_dns_policy_missing":
      return t("overview.issues.keeneticDnsPolicyMissing")
  }
  return issue.title
}

function translateDetail(
  issue: HealthIssue,
  t: TranslateFn
): string | undefined {
  switch (issue.detail_code) {
    case "capability_unsupported":
      return t("overview.issues.capabilityUnsupported")
    case "capability_fallback":
      return t("overview.issues.capabilityFallback")
    case "xt_extension_required":
      return t("overview.issues.xtExtensionRequired")
    case "xt_extension_unused":
      return t("overview.issues.xtExtensionUnused")
    case "kernel_check_failed":
      return t("overview.issues.kernelCheckFailed", {
        reason: issue.params.reason,
      })
    case "dnsmasq_dead":
      return t("overview.issues.dnsmasqDead")
    case "keenetic_dns_policy_blocked":
      return t("overview.issues.keeneticDnsPolicyBlocked", {
        tag: issue.params.tag,
        policy: issue.params.policy,
      })
    case "keenetic_dns_policy_system":
      return t("overview.issues.keeneticDnsPolicySystem", {
        tag: issue.params.tag,
        policy: issue.params.policy,
      })
  }
  return issue.detail
}

function countSummary(
  key: string,
  title: string,
  entries: ReadonlyArray<{ status: string }>,
  t: TranslateFn
): OverviewHealthySummary {
  return {
    key,
    title,
    detail: t("overview.healthy.passed", {
      passed: entries.filter((entry) => entry.status === "ok").length,
      total: entries.length,
    }),
  }
}
