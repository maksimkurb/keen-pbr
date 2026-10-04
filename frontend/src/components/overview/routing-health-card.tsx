import { type ReactNode, useMemo } from "react"
import { HeartPlus } from "lucide-react"
import { useTranslation } from "react-i18next"

import type {
  RouteTableCheck,
  RoutingHealthResponse,
} from "@/api/generated/model"
import { Badge } from "@/components/ui/badge"
import {
  getKernelBadgeState,
  getVisibleInterceptDiagnosticEntries,
  type KernelDiagnosticEntry,
} from "@/lib/intercept-diagnostics"
import {
  Empty,
  EmptyDescription,
  EmptyHeader,
  EmptyMedia,
  EmptyTitle,
} from "@/components/ui/empty"

type StatusTone = "healthy" | "warning" | "degraded" | "neutral"

export type KernelDiagnostics = {
  release?: string
  entries: KernelDiagnosticEntry[]
}

export function RoutingHealthCard({
  routingHealth,
  kernel,
  showHealthyEntries = false,
}: {
  routingHealth?: RoutingHealthResponse
  kernel?: KernelDiagnostics
  showHealthyEntries?: boolean
}) {
  const { t } = useTranslation()

  const firewallRules = useMemo(
    () =>
      filterByHealth(routingHealth?.firewall_rules ?? [], showHealthyEntries),
    [routingHealth?.firewall_rules, showHealthyEntries]
  )
  const routeTables = useMemo(
    () => filterByHealth(routingHealth?.route_tables ?? [], showHealthyEntries),
    [routingHealth?.route_tables, showHealthyEntries]
  )
  const policyRules = useMemo(
    () => filterByHealth(routingHealth?.policy_rules ?? [], showHealthyEntries),
    [routingHealth?.policy_rules, showHealthyEntries]
  )
  const kernelEntries = useMemo(
    () =>
      getVisibleInterceptDiagnosticEntries(
        kernel?.entries ?? [],
        showHealthyEntries
      ),
    [kernel?.entries, showHealthyEntries]
  )
  const kernelBadgeState = kernel
    ? getKernelBadgeState(kernel.entries)
    : undefined

  const groupedRoutes = useMemo(
    () => groupRouteTables(routeTables),
    [routeTables]
  )
  const hasVisibleEntries =
    kernelEntries.length > 0 ||
    firewallRules.length > 0 ||
    groupedRoutes.length > 0 ||
    policyRules.length > 0

  return (
    <div className="flex flex-1 flex-col space-y-4">
      <div className="flex flex-wrap items-center gap-2">
        {routingHealth ? (
          <>
            <StatusBadge tone={mapCheckTone(routingHealth.overall)}>
              {routingHealth.overall}
            </StatusBadge>
            <Badge size="xs" variant="outline">
              {routingHealth.firewall_backend}
            </Badge>
            <ChainStateBadge isHealthy={routingHealth.firewall.chain_present}>
              {t("overview.routing.chain")}
            </ChainStateBadge>
            <ChainStateBadge
              isHealthy={routingHealth.firewall.prerouting_hook_present}
            >
              {t("overview.routing.prerouting")}
            </ChainStateBadge>
          </>
        ) : null}
        {kernelBadgeState ? (
          <span
            data-testid="kernel-badge"
            data-state={kernelBadgeState}
            title={
              kernel?.release
                ? t("overview.intercept.probes.kernel", {
                    release: kernel.release,
                  })
                : undefined
            }
          >
            <StatusBadge tone={kernelBadgeState}>
              {t("overview.routing.kernel")}
            </StatusBadge>
          </span>
        ) : null}
      </div>

      {!hasVisibleEntries && routingHealth ? (
        <Empty className="min-h-0 flex-1 rounded-lg border border-dashed px-4 py-6">
          <EmptyHeader>
            {!showHealthyEntries ? (
              <EmptyMedia
                variant="icon"
                className="bg-success/10 text-success [&_svg:not([class*='size-'])]:size-5"
              >
                <HeartPlus />
              </EmptyMedia>
            ) : null}
            <EmptyTitle>
              {showHealthyEntries
                ? t("overview.routing.noChecksTitle")
                : t("overview.routing.allHealthyTitle")}
            </EmptyTitle>
            <EmptyDescription>
              {showHealthyEntries
                ? t("overview.routing.noChecksDescription")
                : t("overview.routing.allHealthyDescription")}
            </EmptyDescription>
          </EmptyHeader>
        </Empty>
      ) : null}

      {kernelEntries.length > 0 ? (
        <CompactSection
          title={t("overview.intercept.checksTitle")}
          suffix={
            kernel?.release
              ? t("overview.intercept.probes.kernel", {
                  release: kernel.release,
                })
              : undefined
          }
          items={kernelEntries}
          renderItem={(entry) => (
            <CompactDiagnosticRow
              key={entry.key}
              primary={
                entry.kind === "capability" ? (
                  <span className="font-medium">
                    {t(`overview.intercept.capabilities.${entry.feature}`)}
                  </span>
                ) : (
                  <>
                    <span className="font-medium">{entry.feature}</span>
                    {entry.reason ? (
                      <InlineMeta>{entry.reason}</InlineMeta>
                    ) : null}
                  </>
                )
              }
              status={entry.status}
              statusLabel={
                entry.kind === "capability"
                  ? entry.status === "ok"
                    ? t("overview.intercept.supported")
                    : t("overview.intercept.unsupported")
                  : t(`overview.intercept.probes.status.${entry.status}`)
              }
            />
          )}
        />
      ) : null}

      {firewallRules.length > 0 ? (
        <CompactSection
          title={t("overview.routing.sections.firewall")}
          items={firewallRules}
          renderItem={(rule, index) => (
            <CompactDiagnosticRow
              key={`${rule.set_name}-${index}`}
              primary={
                <>
                  <span className="font-mono text-[12px] sm:text-sm">
                    {rule.set_name}
                  </span>
                  <InlineMeta>{rule.action}</InlineMeta>
                  {renderFirewallMark(
                    rule.expected_fwmark,
                    rule.actual_fwmark,
                    t
                  )}
                  {renderInlineDetail(
                    getDiagnosticDetail(rule.status, rule.detail)
                  )}
                </>
              }
              status={rule.status}
            />
          )}
        />
      ) : null}

      {groupedRoutes.length > 0 ? (
        <CompactSection
          title={t("overview.routing.sections.routes")}
          items={groupedRoutes}
          renderItem={(group) => (
            <div className="space-y-1.5" key={group.key}>
              {group.items.map((table, index) => (
                <CompactDiagnosticRow
                  key={`${group.key}-${index}`}
                  primary={
                    <>
                      <span className="text-sm font-medium">
                        {group.outboundTag}
                      </span>
                      <InlineMeta>
                        {t("overview.routing.tableLabel", {
                          value: group.tableId,
                        })}
                      </InlineMeta>
                      <InlineMeta>
                        {table.expected_destination ??
                          t("overview.routing.defaultRoute")}
                      </InlineMeta>
                      <InlineMeta>
                        {formatRouteExpectation(table, t)}
                      </InlineMeta>
                      {renderInlineDetail(getRouteMismatchDetail(table, t))}
                    </>
                  }
                  status={table.status}
                />
              ))}
            </div>
          )}
        />
      ) : null}

      {policyRules.length > 0 ? (
        <CompactSection
          title={t("overview.routing.sections.policies")}
          items={policyRules}
          renderItem={(policy, index) => (
            <CompactDiagnosticRow
              key={`${policy.fwmark}-${policy.expected_table}-${index}`}
              primary={
                <>
                  <span className="font-mono text-[12px] sm:text-sm">
                    {policy.fwmark}/{policy.fwmask}
                  </span>
                  <InlineMeta>
                    {t("overview.routing.tableLabel", {
                      value: policy.expected_table,
                    })}
                  </InlineMeta>
                  <InlineMeta>
                    {t("overview.routing.priorityLabel", {
                      value: policy.priority,
                    })}
                  </InlineMeta>
                  <PresenceBadge
                    label={t("overview.routing.ipv4")}
                    present={policy.rule_present_v4}
                    yesLabel={t("overview.routing.yes")}
                    noLabel={t("overview.routing.no")}
                  />
                  <PresenceBadge
                    label={t("overview.routing.ipv6")}
                    present={policy.rule_present_v6}
                    yesLabel={t("overview.routing.yes")}
                    noLabel={t("overview.routing.no")}
                  />
                  {renderInlineDetail(
                    getDiagnosticDetail(policy.status, policy.detail)
                  )}
                </>
              }
              status={policy.status}
            />
          )}
        />
      ) : null}
    </div>
  )
}

