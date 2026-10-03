import { useMemo, useState } from "react"
import { useTranslation } from "react-i18next"
import { Download, Play, RotateCw, Square } from "lucide-react"

import type { ApiError } from "@/api/client"
import type {
  InterceptHealth,
  Outbound,
  RuntimeOutboundState,
} from "@/api/generated/model"
import {
  DNS_CHECK_DOMAIN_SUFFIX,
  type DnsCheckStatus,
  useInterceptEventMonitor,
} from "@/hooks/use-dns-check"
import {
  useGetConfig,
  useGetHealthRouting,
  useGetHealthService,
  useGetRuntimeInterfaces,
  useGetRuntimeOutbounds,
} from "@/api/queries"
import {
  usePostServiceActionMutation,
  useRoutingControlPendingState,
} from "@/api/mutations"
import { selectConfig } from "@/api/selectors"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { ButtonGroup } from "@/components/ui/button-group"
import { Alert, AlertDescription } from "@/components/ui/alert"
import {
  Empty,
  EmptyDescription,
  EmptyHeader,
  EmptyTitle,
} from "@/components/ui/empty"
import { Skeleton } from "@/components/ui/skeleton"
import { DataTable } from "@/components/shared/data-table"
import { PageHeader } from "@/components/shared/page-header"
import { RuntimeOutboundDetails } from "@/components/shared/runtime-outbound-state"
import { SectionCard } from "@/components/shared/section-card"
import { RoutingHealthCard } from "@/components/overview/routing-health-card"
import { DnsCheckWidget } from "@/components/overview/dns-check-widget"
import { DiagnosticsDownloadDialog } from "@/components/overview/diagnostics-download-dialog"
import { getDnsmasqBadgeState } from "@/components/overview/dnsmasq-status"
import { RoutingTestPanel } from "@/components/overview/routing-test-panel"
import { getApiErrorMessage } from "@/lib/api-errors"
import { useAuth } from "@/auth/auth-context"
import { Link } from "wouter"

