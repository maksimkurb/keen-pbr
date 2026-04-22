import { type ReactNode, useMemo, useState } from "react"
import { HeartPlus } from "lucide-react"
import { useTranslation } from "react-i18next"

import type {
  RouteTableCheck,
  RoutingHealthResponse,
} from "@/api/generated/model"
import { Badge } from "@/components/ui/badge"
import { Checkbox } from "@/components/ui/checkbox"
import {
  Empty,
  EmptyDescription,
  EmptyHeader,
  EmptyMedia,
  EmptyTitle,
} from "@/components/ui/empty"

type StatusTone = "healthy" | "warning" | "degraded"

export function RoutingHealthCard({
  routingHealth,
}: {
  routingHealth: RoutingHealthResponse
}) {
  const { t } = useTranslation()
  const [showHealthyEntries, setShowHealthyEntries] = useState(false)

  const firewallRules = useMemo(
    () =>
      filterByHealth(routingHealth.firewall_rules ?? [], showHealthyEntries),
    [routingHealth.firewall_rules, showHealthyEntries]
  )
  const routeTables = useMemo(
    () => filterByHealth(routingHealth.route_tables ?? [], showHealthyEntries),
    [routingHealth.route_tables, showHealthyEntries]
  )
  const policyRules = useMemo(
    () => filterByHealth(routingHealth.policy_rules ?? [], showHealthyEntries),
    [routingHealth.policy_rules, showHealthyEntries]
  )

  const groupedRoutes = useMemo(() => groupRouteTables(routeTables), [routeTables])
  const hasVisibleEntries =
    firewallRules.length > 0 || groupedRoutes.length > 0 || policyRules.length > 0

  return (
    <div className="flex flex-1 flex-col space-y-4">
      <div className="flex flex-wrap items-center gap-2">
        <StatusBadge tone={mapCheckTone(routingHealth.overall)}>
          {routingHealth.overall}
        </StatusBadge>
        <Badge size="xs" variant="outline">
          {routingHealth.firewall_backend}
        </Badge>
        <ChainStateBadge
          isHealthy={routingHealth.firewall.chain_present}
        >
          {t("overview.routing.chain")}
        </ChainStateBadge>
        <ChainStateBadge isHealthy={routingHealth.firewall.prerouting_hook_present}>
          {t("overview.routing.prerouting")}
        </ChainStateBadge>
        <label className="ml-auto flex items-center gap-2 text-xs text-muted-foreground">
          <Checkbox
            checked={showHealthyEntries}
            onCheckedChange={(checked) => setShowHealthyEntries(checked === true)}
          />
          <span>{t("overview.routing.showHealthyEntries")}</span>
        </label>
      </div>

      {!hasVisibleEntries ? (
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

      {firewallRules.length > 0 ? (
        <CompactSection
          title={t("overview.routing.sections.firewall")}
          items={firewallRules}
          renderItem={(rule, index) => (
            <CompactDiagnosticRow
              key={`${rule.set_name}-${index}`}
              primary={
                <>
                  <span className="font-mono text-[12px] sm:text-sm">{rule.set_name}</span>
                  <InlineMeta>{rule.action}</InlineMeta>
                  {renderFirewallMark(rule.expected_fwmark, rule.actual_fwmark, t)}
                  {renderInlineDetail(getDiagnosticDetail(rule.status, rule.detail))}
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
                      <span className="text-sm font-medium">{group.outboundTag}</span>
                      <InlineMeta>
                        {t("overview.routing.tableLabel", { value: group.tableId })}
                      </InlineMeta>
                      <InlineMeta>
                        {table.expected_destination ??
                          t("overview.routing.defaultRoute")}
                      </InlineMeta>
                      <InlineMeta>{formatRouteExpectation(table, t)}</InlineMeta>
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
                    {t("overview.routing.tableLabel", { value: policy.expected_table })}
                  </InlineMeta>
                  <InlineMeta>
                    {t("overview.routing.priorityLabel", { value: policy.priority })}
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
                  {renderInlineDetail(getDiagnosticDetail(policy.status, policy.detail))}
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
  items,
  renderItem,
}: {
  title: string
  items: T[]
  renderItem: (item: T, index: number) => ReactNode
}) {
  return (
    <section className="space-y-2">
      <div className="flex items-center justify-between gap-2">
        <h3 className="text-sm font-semibold">{title}</h3>
        <span className="text-xs text-muted-foreground">{items.length}</span>
      </div>
      <div className="space-y-2">{items.map(renderItem)}</div>
    </section>
  )
}

function CompactDiagnosticRow({
  primary,
  status,
}: {
  primary: ReactNode
  status: string
}) {
  return (
    <div className="rounded-md border border-border/70 bg-muted/20 px-3 py-1.5">
      <div className="flex items-center justify-between gap-3">
        <div className="min-w-0 flex flex-wrap items-center gap-x-2 gap-y-1 text-xs text-muted-foreground">
          {primary}
        </div>
        <StatusBadge tone={mapCheckTone(status)}>{status}</StatusBadge>
      </div>
    </div>
  )
}

function InlineMeta({ children }: { children: ReactNode }) {
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
    <Badge size="xs" variant={isHealthy ? "success" : "warning"}>{children}</Badge>
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
    return <InlineMeta>{t("overview.routing.fwmarkLabel", { value: expected })}</InlineMeta>
  }

  if (expected && actual) {
    return (
      <InlineMeta>
        {t("overview.routing.fwmarkExpectedActual", { expected, actual })}
      </InlineMeta>
    )
  }

  if (expected) {
    return <InlineMeta>{t("overview.routing.fwmarkLabel", { value: expected })}</InlineMeta>
  }

  return <InlineMeta>{t("overview.routing.actualLabel", { value: actual })}</InlineMeta>
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
    { key: string; tableId: number; outboundTag: string; items: RouteTableCheck[] }
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
  const parts = [table.expected_route_type ?? t("overview.routing.routeTypeFallback")]

  if (table.expected_interface) {
    parts.push(t("overview.routing.routeVia", { value: table.expected_interface }))
  }

  if (table.expected_gateway) {
    parts.push(t("overview.routing.routeGateway", { value: table.expected_gateway }))
  }

  if (typeof table.expected_metric === "number") {
    parts.push(t("overview.routing.routeMetric", { value: table.expected_metric }))
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

  return "degraded"
}

function StatusBadge({
  tone,
  children,
}: {
  tone: StatusTone
  children: string
}) {
  return (
    <Badge
      size="xs"
      variant={
        tone === "warning"
          ? "warning"
          : tone === "degraded"
            ? "destructive"
            : "success"
      }
    >
      {children}
    </Badge>
  )
}
