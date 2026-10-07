import type {
  DnsmasqHealth,
  InterceptHealth,
  RouteTableCheck,
  RoutingHealthResponse,
  RuntimeOutboundState,
} from "@/api/generated/model"
import {
  isHealthyInterceptStatus,
  mapKernelDiagnosticEntries,
} from "@/lib/intercept-diagnostics"

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

const MAX_DETAIL_ENTRIES = 3

export function collectOverviewIssues({
  serviceRunning,
  routingHealth,
  routingHealthError,
  intercept,
  requestedDns,
  requestedL7,
  dnsmasq,
  runtimeOutbounds,
  t,
}: {
  serviceRunning: boolean
  routingHealth?: RoutingHealthResponse
  routingHealthError?: string | null
  intercept?: InterceptHealth
  requestedDns: boolean
  requestedL7: boolean
  dnsmasq?: DnsmasqHealth
  runtimeOutbounds: RuntimeOutboundState[]
  t: TranslateFn
}): OverviewIssue[] {
  const issues: OverviewIssue[] = []

  if (intercept) {
    const kernelEntries = mapKernelDiagnosticEntries(
      intercept.capabilities,
      intercept.probes ?? [],
      requestedDns,
      requestedL7
    )
    for (const entry of kernelEntries) {
      if (!entry.relevant || isHealthyInterceptStatus(entry.status)) {
        continue
      }
      const failed = entry.status === "error" || entry.status === "unsupported"
      issues.push({
        key: entry.key,
        tone: failed && !entry.advisory ? "bad" : "warn",
        title:
          entry.kind === "capability"
            ? t(`overview.intercept.capabilities.${entry.feature}`)
            : humanizeFeature(entry.feature),
        detail:
          entry.kind === "capability"
            ? entry.advisory
              ? t("overview.issues.capabilityFallback")
              : t("overview.issues.capabilityUnsupported")
            : t("overview.issues.kernelCheckFailed", {
                reason:
                  entry.reason ??
                  t(`overview.intercept.probes.status.${entry.status}`),
              }),
      })
    }
  }

  if (!serviceRunning) {
    return issues
  }

  if (intercept?.enabled) {
    intercept.reasons.forEach((reason, index) => {
      issues.push({
        key: `intercept-reason-${index}`,
        tone: "warn",
        title: t("overview.issues.interceptLimited"),
        detail: reason,
      })
    })
  }
  intercept?.warnings?.forEach((warning, index) => {
    issues.push({
      key: `intercept-warning-${index}`,
      tone: "warn",
      title: t("overview.issues.interceptWarning"),
      detail: warning,
    })
  })

  if (routingHealthError) {
    issues.push({
      key: "routing-error",
      tone: "bad",
      title: t("overview.issues.routingCheckFailed"),
      detail: routingHealthError,
    })
  }

  if (routingHealth) {
    const { firewall } = routingHealth
    if (firewall.verification_state !== "unavailable") {
      if (!firewall.chain_present) {
        issues.push({
          key: "firewall-chain",
          tone: "bad",
          title: t("overview.issues.chainMissing"),
          detail: firewall.detail,
        })
      } else if (!firewall.prerouting_hook_present) {
        issues.push({
          key: "firewall-prerouting",
          tone: "bad",
          title: t("overview.issues.preroutingMissing"),
          detail: firewall.detail,
        })
      }
    }

    const badRules = routingHealth.firewall_rules.filter(
      (rule) => rule.status !== "ok"
    )
    if (badRules.length > 0) {
      issues.push({
        key: "firewall-rules",
        tone: "bad",
        title: t("overview.issues.firewallRules", { count: badRules.length }),
        detail: summarizeEntries(
          badRules.map((rule) =>
            [rule.set_name, rule.detail ?? rule.status].join(": ")
          )
        ),
      })
    }

    const badRoutes = routingHealth.route_tables.filter(
      (table) => table.status !== "ok"
    )
    if (badRoutes.length > 0) {
      issues.push({
        key: "route-tables",
        tone: "bad",
        title: t("overview.issues.routes", { count: badRoutes.length }),
        detail: summarizeEntries(
          badRoutes.map(
            (table) =>
              `${table.outbound_tag} (${t("overview.routing.tableLabel", {
                value: table.table_id,
              })}): ${describeRouteMismatch(table, t)}`
          )
        ),
      })
    }

    const badPolicies = routingHealth.policy_rules.filter(
      (policy) => policy.status !== "ok"
    )
    if (badPolicies.length > 0) {
      issues.push({
        key: "policy-rules",
        tone: "bad",
        title: t("overview.issues.policies", { count: badPolicies.length }),
        detail: summarizeEntries(
          badPolicies.map(
            (policy) =>
              `${policy.fwmark}/${policy.fwmask}: ${policy.detail ?? policy.status}`
          )
        ),
      })
    }
  }

  if (dnsmasq?.mode === "dnsmasq") {
    if (dnsmasq.state === "error") {
      issues.push({
        key: "dnsmasq",
        tone: "bad",
        title: t("overview.issues.dnsmasqError"),
        detail:
          dnsmasq.last_error ??
          dnsmasq.repair_reason ??
          (dnsmasq.dnsmasq_alive === "dead"
            ? t("overview.issues.dnsmasqDead")
            : undefined),
      })
    } else if (dnsmasq.state === "reconciling") {
      issues.push({
        key: "dnsmasq",
        tone: "warn",
        title: t("overview.issues.dnsmasqReconciling"),
        detail: dnsmasq.repair_reason ?? undefined,
      })
    }
  }

  for (const outbound of runtimeOutbounds) {
    const failingMembers = outbound.interfaces.filter(
      (member) =>
        member.status === "degraded" || member.status === "unavailable"
    )
    const isGroup = outbound.type === "urltest" || outbound.type === "icmptest"

    if (outbound.status === "unavailable" || outbound.status === "degraded") {
      issues.push({
        key: `outbound-${outbound.tag}`,
        tone: outbound.status === "unavailable" ? "bad" : "warn",
        title: t(
          outbound.status === "unavailable"
            ? "overview.issues.outboundUnavailable"
            : "overview.issues.outboundDegraded",
          { tag: outbound.tag }
        ),
        detail:
          outbound.detail ??
          (isGroup ? describeMembers(failingMembers, t) : undefined),
        href: `/outbounds/${encodeURIComponent(outbound.tag)}/edit`,
      })
      continue
    }

    // A healthy group can still contain failing children; routing works but
    // the user has less redundancy than configured.
    if (isGroup && failingMembers.length > 0) {
      issues.push({
        key: `outbound-${outbound.tag}-members`,
        tone: "warn",
        title: t("overview.issues.groupMembersFailing", {
          tag: outbound.tag,
          count: failingMembers.length,
        }),
        detail: describeMembers(failingMembers, t),
        href: `/outbounds/${encodeURIComponent(outbound.tag)}/edit`,
      })
    }
  }

  return issues
}

