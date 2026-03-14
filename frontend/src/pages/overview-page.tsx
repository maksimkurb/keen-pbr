import { type ReactNode, useEffect, useMemo, useState } from "react"
import { Play, RotateCw, Square } from "lucide-react"

import { useGetHealthRouting, useGetHealthService } from "@/api/queries"
import {
  usePostReloadMutation,
  usePostRoutingTestMutation,
} from "@/api/mutations"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Alert, AlertDescription } from "@/components/ui/alert"
import {
  Empty,
  EmptyDescription,
  EmptyHeader,
  EmptyTitle,
} from "@/components/ui/empty"
import { Input } from "@/components/ui/input"
import { Skeleton } from "@/components/ui/skeleton"
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip"
import { ButtonGroup } from "@/components/shared/button-group"
import { DataTable } from "@/components/shared/data-table"
import { SectionCard } from "@/components/shared/section-card"

const unsupportedActionReason = "Not available in current API"

export function OverviewPage() {
  const [testTarget, setTestTarget] = useState("example.com")

  const serviceHealthQuery = useGetHealthService({
    query: {
      refetchInterval: 30_000,
      refetchIntervalInBackground: false,
    },
  })
  const routingHealthQuery = useGetHealthRouting({
    query: {
      refetchInterval: 45_000,
      refetchIntervalInBackground: false,
    },
  })

  const postReloadMutation = usePostReloadMutation()
  const routingTestMutation = usePostRoutingTestMutation()

  const dnsEvents = useDnsTestEvents()

  const serviceHealth = serviceHealthQuery.data?.data
  const routingHealth = routingHealthQuery.data?.data

  const outboundRows = useMemo(() => {
    if (!serviceHealth) {
      return []
    }

    return serviceHealth.outbounds.map((outbound) => {
      const childRows = outbound.children?.length ? (
        <UrltestTagTree
          children={outbound.children}
          selectedOutbound={outbound.selected_outbound}
        />
      ) : (
        outbound.tag
      )

      return [
        childRows,
        outbound.type,
        outbound.children?.length
          ? `${outbound.children.length} children`
          : "-",
        outbound.selected_outbound ?? "-",
        <StatusBadge
          key={`${outbound.tag}-status`}
          tone={mapHealthTone(outbound.status)}
        >
          {outbound.status}
        </StatusBadge>,
      ]
    })
  }, [serviceHealth])

  const routingRows = useMemo(() => {
    if (!routingHealth) {
      return []
    }

    const firewallRows = routingHealth.firewall_rules.map((rule, index) => [
      `Firewall #${index + 1}`,
      `set ${rule.set_name}`,
      rule.action,
      rule.expected_fwmark ?? "-",
      rule.actual_fwmark ?? "-",
      rule.detail ?? "-",
      <StatusBadge key={`fw-${index}`} tone={mapCheckTone(rule.status)}>
        {rule.status}
      </StatusBadge>,
    ])

    const routeRows = routingHealth.route_tables.map((table) => [
      `Route table ${table.table_id}`,
      table.outbound_tag,
      table.expected_destination ?? "default",
      table.expected_interface ?? "-",
      table.expected_gateway ?? "-",
      table.detail ?? "-",
      <StatusBadge
        key={`route-${table.table_id}`}
        tone={mapCheckTone(table.status)}
      >
        {table.status}
      </StatusBadge>,
    ])

    const policyRows = routingHealth.policy_rules.map((policy, index) => [
      `Policy #${index + 1}`,
      `fwmark ${policy.fwmark}/${policy.fwmask}`,
      `table ${policy.expected_table}`,
      `priority ${policy.priority}`,
      `v4:${policy.rule_present_v4 ? "yes" : "no"} v6:${policy.rule_present_v6 ? "yes" : "no"}`,
      policy.detail ?? "-",
      <StatusBadge key={`policy-${index}`} tone={mapCheckTone(policy.status)}>
        {policy.status}
      </StatusBadge>,
    ])

    return [...firewallRows, ...routeRows, ...policyRows]
  }, [routingHealth])

  const firstRoutingTestResult = routingTestMutation.data?.data.results[0]

  return (
    <div className="space-y-6">
      <div className="grid gap-4 lg:grid-cols-2">
        <SectionCard className="col-span-2 lg:col-span-1" title="keen-pbr">
          {serviceHealthQuery.isLoading ? <ServiceSummarySkeleton /> : null}

          {serviceHealthQuery.isError ? (
            <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
              <AlertDescription>
                Failed to load service health.
              </AlertDescription>
            </Alert>
          ) : null}

          {serviceHealth ? (
            <>
              <div className="grid gap-4 md:grid-cols-2">
                <div>
                  <div className="mb-1 text-sm text-muted-foreground">
                    Version
                  </div>
                  <div className="text-lg font-semibold">
                    {serviceHealth.version}
                  </div>
                </div>
                <div>
                  <div className="mb-1 text-sm text-muted-foreground">
                    Service status
                  </div>
                  <div className="flex items-center gap-2">
                    <StatusBadge
                      tone={mapServiceStatusTone(serviceHealth.status)}
                    >
                      {serviceHealth.status}
                    </StatusBadge>
                    {serviceHealth.resolver_config_hash ? (
                      <Badge variant="outline" className="font-mono text-xs">
                        {serviceHealth.resolver_config_hash.slice(0, 10)}…
                      </Badge>
                    ) : null}
                  </div>
                </div>
              </div>

              <ButtonGroup className="mt-2 [&>[data-slot=button]]:flex-1">
                <DisabledActionButton
                  icon={<Play className="mr-1 h-3 w-3" />}
                  label="Start"
                />
                <DisabledActionButton
                  icon={<Square className="mr-1 h-3 w-3" />}
                  label="Stop"
                />
                <DisabledActionButton
                  icon={<RotateCw className="mr-1 h-3 w-3" />}
                  label="Restart"
                />
                <Button
                  size="sm"
                  variant="outline"
                  disabled={postReloadMutation.isPending}
                  onClick={() => postReloadMutation.mutate()}
                >
                  <RotateCw className="mr-1 h-3 w-3" />
                  Reload
                </Button>
              </ButtonGroup>
            </>
          ) : null}
        </SectionCard>

        <SectionCard
          className="col-span-2 lg:col-span-1"
          title="DNS server self-check"
        >
          <div className="rounded-md border p-3">
            {dnsEvents.length === 0 ? (
              <div className="text-sm text-muted-foreground">
                Waiting for DNS test events…
              </div>
            ) : (
              <ul className="space-y-1 text-sm">
                {dnsEvents.map((event, index) => (
                  <li
                    key={`${event}-${index}`}
                    className="font-mono text-xs md:text-sm"
                  >
                    {event}
                  </li>
                ))}
              </ul>
            )}
          </div>

          <ButtonGroup className="mt-2 [&>[data-slot=button]]:flex-1">
            <Button size="sm" variant="outline" disabled>
              Run again
            </Button>
            <Button size="sm" variant="outline" disabled>
              Test from PC
            </Button>
          </ButtonGroup>
        </SectionCard>
      </div>

      <SectionCard title="Outbounds health">
        {serviceHealthQuery.isLoading ? <TableSkeleton /> : null}
        {serviceHealthQuery.isError ? (
          <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
            <AlertDescription>Unable to load outbound health.</AlertDescription>
          </Alert>
        ) : null}
        {serviceHealth && outboundRows.length === 0 ? (
          <Empty className="border">
            <EmptyHeader>
              <EmptyTitle>No outbounds configured</EmptyTitle>
              <EmptyDescription>
                Add outbounds to see health checks.
              </EmptyDescription>
            </EmptyHeader>
          </Empty>
        ) : null}
        {outboundRows.length > 0 ? (
          <DataTable
            headers={["Tag", "Type", "Children", "Selected", "Status"]}
            rows={outboundRows}
          />
        ) : null}
      </SectionCard>

      <SectionCard title="Routing rule health">
        {routingHealthQuery.isLoading ? <TableSkeleton /> : null}
        {routingHealthQuery.isError ? (
          <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
            <AlertDescription>Unable to load routing checks.</AlertDescription>
          </Alert>
        ) : null}
        {routingHealth && routingRows.length === 0 ? (
          <Empty className="border">
            <EmptyHeader>
              <EmptyTitle>No routing checks available</EmptyTitle>
              <EmptyDescription>
                Routing checks will appear after reload.
              </EmptyDescription>
            </EmptyHeader>
          </Empty>
        ) : null}
        {routingRows.length > 0 ? (
          <DataTable
            headers={[
              "Check",
              "Expected",
              "Observed",
              "Field 1",
              "Field 2",
              "Detail",
              "Status",
            ]}
            rows={routingRows}
          />
        ) : null}
      </SectionCard>

      <SectionCard title="Domain/IP routing test">
        <form
          className="space-y-3"
          onSubmit={(event) => {
            event.preventDefault()
            routingTestMutation.mutate({ data: { target: testTarget.trim() } })
          }}
        >
          <ButtonGroup>
            <Input
              className="min-w-0 flex-1 rounded-none border-0 shadow-none focus-visible:ring-0"
              onChange={(event) => setTestTarget(event.target.value)}
              value={testTarget}
            />
            <Button
              className="whitespace-nowrap"
              disabled={routingTestMutation.isPending || !testTarget.trim()}
            >
              Run routing test
            </Button>
          </ButtonGroup>
        </form>

        {routingTestMutation.isPending ? (
          <div className="space-y-2">
            <Skeleton className="h-4 w-2/3" />
            <Skeleton className="h-4 w-1/2" />
          </div>
        ) : null}

        {routingTestMutation.isError ? (
          <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
            <AlertDescription>
              Routing test failed. Please try again.
            </AlertDescription>
          </Alert>
        ) : null}

        {routingTestMutation.isSuccess && !firstRoutingTestResult ? (
          <Empty className="border">
            <EmptyHeader>
              <EmptyTitle>No route matched</EmptyTitle>
              <EmptyDescription>
                Try another domain or IP address.
              </EmptyDescription>
            </EmptyHeader>
          </Empty>
        ) : null}

        {firstRoutingTestResult ? (
          <div className="text-sm text-muted-foreground">
            Expected outbound: {firstRoutingTestResult.expected_outbound} ·
            Actual outbound: {firstRoutingTestResult.actual_outbound} · Matched
            lists:{" "}
            {firstRoutingTestResult.list_match
              ? `${firstRoutingTestResult.list_match.list} via ${firstRoutingTestResult.list_match.via}`
              : "none"}
          </div>
        ) : null}
      </SectionCard>
    </div>
  )
}

