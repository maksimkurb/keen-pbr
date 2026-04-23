import { useMemo, useState } from "react"
import { useTranslation } from "react-i18next"
import { Download, Play, RotateCw, Square } from "lucide-react"

import type { ApiError } from "@/api/client"
import type { Outbound, RuntimeOutboundState } from "@/api/generated/model"
import type { DnsCheckStatus } from "@/hooks/use-dns-check"
import {
  useGetConfig,
  useGetHealthRouting,
  useGetHealthService,
  useGetRuntimeOutbounds,
} from "@/api/queries"
import {
  usePostServiceActionMutation,
  useRoutingControlPendingState,
} from "@/api/mutations"
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
import { ButtonGroup } from "@/components/shared/button-group"
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

export function OverviewPage() {
  const { t } = useTranslation()
  const [dnsCheckStatus, setDnsCheckStatus] = useState<DnsCheckStatus>("idle")
  const [isDiagnosticsDialogOpen, setIsDiagnosticsDialogOpen] = useState(false)
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
  const runtimeOutboundsQuery = useGetRuntimeOutbounds({
    query: {
      refetchInterval: 30_000,
      refetchIntervalInBackground: false,
    },
  })

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
  const dnsmasqBadge = getDnsmasqBadgeState(
    serviceHealth?.resolver_live_status,
    serviceHealth?.resolver_config_sync_state
  )
  const hasServiceHealth = Boolean(serviceHealth)
  const isServiceRunning = serviceHealth?.status === "running"
  const configIsDraft =
    configQuery.data?.status === 200 ? configQuery.data.data.is_draft : false

  const diagnosticsDownloadReady =
    Boolean(loadedConfig) &&
    Boolean(serviceHealth) &&
    Boolean(routingHealth) &&
    runtimeOutboundsQuery.data?.status === 200 &&
    dnsCheckStatus !== "idle" &&
    dnsCheckStatus !== "checking" &&
    !configIsDraft

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
          t={t}
          variant="tree"
        />
      ) : null
      const tagCell =
        outbound.type === "urltest" ||
        outbound.type === "interface" ||
        detailContent ? (
          <div className="space-y-2">
            <div className="flex flex-wrap items-center gap-2">
              <div className="font-medium">{outbound.tag}</div>
              <Badge size="xs" variant="outline">
                {outbound.type}
              </Badge>
            </div>
            {detailContent}
          </div>
        ) : (
          <div className="flex flex-wrap items-center gap-2">
            <div className="font-medium">{outbound.tag}</div>
            <Badge size="xs" variant="outline">
              {outbound.type}
            </Badge>
          </div>
        )

      return [
        tagCell,
        <StatusBadge
          key={`${outbound.tag}-status`}
          tone={mapRuntimeHealthTone(runtimeState?.status)}
        >
          {runtimeState?.status ?? "unknown"}
        </StatusBadge>,
      ]
    })
  }, [loadedConfig, runtimeOutboundByTag, t])

  const routingHealthErrorMessage = routingHealthQuery.isError
    ? getRoutingHealthErrorMessage(routingHealthQuery.error, t)
    : null

  return (
    <div className="space-y-6">
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
              <ButtonGroup className="mt-auto [&>[data-slot=button]]:flex-1">
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
          dnsProbeEnabled={Boolean(loadedConfig?.dns?.dns_test_server)}
          onStatusChange={setDnsCheckStatus}
        />
      </div>

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
            <DataTable
              compact
              rows={outboundRows}
            />
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
              disabled={!diagnosticsDownloadReady}
              onClick={() => setIsDiagnosticsDialogOpen(true)}
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
          routingHealth.firewall_rules.length === 0 &&
          routingHealth.route_tables.length === 0 &&
          routingHealth.policy_rules.length === 0 ? (
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
          (routingHealth.firewall_rules.length > 0 ||
            routingHealth.route_tables.length > 0 ||
            routingHealth.policy_rules.length > 0) ? (
            <RoutingHealthCard routingHealth={routingHealth} />
          ) : null}
        </SectionCard>
      </div>

      {loadedConfig &&
      serviceHealth &&
      routingHealth &&
      runtimeOutboundsQuery.data?.status === 200 ? (
        <DiagnosticsDownloadDialog
          config={loadedConfig}
          dnsCheckStatus={dnsCheckStatus}
          onOpenChange={setIsDiagnosticsDialogOpen}
          open={isDiagnosticsDialogOpen}
          routingHealth={routingHealth}
          runtimeOutbounds={runtimeOutboundsQuery.data.data}
          serviceHealth={serviceHealth}
        />
      ) : null}
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