function CompactSection<T>({
  title,
  suffix,
  items,
  renderItem,
}: {
  title: string
  suffix?: string
  items: T[]
  renderItem: (item: T, index: number) => ReactNode
}) {
  return (
    <section className="space-y-2">
      <div className="flex items-center justify-between gap-2">
        <h3 className="text-sm font-semibold">{title}</h3>
        <span className="text-xs text-muted-foreground">
          {suffix ? `${suffix} · ` : ""}
          {items.length}
        </span>
      </div>
      <div className="space-y-2">{items.map(renderItem)}</div>
    </section>
  )
}

export function CompactDiagnosticRow({
  primary,
  status,
  statusLabel,
}: {
  primary: ReactNode
  status: string
  statusLabel?: ReactNode
}) {
  return (
    <div className="rounded-md border border-border/70 bg-muted/20 px-3 py-1.5">
      <div className="flex items-center justify-between gap-3">
        <div className="flex min-w-0 flex-wrap items-center gap-x-2 gap-y-1 text-xs text-muted-foreground">
          {primary}
        </div>
        <StatusBadge tone={mapCheckTone(status)}>
          {statusLabel ?? status}
        </StatusBadge>
      </div>
    </div>
  )
}

export function InlineMeta({ children }: { children: ReactNode }) {
  return <span className="text-xs text-muted-foreground">{children}</span>
}