function useDnsTestEvents(maxEvents = 8) {
  const [events, setEvents] = useState<string[]>([])

  useEffect(() => {
    const eventSource = new EventSource("/api/dns/test")

    eventSource.onmessage = (event) => {
      const message = event.data?.trim()
      if (!message) {
        return
      }

      setEvents((previous) => [message, ...previous].slice(0, maxEvents))
    }

    eventSource.onerror = () => {
      setEvents((previous) =>
        ["DNS monitor disconnected", ...previous].slice(0, maxEvents)
      )
      eventSource.close()
    }

    return () => {
      eventSource.close()
    }
  }, [maxEvents])

  return events
}

function ServiceSummarySkeleton() {
  return (
    <div className="grid gap-4 md:grid-cols-2">
      <div className="space-y-2">
        <Skeleton className="h-4 w-20" />
        <Skeleton className="h-7 w-28" />
      </div>
      <div className="space-y-2">
        <Skeleton className="h-4 w-28" />
        <Skeleton className="h-7 w-32" />
      </div>
    </div>
  )
}

function TableSkeleton() {
  return (
    <div className="space-y-2">
      <Skeleton className="h-10 w-full" />
      <Skeleton className="h-10 w-full" />
      <Skeleton className="h-10 w-full" />
    </div>
  )
}

