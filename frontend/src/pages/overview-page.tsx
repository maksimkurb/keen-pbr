import { type ReactNode, useMemo } from "react"
import {
  Play,
  RotateCw,
  Square,
} from "lucide-react"

import type { ApiError } from "@/api/client"
import type { ConfigObject, HealthEntry, Outbound } from "@/api/generated/model"
import { useGetConfig, useGetHealthRouting, useGetHealthService } from "@/api/queries"
import { usePostReloadMutation } from "@/api/mutations"
import { selectConfig } from "@/api/selectors"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Alert, AlertDescription } from "@/components/ui/alert"
import {
  Empty,
  EmptyDescription,
  EmptyHeader,
  EmptyTitle,
} from "@/components/ui/empty"
import { Skeleton } from "@/components/ui/skeleton"
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip"
import { ButtonGroup } from "@/components/shared/button-group"
import { DataTable } from "@/components/shared/data-table"
import { SectionCard } from "@/components/shared/section-card"
import { RoutingHealthCard } from "@/components/overview/routing-health-card"
import { DnsCheckWidget } from "@/components/overview/dns-check-widget"
import { RoutingTestPanel } from "@/components/overview/routing-test-panel"
import { getApiErrorMessage } from "@/lib/api-errors"

const unsupportedActionReason = "Not available in current API"

export function OverviewPage() {
  const serviceHealthQuery = useGetHealthService({
    query: {
      refetchInterval: 30_000,
      refetchIntervalInBackground: false,
    },
  })
  const configQuery = useGetConfig()
  const routingHealthQuery = useGetHealthRouting({
    query: {
      refetchInterval: 45_000,
      refetchIntervalInBackground: false,
    },
  })

  const postReloadMutation = usePostReloadMutation()

  const serviceHealth =
    serviceHealthQuery.data?.status === 200
      ? serviceHealthQuery.data.data
      : undefined
  const loadedConfig = selectConfig(configQuery.data)
  const routingHealth =
    routingHealthQuery.data?.status === 200
      ? routingHealthQuery.data.data
      : undefined

  const outboundRows = useMemo(() => {
    if (!serviceHealth) {
      return []
    }

    return serviceHealth.outbounds.map((outbound) => {
      const configuredOutbound = findConfiguredOutbound(loadedConfig, outbound.tag)
      const tagCell = outbound.children?.length ? (
        <UrltestTagTree
          children={outbound.children}
          selectedOutbound={outbound.selected_outbound}
        />
      ) : (
        <div className="flex flex-wrap items-center gap-2">
          <span>{outbound.tag}</span>
          <Badge variant="outline" className="text-[11px]">
            {outbound.type}
          </Badge>
        </div>
      )

      return [
        tagCell,
        <OutboundDestinationCell
          key={`${outbound.tag}-destination`}
          configuredOutbound={configuredOutbound}
          outbound={outbound}
        />,
        <StatusBadge
          key={`${outbound.tag}-status`}
          tone={mapHealthTone(outbound.status)}
        >
          {outbound.status}
        </StatusBadge>,
      ]
    })
  }, [loadedConfig, serviceHealth])

  const routingHealthErrorMessage = routingHealthQuery.isError
    ? getRoutingHealthErrorMessage(routingHealthQuery.error)
    : null

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

        <DnsCheckWidget dnsServers={loadedConfig?.dns?.servers ?? []} />
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
            compact
            headers={["Tag", "Destination", "Status"]}
            rows={outboundRows}
          />
        ) : null}
      </SectionCard>

      <SectionCard title="Routing rule health">
        {routingHealthQuery.isLoading ? <TableSkeleton /> : null}
        {routingHealthQuery.isError ? (
          <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
            <AlertDescription className="whitespace-pre-wrap">
              {routingHealthErrorMessage}
            </AlertDescription>
          </Alert>
        ) : null}
        {routingHealth &&
        routingHealth.firewall_rules.length === 0 &&
        routingHealth.route_tables.length === 0 &&
        routingHealth.policy_rules.length === 0 ? (
          <Empty className="border">
            <EmptyHeader>
              <EmptyTitle>No routing checks reported yet</EmptyTitle>
              <EmptyDescription>
                Routing checks will appear after the next reload.
              </EmptyDescription>
            </EmptyHeader>
          </Empty>
        ) : null}
        {routingHealth &&
        (routingHealth.firewall_rules.length > 0 ||
          routingHealth.route_tables.length > 0 ||
          routingHealth.policy_rules.length > 0) ? (
          <RoutingHealthCard routingHealth={routingHealth} />
        ) : null}
      </SectionCard>

      <RoutingTestPanel />
    </div>
  )
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

function getRoutingHealthErrorMessage(error: unknown) {
  if (error && typeof error === "object" && "error" in error) {
    const message = (error as { error?: unknown }).error
    if (typeof message === "string" && message.trim().length > 0) {
      return message
    }
  }

  return (
    getApiErrorMessage(error as ApiError | null) || "Unable to load routing checks."
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

function OutboundDestinationCell({
  outbound,
  configuredOutbound,
}: {
  outbound: HealthEntry
  configuredOutbound?: Outbound
}) {
  return (
    <span className="text-sm">
      {formatOutboundDestination(outbound, configuredOutbound)}
    </span>
  )
}

function formatOutboundDestination(
  outbound: HealthEntry,
  configuredOutbound?: Outbound
) {
  if (outbound.type === "interface") {
    const interfaceName = configuredOutbound?.interface ?? outbound.tag
    const gateway = configuredOutbound?.gateway

    return gateway
      ? `Interface ${interfaceName} (gw: ${gateway})`
      : `Interface ${interfaceName}`
  }

  if (outbound.type === "table") {
    if (typeof configuredOutbound?.table === "number") {
      return `Table ${configuredOutbound.table}`
    }

    return `Table ${outbound.tag}`
  }

  if (outbound.type === "urltest") {
    return `Outbound ${outbound.selected_outbound ?? "-"}`
  }

  return `Outbound ${outbound.tag}`
}

function findConfiguredOutbound(
  config: ConfigObject | undefined,
  tag: string
) {
  return config?.outbounds?.find((outbound) => outbound.tag === tag)
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
      <div className="flex flex-wrap items-center gap-2">
        <div className="font-medium">urltest</div>
        <Badge variant="outline" className="text-[11px]">
          urltest
        </Badge>
      </div>
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
            name={child.tag}
            state={child.success ? "healthy" : "degraded"}
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
