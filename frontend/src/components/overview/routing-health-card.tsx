import { type ReactNode, useMemo, useState } from "react"
import { HeartPlus } from "lucide-react"

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
    <div className="space-y-4">
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
          chain
        </ChainStateBadge>
        <ChainStateBadge isHealthy={routingHealth.firewall.prerouting_hook_present}>
          prerouting
        </ChainStateBadge>
        <label className="ml-auto flex items-center gap-2 text-xs text-muted-foreground">
          <Checkbox
            checked={showHealthyEntries}
            onCheckedChange={(checked) => setShowHealthyEntries(checked === true)}
          />
          <span>Show healthy entries</span>
        </label>
      </div>

      {!hasVisibleEntries ? (
        <Empty className="min-h-0 rounded-lg border border-dashed px-4 py-6">
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
              {showHealthyEntries ? "No checks reported" : "Everything is good"}
            </EmptyTitle>
            <EmptyDescription>
              {showHealthyEntries
                ? "Routing health has no entries to display."
                : "No failing routing health entries right now."}
            </EmptyDescription>
          </EmptyHeader>
        </Empty>
      ) : null}

      {firewallRules.length > 0 ? (
        <CompactSection
          title="Firewall"
          items={firewallRules}
          renderItem={(rule, index) => (
            <CompactDiagnosticRow
              key={`${rule.set_name}-${index}`}
              primary={
                <>
                  <span className="font-mono text-[12px] sm:text-sm">{rule.set_name}</span>
                  <InlineMeta>{rule.action}</InlineMeta>
                  {renderFirewallMark(rule.expected_fwmark, rule.actual_fwmark)}
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
          title="Routes"
          items={groupedRoutes}
          renderItem={(group) => (
            <div className="space-y-1.5" key={group.key}>
              {group.items.map((table, index) => (
                <CompactDiagnosticRow
                  key={`${group.key}-${index}`}
                  primary={
                    <>
                      <span className="text-sm font-medium">{group.outboundTag}</span>
                      <InlineMeta>table {group.tableId}</InlineMeta>
                      <InlineMeta>{table.expected_destination ?? "default"}</InlineMeta>
                      <InlineMeta>{formatRouteExpectation(table)}</InlineMeta>
                      {renderInlineDetail(getRouteMismatchDetail(table))}
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
          title="Policies"
          items={policyRules}
          renderItem={(policy, index) => (
            <CompactDiagnosticRow
              key={`${policy.fwmark}-${policy.expected_table}-${index}`}
              primary={
                <>
                  <span className="font-mono text-[12px] sm:text-sm">
                    {policy.fwmark}/{policy.fwmask}
                  </span>
                  <InlineMeta>table {policy.expected_table}</InlineMeta>
                  <InlineMeta>priority {policy.priority}</InlineMeta>
                  <PresenceBadge label="IPv4" present={policy.rule_present_v4} />
                  <PresenceBadge label="IPv6" present={policy.rule_present_v6} />
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
    <Badge variant={isHealthy ? "success" : "warning"}>{children}</Badge>
  )
}

function PresenceBadge({
  label,
  present,
}: {
  label: string
  present: boolean
}) {
  return (
    <Badge size="xs" variant={present ? "success" : "warning"}>
      {label} {present ? "yes" : "no"}
    </Badge>
  )
}

function renderFirewallMark(expected?: string, actual?: string) {
  if (!expected && !actual) {
    return null
  }

  if (expected && actual && expected === actual) {
    return <InlineMeta>fwmark {expected}</InlineMeta>
  }

  if (expected && actual) {
    return <InlineMeta>expected {expected}, got {actual}</InlineMeta>
  }

  if (expected) {
    return <InlineMeta>fwmark {expected}</InlineMeta>
  }

  return <InlineMeta>actual {actual}</InlineMeta>
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

function formatRouteExpectation(table: RouteTableCheck) {
  const parts = [table.expected_route_type ?? "route"]

  if (table.expected_interface) {
    parts.push(`via ${table.expected_interface}`)
  }

  if (table.expected_gateway) {
    parts.push(`gw ${table.expected_gateway}`)
  }

  if (typeof table.expected_metric === "number") {
    parts.push(`metric ${table.expected_metric}`)
  }

  return parts.join(" ")
}

function getRouteMismatchDetail(table: RouteTableCheck) {
  const issues: string[] = []

  if (!table.table_exists) {
    issues.push("table missing")
  }

  if (!table.default_route_present) {
    issues.push("default route missing")
  }

  if (!table.interface_matches) {
    issues.push("interface mismatch")
  }

  if (!table.gateway_matches) {
    issues.push("gateway mismatch")
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
  if (tone === "warning") {
    return <Badge variant="warning">{children}</Badge>
  }

  if (tone === "degraded") {
    return <Badge variant="destructive">{children}</Badge>
  }

  return <Badge variant="success">{children}</Badge>
}