function DisabledActionButton({
  label,
  icon,
}: {
  label: string
  icon: ReactNode
}) {
  return (
    <Tooltip>
      <TooltipTrigger
        render={
          <span tabIndex={0}>
            <Button disabled size="sm" variant="outline">
              {icon}
              {label}
            </Button>
          </span>
        }
      />
      <TooltipContent>{unsupportedActionReason}</TooltipContent>
    </Tooltip>
  )
}

function mapServiceStatusTone(
  status: string
): "healthy" | "warning" | "degraded" {
  if (status === "running") {
    return "healthy"
  }

  if (status === "starting" || status === "reloading") {
    return "warning"
  }

  return "degraded"
}

function mapHealthTone(status: string): "healthy" | "warning" | "degraded" {
  if (status === "healthy") {
    return "healthy"
  }

  if (status === "unknown") {
    return "warning"
  }

  return "degraded"
}

function mapCheckTone(status: string): "healthy" | "warning" | "degraded" {
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
  tone: "healthy" | "warning" | "degraded"
  children: string
}) {
  if (tone === "warning") {
    return (
      <Badge className="bg-warning text-warning-foreground">{children}</Badge>
    )
  }

  if (tone === "degraded") {
    return (
      <Badge className="bg-destructive text-destructive-foreground">
        {children}
      </Badge>
    )
  }

  return (
    <Badge className="bg-success text-success-foreground">{children}</Badge>
  )
}