function ChainStateBadge({
  isHealthy,
  children,
}: {
  isHealthy: boolean
  children: ReactNode
}) {
  return (
    <Badge size="xs" variant={isHealthy ? "success" : "warning"}>
      {children}
    </Badge>
  )
}

function PresenceBadge({
  label,
  present,
  yesLabel,
  noLabel,
}: {
  label: string
  present: boolean
  yesLabel: string
  noLabel: string
}) {
  return (
    <Badge size="xs" variant={present ? "success" : "warning"}>
      {label} {present ? yesLabel : noLabel}
    </Badge>
  )
}

function renderFirewallMark(
  expected: string | undefined,
  actual: string | undefined,
  t: (key: string, options?: Record<string, unknown>) => string
) {
  if (!expected && !actual) {
    return null
  }

  if (expected && actual && expected === actual) {
    return (
      <InlineMeta>
        {t("overview.routing.fwmarkLabel", { value: expected })}
      </InlineMeta>
    )
  }

  if (expected && actual) {
    return (
      <InlineMeta>
        {t("overview.routing.fwmarkExpectedActual", { expected, actual })}
      </InlineMeta>
    )
  }

  if (expected) {
    return (
      <InlineMeta>
        {t("overview.routing.fwmarkLabel", { value: expected })}
      </InlineMeta>
    )
  }

  return (
    <InlineMeta>
      {t("overview.routing.actualLabel", { value: actual })}
    </InlineMeta>
  )
}

function renderInlineDetail(detail?: string | null) {
  if (!detail) {
    return null
  }

  return <InlineMeta>{detail}</InlineMeta>
}

function groupRouteTables(routeTables: RouteTableCheck[]) {
  const groups = new Map<
    string,
    {
      key: string
      tableId: number
      outboundTag: string
      items: RouteTableCheck[]
    }
  >()

  routeTables.forEach((table) => {
    const key = `${table.table_id}:${table.outbound_tag}`
    const existing = groups.get(key)

    if (existing) {
      existing.items.push(table)
      return
    }

    groups.set(key, {
      key,
      tableId: table.table_id,
      outboundTag: table.outbound_tag,
      items: [table],
    })
  })

  return Array.from(groups.values())
}

function filterByHealth<T extends { status: string }>(
  items: T[],
  showHealthyEntries: boolean
) {
  if (showHealthyEntries) {
    return items
  }

  return items.filter((item) => item.status !== "ok")
}

function formatRouteExpectation(
  table: RouteTableCheck,
  t: (key: string, options?: Record<string, unknown>) => string
) {
  const parts = [
    table.expected_route_type ?? t("overview.routing.routeTypeFallback"),
  ]

  if (table.expected_interface) {
    parts.push(
      t("overview.routing.routeVia", { value: table.expected_interface })
    )
  }

  if (table.expected_gateway) {
    parts.push(
      t("overview.routing.routeGateway", { value: table.expected_gateway })
    )
  }

  if (typeof table.expected_metric === "number") {
    parts.push(
      t("overview.routing.routeMetric", { value: table.expected_metric })
    )
  }

  return parts.join(" ")
}

function getRouteMismatchDetail(
  table: RouteTableCheck,
  t: (key: string, options?: Record<string, unknown>) => string
) {
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

  return getDiagnosticDetail(table.status, table.detail)
}

function getDiagnosticDetail(status: string, detail?: string | null) {
  if (!detail || detail === "ok") {
    return status === "ok" ? null : null
  }

  return detail
}

function mapCheckTone(status: string): StatusTone {
  if (status === "ok") {
    return "healthy"
  }

  if (status === "missing") {
    return "warning"
  }

  if (status === "not_run" || status === "skipped") {
    return "neutral"
  }

  return "degraded"
}

function StatusBadge({
  tone,
  children,
}: {
  tone: StatusTone
  children: ReactNode
}) {
  return (
    <Badge
      size="xs"
      variant={
        tone === "warning"
          ? "warning"
          : tone === "degraded"
            ? "destructive"
            : tone === "neutral"
              ? "secondary"
              : "success"
      }
    >
      {children}
    </Badge>
  )
}