export function collectHealthySummaries({
  routingHealth,
  intercept,
  requestedDns,
  requestedL7,
  runtimeOutbounds,
  t,
}: {
  routingHealth?: RoutingHealthResponse
  intercept?: InterceptHealth
  requestedDns: boolean
  requestedL7: boolean
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

  if (intercept) {
    const relevant = mapKernelDiagnosticEntries(
      intercept.capabilities,
      intercept.probes ?? [],
      requestedDns,
      requestedL7
    ).filter((entry) => entry.relevant)
    if (relevant.length > 0) {
      summaries.push({
        key: "kernel",
        title: intercept.kernel_release
          ? t("overview.healthy.kernelWithRelease", {
              release: intercept.kernel_release,
            })
          : t("overview.intercept.checksTitle"),
        detail: t("overview.healthy.passed", {
          passed: relevant.filter((entry) =>
            isHealthyInterceptStatus(entry.status)
          ).length,
          total: relevant.length,
        }),
      })
    }
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

function describeMembers(
  members: RuntimeOutboundState["interfaces"],
  t: TranslateFn
) {
  return summarizeEntries(
    members.map((member) =>
      [
        member.outbound_tag,
        t(`runtime.interfaceStatus.${member.status}`).toLowerCase(),
        member.detail,
      ]
        .filter(Boolean)
        .join(" · ")
    )
  )
}

function describeRouteMismatch(table: RouteTableCheck, t: TranslateFn) {
  const issues: string[] = []
  if (!table.table_exists) {
    issues.push(t("overview.routing.issues.tableMissing"))
  }
  if (!table.default_route_present) {
    issues.push(t("overview.routing.issues.defaultRouteMissing"))
  }
  if (!table.interface_matches) {
    issues.push(t("overview.routing.issues.interfaceMismatch"))
  }
  if (!table.gateway_matches) {
    issues.push(t("overview.routing.issues.gatewayMismatch"))
  }
  if (issues.length > 0) {
    return issues.join(", ")
  }
  return table.detail ?? table.status
}

function summarizeEntries(entries: string[]) {
  const shown = entries.slice(0, MAX_DETAIL_ENTRIES).join("; ")
  const rest = entries.length - MAX_DETAIL_ENTRIES
  return rest > 0 ? `${shown}; +${rest}` : shown
}

function humanizeFeature(feature: string) {
  const text = feature.replace(/_/g, " ")
  return text.charAt(0).toUpperCase() + text.slice(1)
}