export function OverviewPage() {
  const { t } = useTranslation()
  const auth = useAuth()
  const [dnsCheckStatus, setDnsCheckStatus] = useState<DnsCheckStatus>("idle")
  const [isDiagnosticsDialogOpen, setIsDiagnosticsDialogOpen] = useState(false)
  const serviceHealthQuery = useGetHealthService()
  const configQuery = useGetConfig()
  const routingHealthQuery = useGetHealthRouting({
    query: {
      refetchInterval: 45_000,
      refetchIntervalInBackground: false,
    },
  })
  const runtimeOutboundsQuery = useGetRuntimeOutbounds()
  const runtimeInterfacesQuery = useGetRuntimeInterfaces()

  const postServiceStartMutation = usePostServiceActionMutation("start")
  const postServiceStopMutation = usePostServiceActionMutation("stop")
  const postServiceRestartMutation = usePostServiceActionMutation("restart")
  const { anyPending: actionPending } = useRoutingControlPendingState()

  const serviceHealth =
    serviceHealthQuery.data?.status === 200
      ? serviceHealthQuery.data.data
      : undefined
  const loadedConfig = selectConfig(configQuery.data)
  const routingHealth =
    routingHealthQuery.data?.status === 200
      ? routingHealthQuery.data.data
      : undefined
  const routingFirewallRules = routingHealth?.firewall_rules ?? []
  const routingRouteTables = routingHealth?.route_tables ?? []
  const routingPolicyRules = routingHealth?.policy_rules ?? []
  const runtimeOutbounds = useMemo(
    () =>
      runtimeOutboundsQuery.data?.status === 200
        ? runtimeOutboundsQuery.data.data.outbounds
        : [],
    [runtimeOutboundsQuery.data]
  )
  const runtimeOutboundByTag = useMemo(
    () =>
      new Map(
        runtimeOutbounds.map((runtimeOutbound) => [
          runtimeOutbound.tag,
          runtimeOutbound,
        ])
      ),
    [runtimeOutbounds]
  )
  const runtimeInterfaceByName = useMemo(
    () =>
      new Map(
        (runtimeInterfacesQuery.data?.status === 200
          ? runtimeInterfacesQuery.data.data.interfaces
          : []
        ).map((runtimeInterface) => [runtimeInterface.name, runtimeInterface])
      ),
    [runtimeInterfacesQuery.data]
  )
  const dnsmasqBadge = getDnsmasqBadgeState(
    serviceHealth?.resolver_live_status,
    serviceHealth?.resolver_config_sync_state,
    serviceHealth?.resolver_integration,
    serviceHealth?.resolver_config_probe_status
  )
  const hasServiceHealth = Boolean(serviceHealth)
  const isServiceRunning = serviceHealth?.status === "running"
  const markerConfig = loadedConfig?.intercept?.dns?.marker
  const dnsCheckConfigEnabled =
    Boolean(loadedConfig) &&
    loadedConfig?.intercept?.enabled !== false &&
    loadedConfig?.intercept?.dns?.enabled !== false
  const dnsCheckRuntimeEnabled =
    serviceHealth?.intercept?.running === true &&
    serviceHealth.intercept.dns_hold_active === true
  const dnsCheckEnabled = dnsCheckConfigEnabled && dnsCheckRuntimeEnabled
  const dnsCheckDisabledReason = !dnsCheckConfigEnabled ? "config" : "runtime"
  const interceptMonitor = useInterceptEventMonitor(
    serviceHealth?.intercept?.running === true
  )
  const outboundRows = useMemo(() => {
    const configuredOutbounds = loadedConfig?.outbounds ?? []
    if (configuredOutbounds.length === 0) {
      return []
    }

    return configuredOutbounds.map((outbound) => {
      const runtimeState = runtimeOutboundByTag.get(outbound.tag)
      const detailContent = runtimeState ? (
        <RuntimeOutboundDetails
          fallbackLabel={getRuntimeFallbackLabel(outbound, t)}
          fallbackTone={getRuntimeFallbackTone(outbound)}
          runtimeState={runtimeState}
          runtimeInterfaces={runtimeInterfaceByName}
          t={t}
          variant="tree"
        />
      ) : null
      const tagCell =
        outbound.type === "urltest" ||
        outbound.type === "interface" ||
        detailContent ? (
          <div className="space-y-2">
            <OutboundHeader outbound={outbound} runtimeState={runtimeState} />
            {detailContent}
          </div>
        ) : (
          <OutboundHeader outbound={outbound} runtimeState={runtimeState} />
        )

      return [tagCell]
    })
  }, [loadedConfig, runtimeInterfaceByName, runtimeOutboundByTag, t])

  const routingHealthErrorMessage = routingHealthQuery.isError
    ? getRoutingHealthErrorMessage(routingHealthQuery.error, t)
    : null

  return (
    <div className="space-y-6">
      {!auth.enabled ? (
        <Alert className="border-amber-500/40 bg-amber-500/10">
          <AlertDescription>
            {t("auth.warning.prefix")}
            <Link className="font-medium underline" href="/security">
              {t("auth.warning.action")}
            </Link>
            {t("auth.warning.suffix")}
          </AlertDescription>
        </Alert>
      ) : null}
      <PageHeader
        description={t("overview.pageDescription")}
        title={t("nav.items.systemMonitor")}
      />

      <div className="grid gap-4 xl:grid-cols-2">
        <SectionCard
          className="h-full"
          contentClassName="flex flex-1 flex-col"
          title={t("overview.runtime.title")}
          description={t("overview.runtime.description")}
        >
          {serviceHealthQuery.isLoading ? <ServiceSummarySkeleton /> : null}

          {serviceHealthQuery.isError ? (
            <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
              <AlertDescription>
                {t("overview.runtime.loadError")}
              </AlertDescription>
            </Alert>
          ) : null}

          {serviceHealth ? (
            <div className="flex h-full flex-1 flex-col">
              <div className="mb-2 grid gap-4 md:grid-cols-2 xl:grid-cols-3">
                <div>
                  <div className="mb-1 text-sm text-muted-foreground">
                    {t("overview.runtime.version")}
                  </div>
                  <div className="text-lg font-semibold">
                    {serviceHealth.version}
                  </div>
                  <div className="text-xs text-muted-foreground">
                    build {serviceHealth.build}
                  </div>
                </div>
                <div>
                  <div className="mb-1 text-sm text-muted-foreground">
                    {t("overview.runtime.router")}
                  </div>
                  <div className="text-lg font-semibold">
                    {`${serviceHealth.os_type} ${serviceHealth.os_version}`}
                  </div>
                </div>
                <div>
                  <div className="mb-1 text-sm text-muted-foreground">
                    {t("overview.runtime.status")}
                  </div>
                  <div className="flex flex-wrap items-center gap-2">
                    <StatusBadge
                      tone={mapServiceStatusTone(serviceHealth.status)}
                    >
                      {serviceHealth.status}
                    </StatusBadge>
                    <StatusBadge tone={dnsmasqBadge.tone}>
                      {t(dnsmasqBadge.labelKey)}
                    </StatusBadge>
                  </div>
                </div>
              </div>
              <ButtonGroup className="mt-auto w-full [&>[data-slot=button]]:flex-1">
                <Button
                  size="sm"
                  variant="outline"
                  disabled={
                    actionPending || !hasServiceHealth || isServiceRunning
                  }
                  onClick={() => postServiceStartMutation.mutate()}
                >
                  <Play className="mr-1 h-3 w-3" />
                  {t("overview.runtime.actions.start")}
                </Button>
                <Button
                  size="sm"
                  variant="outline"
                  disabled={
                    actionPending || !hasServiceHealth || !isServiceRunning
                  }
                  onClick={() => postServiceStopMutation.mutate()}
                >
                  <Square className="mr-1 h-3 w-3" />
                  {t("overview.runtime.actions.stop")}
                </Button>
                <Button
                  size="sm"
                  variant="outline"
                  disabled={
                    actionPending || !hasServiceHealth || !isServiceRunning
                  }
                  onClick={() => postServiceRestartMutation.mutate()}
                >
                  <RotateCw className="mr-1 h-3 w-3" />
                  {t("overview.runtime.actions.restart")}
                </Button>
              </ButtonGroup>
            </div>
          ) : null}
        </SectionCard>

        <DnsCheckWidget
          disabledReason={dnsCheckDisabledReason}
          dnsProbeEnabled={dnsCheckEnabled}
          liveEvent={interceptMonitor.lastEvent}
          liveMonitorStatus={interceptMonitor.status}
          markerDomain={markerConfig?.domain ?? DNS_CHECK_DOMAIN_SUFFIX}
          onStatusChange={setDnsCheckStatus}
        />
      </div>

      <InterceptHealthCard health={serviceHealth?.intercept} />

      <RoutingTestPanel />

      <div className="grid gap-4 xl:grid-cols-2">
        <SectionCard className="h-full" title={t("overview.outbounds.title")}>
          {configQuery.isLoading ? <TableSkeleton /> : null}
          {configQuery.isError || runtimeOutboundsQuery.isError ? (
            <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
              <AlertDescription>
                {t("overview.outbounds.loadError")}
              </AlertDescription>
            </Alert>
          ) : null}
          {!configQuery.isLoading && outboundRows.length === 0 ? (
            <Empty className="border">
              <EmptyHeader>
                <EmptyTitle>{t("overview.outbounds.emptyTitle")}</EmptyTitle>
                <EmptyDescription>
                  {t("overview.outbounds.emptyDescription")}
                </EmptyDescription>
              </EmptyHeader>
            </Empty>
          ) : null}
          {outboundRows.length > 0 ? (
            <DataTable compact rows={outboundRows} />
          ) : null}
        </SectionCard>

        <SectionCard
          className="h-full"
          contentClassName="flex flex-1 flex-col"
          title={t("overview.routing.title")}
          action={
            <Button
              size="sm"
              variant="outline"
              onClick={() => {
                setIsDiagnosticsDialogOpen(true)
              }}
            >
              <Download className="h-4 w-4" />
              {t("overview.diagnosticsDownload.button")}
            </Button>
          }
        >
          {routingHealthQuery.isLoading ? <TableSkeleton /> : null}
          {routingHealthQuery.isError ? (
            <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
              <AlertDescription className="whitespace-pre-wrap">
                {routingHealthErrorMessage}
              </AlertDescription>
            </Alert>
          ) : null}
          {routingHealth &&
          routingFirewallRules.length === 0 &&
          routingRouteTables.length === 0 &&
          routingPolicyRules.length === 0 ? (
            <Empty className="border">
              <EmptyHeader>
                <EmptyTitle>{t("overview.routing.emptyTitle")}</EmptyTitle>
                <EmptyDescription>
                  {t("overview.routing.emptyDescription")}
                </EmptyDescription>
              </EmptyHeader>
            </Empty>
          ) : null}
          {routingHealth &&
          (routingFirewallRules.length > 0 ||
            routingRouteTables.length > 0 ||
            routingPolicyRules.length > 0) ? (
            <RoutingHealthCard routingHealth={routingHealth} />
          ) : null}
        </SectionCard>
      </div>

      <DiagnosticsDownloadDialog
        config={loadedConfig}
        dnsCheckStatus={dnsCheckStatus}
        onOpenChange={setIsDiagnosticsDialogOpen}
        open={isDiagnosticsDialogOpen}
        routingHealth={routingHealth}
        runtimeOutbounds={
          runtimeOutboundsQuery.data?.status === 200
            ? runtimeOutboundsQuery.data.data
            : undefined
        }
        serviceHealth={serviceHealth}
      />
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

function InterceptHealthCard({ health }: { health?: InterceptHealth }) {
  const { t } = useTranslation()

  if (!health) {
    return null
  }

  const status = !health.enabled
    ? t("overview.intercept.status.disabled")
    : health.running
      ? t("overview.intercept.status.running")
      : t("overview.intercept.status.stopped")
  const statusTone = !health.enabled
    ? "warning"
    : health.running
      ? "healthy"
      : "degraded"
  const counters = health.counters
  const capabilities = [
    ["nfqueue", health.capabilities.nfqueue],
    ["nflog", health.capabilities.nflog],
    ["connbytes", health.capabilities.connbytes],
  ] as const
  const counterEntries = counters
    ? ([
        ["dnsPackets", counters.dns_packets],
        ["dnsParseErrors", counters.dns_parse_errors],
        ["dnsMatched", counters.dns_matched],
        ["dnsHoldTimeouts", counters.dns_hold_timeouts],
        ["dnsTcpPartial", counters.dns_tcp_partial],
        ["markerHits", counters.marker_hits],
        ["l7Packets", counters.l7_packets],
        ["l7Matched", counters.l7_matched],
        ["setAdded", counters.set_added],
        ["setRefreshed", counters.set_refreshed],
        ["setErrors", counters.set_errors],
        ["conntrackRequests", counters.conntrack_requests],
        ["conntrackDeleted", counters.conntrack_deleted],
        ["conntrackErrors", counters.conntrack_errors],
        ["queueOverruns", counters.queue_overruns],
        ["logOverruns", counters.log_overruns],
      ] as const)
    : []

  return (
    <SectionCard
      description={t("overview.intercept.description")}
      title={t("overview.intercept.title")}
    >
      <div className="space-y-4">
        <div className="flex flex-wrap items-center gap-2">
          <StatusBadge tone={statusTone}>{status}</StatusBadge>
          <StatusBadge tone={health.dns_hold_active ? "healthy" : "warning"}>
            {health.dns_hold_active
              ? t("overview.intercept.dnsHoldActive")
              : t("overview.intercept.dnsHoldInactive")}
          </StatusBadge>
          <StatusBadge tone={health.l7_active ? "healthy" : "warning"}>
            {health.l7_active
              ? t("overview.intercept.l7Active")
              : t("overview.intercept.l7Inactive")}
          </StatusBadge>
        </div>

        <div className="grid gap-3 text-sm sm:grid-cols-3">
          {capabilities.map(([name, supported]) => (
            <div className="flex items-center justify-between gap-2" key={name}>
              <span className="text-muted-foreground">
                {t(`overview.intercept.capabilities.${name}`)}
              </span>
              <span
                className={supported ? "text-emerald-600" : "text-destructive"}
              >
                {supported
                  ? t("overview.intercept.supported")
                  : t("overview.intercept.unsupported")}
              </span>
            </div>
          ))}
        </div>

        {health.reasons.length > 0 ? (
          <Alert className="border-amber-500/30 bg-amber-500/5 text-amber-700 dark:text-amber-300">
            <AlertDescription>
              <div className="space-y-1">
                <div className="font-medium">
                  {t("overview.intercept.unsupportedWarning")}
                </div>
                <ul className="list-disc pl-5">
                  {health.reasons.map((reason) => (
                    <li key={reason}>{reason}</li>
                  ))}
                </ul>
              </div>
            </AlertDescription>
          </Alert>
        ) : null}

        {counterEntries.length > 0 ? (
          <div className="grid gap-x-4 gap-y-2 text-sm sm:grid-cols-2 lg:grid-cols-4">
            {counterEntries.map(([name, value]) => (
              <Counter
                key={name}
                label={t(`overview.intercept.counters.${name}`)}
                value={value}
              />
            ))}
          </div>
        ) : null}
      </div>
    </SectionCard>
  )
}

function Counter({ label, value }: { label: string; value?: number }) {
  return (
    <div className="flex items-center justify-between gap-2">
      <span className="text-muted-foreground">{label}</span>
      <span className="font-medium tabular-nums">{value ?? 0}</span>
    </div>
  )
}

function getRoutingHealthErrorMessage(
  error: unknown,
  t: (key: string) => string
) {
  if (error && typeof error === "object" && "error" in error) {
    const message = (error as { error?: unknown }).error
    if (typeof message === "string" && message.trim().length > 0) {
      return message
    }
  }

  return (
    getApiErrorMessage(error as ApiError | null) ||
    t("overview.routing.loadError")
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

function StatusBadge({
  tone,
  children,
}: {
  tone: "healthy" | "warning" | "degraded"
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

function OutboundHeader({
  outbound,
  runtimeState,
}: {
  outbound: Outbound
  runtimeState?: RuntimeOutboundState
}) {
  return (
    <div className="flex flex-wrap items-center gap-2">
      <div className="font-medium">{outbound.tag}</div>
      <Badge size="xs" variant="outline">
        {outbound.type}
      </Badge>
      <StatusBadge tone={mapRuntimeHealthTone(runtimeState?.status)}>
        {runtimeState?.status ?? "unknown"}
      </StatusBadge>
    </div>
  )
}

function mapRuntimeHealthTone(
  status: RuntimeOutboundState["status"] | undefined
): "healthy" | "warning" | "degraded" {
  if (status === "healthy") {
    return "healthy"
  }

  if (status === "unknown" || status === undefined) {
    return "warning"
  }

  return "degraded"
}

function getRuntimeFallbackLabel(
  outbound: Outbound,
  t: (key: string, options?: Record<string, unknown>) => string
): string | undefined {
  if (outbound.type === "table" && typeof outbound.table === "number") {
    return t("runtime.fallback.table", { value: outbound.table })
  }

  if (outbound.type === "blackhole") {
    return t("runtime.fallback.blackhole")
  }

  return undefined
}

function getRuntimeFallbackTone(
  outbound: Outbound
): "info" | "unknown" | undefined {
  if (outbound.type === "table") {
    return "info"
  }

  if (outbound.type === "blackhole") {
    return "unknown"
  }

  return undefined
}