function UrltestTagTree({
  children,
  selectedOutbound,
}: {
  children: Array<{
    tag: string
    success: boolean
    latency_ms: number
  }>
  selectedOutbound?: string
}) {
  return (
    <div className="space-y-2">
      <div className="font-medium">urltest</div>
      <div>
        {children.map((child, index) => (
          <UrltestOutboundRow
            key={child.tag}
            active={child.tag === selectedOutbound}
            isLast={index === children.length - 1}
            latency={
              typeof child.latency_ms === "number"
                ? `${child.latency_ms} ms`
                : "-"
            }
            name={child.outbound_tag}
            state={child.healthy ? "healthy" : "degraded"}
          />
        ))}
      </div>
    </div>
  )
}

function UrltestOutboundRow({
  name,
  latency,
  state,
  active = false,
  isLast = false,
}: {
  name: string
  latency: string
  state: "healthy" | "degraded"
  active?: boolean
  isLast?: boolean
}) {
  return (
    <div className="ml-1 flex flex-wrap items-center text-base md:text-sm">
      <TreeConnector isLast={isLast} />
      <span
        className={`relative ml-2 inline-flex size-2 rounded-full ${
          state === "healthy" ? "bg-success" : "bg-destructive"
        }`}
      />
      <span className="ml-2 font-medium">{name}</span>
      <span className="ml-2 text-muted-foreground">{latency}</span>
      {active ? (
        <Badge className="ml-2" variant="outline">
          In use
        </Badge>
      ) : null}
    </div>
  )
}

function TreeConnector({ isLast }: { isLast: boolean }) {
  return (
    <svg
      aria-hidden="true"
      className="mr-0.5 h-full shrink-0 self-stretch"
      preserveAspectRatio="none"
      viewBox="0 0 16 24"
      width="16"
    >
      {isLast ? (
        <path
          d="M2 0V12H14"
          fill="none"
          stroke="currentColor"
          strokeOpacity="0.3"
          strokeWidth="1"
        />
      ) : (
        <path
          d="M2 0V24M2 12H14"
          fill="none"
          stroke="currentColor"
          strokeOpacity="0.3"
          strokeWidth="1"
        />
      )}
    </svg>
  )
}
