import { useMemo, useState } from "react"
import { useTranslation } from "react-i18next"
import { Download, Play, RotateCw, Square } from "lucide-react"

import type { ApiError } from "@/api/client"
import type {
  DnsmasqHealth,
  InterceptHealth,
  Outbound,
  RuntimeOutboundState,
} from "@/api/generated/model"
import {
  DNS_CHECK_DOMAIN_SUFFIX,
  type DnsCheckStatus,
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
import { Card } from "@/components/ui/card"
import { Checkbox } from "@/components/ui/checkbox"
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
import {
  CompactDiagnosticRow,
  InlineMeta,
  RoutingHealthCard,
} from "@/components/overview/routing-health-card"
import { DnsCheckWidget } from "@/components/overview/dns-check-widget"
import { DiagnosticsDownloadDialog } from "@/components/overview/diagnostics-download-dialog"
import { RoutingTestPanel } from "@/components/overview/routing-test-panel"
import { getApiErrorMessage } from "@/lib/api-errors"
import {
  collectInterceptDiagnosticErrors,
  getVisibleInterceptDiagnosticEntries,
} from "@/lib/intercept-diagnostics"
import { useAuth } from "@/auth/auth-context"
import { Link } from "wouter"

export function OverviewPage() {
  const { t } = useTranslation()
  const auth = useAuth()
  const [dnsCheckStatus, setDnsCheckStatus] = useState<DnsCheckStatus>("idle")
  const [isDiagnosticsDialogOpen, setIsDiagnosticsDialogOpen] = useState(false)
  const [showHealthyDiagnostics, setShowHealthyDiagnostics] = useState(false)
  const serviceHealthQuery = useGetHealthService()
  const configQuery = useGetConfig()
  const routingHealthQuery = useGetHealthRouting({
    query: {
      refetchOnMount: "always",
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

      <Card className="gap-0 py-0">
        <div className="grid min-w-0 grid-cols-1 divide-y lg:grid-cols-3 lg:divide-x lg:divide-y-0">
          <div className="flex min-w-0 flex-col gap-4 p-4 lg:p-5">
            <h3 className="font-semibold">{t("overview.runtime.title")}</h3>
            {serviceHealthQuery.isLoading ? <ServiceSummarySkeleton /> : null}

            {serviceHealthQuery.isError ? (
              <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
                <AlertDescription>
                  {t("overview.runtime.loadError")}
                </AlertDescription>
              </Alert>
            ) : null}

            {serviceHealth ? (
              <>
                <div className="min-w-0 space-y-2">
                  <RuntimeValue
                    label={t("overview.runtime.version")}
                    value={serviceHealth.version}
                  />
                  <RuntimeValue
                    label={t("overview.runtime.build")}
                    value={serviceHealth.build}
                    valueWeight="normal"
                  />
                  <RuntimeValue
                    label={t("overview.runtime.router")}
                    value={`${serviceHealth.os_type} ${serviceHealth.os_version}`}
                  />
                  <StatusValue
                    label={t("overview.runtime.routingStatus")}
                    value={
                      <StatusBadge
                        tone={mapServiceStatusTone(serviceHealth.status)}
                      >
                        {serviceHealth.status}
                      </StatusBadge>
                    }
                  />
                </div>
                <div
                  className={`mt-auto grid gap-2 ${isServiceRunning ? "grid-cols-2" : "grid-cols-1"}`}
                  role="group"
                >
                  {isServiceRunning ? (
                    <>
                      <Button
                        className="flex aspect-square min-h-20 w-full min-w-0 flex-col gap-1 px-1.5 py-2 text-center text-xs leading-tight whitespace-normal"
                        variant="outline"
                        disabled={actionPending}
                        onClick={() => postServiceStopMutation.mutate()}
                      >
                        <Square className="size-6 shrink-0" />
                        {t("overview.runtime.actions.stop")}
                      </Button>
                      <Button
                        className="flex aspect-square min-h-20 w-full min-w-0 flex-col gap-1 px-1.5 py-2 text-center text-xs leading-tight whitespace-normal"
                        variant="outline"
                        disabled={actionPending}
                        onClick={() => postServiceRestartMutation.mutate()}
                      >
                        <RotateCw className="size-6 shrink-0" />
                        {t("overview.runtime.actions.restart")}
                      </Button>
                    </>
                  ) : (
                    <Button
                      className="flex aspect-square min-h-20 w-full min-w-0 flex-col gap-1 px-1.5 py-2 text-center text-xs leading-tight whitespace-normal"
                      variant="outline"
                      disabled={actionPending}
                      onClick={() => postServiceStartMutation.mutate()}
                    >
                      <Play className="size-6 shrink-0" />
                      {t("overview.runtime.actions.start")}
                    </Button>
                  )}
                </div>
              </>
            ) : null}
          </div>

          <div className="flex min-w-0 flex-col gap-4 p-4 lg:p-5">
            <DnsCheckWidget
              summary={<InterceptStatusSummary health={serviceHealth?.intercept} />}
              disabledReason={dnsCheckDisabledReason}
              dnsProbeEnabled={dnsCheckEnabled}
              markerDomain={markerConfig?.domain ?? DNS_CHECK_DOMAIN_SUFFIX}
              onStatusChange={setDnsCheckStatus}
              embedded
            />
          </div>

          <DnsRulesSection health={serviceHealth?.dnsmasq} />
        </div>
      </Card>

      <InterceptCountersCard health={serviceHealth?.intercept} />

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
          <div className="flex flex-wrap items-center justify-end gap-2">
            <label className="flex items-center gap-2 text-xs text-muted-foreground">
              <Checkbox
                checked={showHealthyDiagnostics}
                onCheckedChange={(checked) =>
                  setShowHealthyDiagnostics(checked === true)
                }
              />
              <span>{t("overview.routing.showHealthyEntries")}</span>
            </label>
          </div>
          <InterceptHealthCard
            health={serviceHealth?.intercept}
            requestedDns={
              Boolean(loadedConfig) &&
              loadedConfig?.intercept?.enabled !== false &&
              loadedConfig?.intercept?.dns?.enabled !== false
            }
            requestedL7={
              Boolean(loadedConfig) &&
              loadedConfig?.intercept?.enabled !== false &&
              loadedConfig?.intercept?.l7?.enabled !== false
            }
            showHealthyEntries={showHealthyDiagnostics}
          />
          {routingHealthQuery.isLoading ? <TableSkeleton /> : null}
          {routingHealthQuery.isError ? (
            <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
              <AlertDescription className="whitespace-pre-wrap">
                {routingHealthErrorMessage}
              </AlertDescription>
            </Alert>
          ) : null}
          {routingHealth ? (
            <RoutingHealthCard
              routingHealth={routingHealth}
              showHealthyEntries={showHealthyDiagnostics}
            />
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

function DnsRulesSection({ health }: { health?: DnsmasqHealth }) {
  const { t, i18n } = useTranslation()
  const state = health?.mode === "dnsmasq" ? health.state : "disabled"
  const tone =
    state === "ok"
      ? "healthy"
      : state === "applying"
        ? "warning"
        : state === "disabled"
          ? "warning"
          : "degraded"
  const lastApplyTs = health?.last_apply_ts ?? null

  return (
    <section className="min-w-0 space-y-3 p-4 lg:p-5">
      <div className="flex min-w-0 flex-wrap items-center gap-2">
        <h3 className="min-w-0 flex-1 basis-40 font-semibold break-words">
          {t("overview.dnsRules.title")}
        </h3>
        <div className="flex shrink-0 flex-wrap items-center gap-2">
          <StatusBadge tone={tone}>
            {t(`overview.dnsRules.state.${state}`)}
          </StatusBadge>
        </div>
      </div>
      <div className="space-y-3">
        <div className="space-y-1 text-sm">
          <StatusValue
            label={t("overview.dnsRules.server")}
            value="dnsmasq"
          />
          <StatusValue
            label={t("overview.dnsRules.rules")}
            value={String(health?.rules ?? 0)}
          />
          <StatusValue
            label={t("overview.dnsRules.domains")}
            value={String(health?.domains ?? 0)}
          />
          <StatusValue
            label={t("overview.dnsRules.lastSync")}
            value={
              <span
                title={
                  lastApplyTs
                    ? new Date(lastApplyTs * 1000).toLocaleString(i18n.language)
                    : undefined
                }
              >
                {lastApplyTs
                  ? formatRelativeTime(lastApplyTs * 1000, i18n.language)
                  : t("overview.dnsRules.neverSynced")}
              </span>
            }
          />
        </div>
        {state === "disabled" ? (
          <Alert className="border-border bg-muted/20">
            <AlertDescription>
              {t("overview.dnsRules.disabledDescription")}
            </AlertDescription>
          </Alert>
        ) : null}
        {state === "error" && health?.last_error ? (
          <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
            <AlertDescription>
              {t("overview.dnsRules.lastError")}: {health.last_error}
            </AlertDescription>
          </Alert>
        ) : null}
      </div>
    </section>
  )
}

function formatRelativeTime(timestampMs: number, locale: string) {
  const diffSeconds = Math.round((timestampMs - Date.now()) / 1000)
  const formatter = new Intl.RelativeTimeFormat(locale, { numeric: "auto" })
  const abs = Math.abs(diffSeconds)

  if (abs < 60) {
    return formatter.format(diffSeconds, "second")
  }
  if (abs < 3600) {
    return formatter.format(Math.round(diffSeconds / 60), "minute")
  }
  if (abs < 86400) {
    return formatter.format(Math.round(diffSeconds / 3600), "hour")
  }
  return formatter.format(Math.round(diffSeconds / 86400), "day")
}

function RuntimeValue({
  label,
  value,
  valueWeight = "strong",
}: {
  label: string
  value: string
  valueWeight?: "normal" | "strong"
}) {
  return (
    <StatusValue
      label={label}
      value={
        <div
          className={valueWeight === "normal" ? "font-normal" : "font-semibold"}
        >
          {value}
        </div>
      }
    />
  )
}

function InterceptStatusSummary({ health }: { health?: InterceptHealth }) {
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

  return (
    <div className="space-y-1 text-sm">
      <StatusValue
        label={t("overview.intercept.summary.processor")}
        value={<StatusBadge tone={statusTone}>{status}</StatusBadge>}
      />
      <StatusValue
        label={t("overview.intercept.summary.dnsHold")}
        value={
          <StatusBadge tone={health.dns_hold_active ? "healthy" : "warning"}>
            {health.dns_hold_active
              ? t("overview.intercept.summary.enabled")
              : t("overview.intercept.summary.disabled")}
          </StatusBadge>
        }
      />
      <StatusValue
        label={t("overview.intercept.summary.dpi")}
        value={
          <StatusBadge tone={health.l7_active ? "healthy" : "warning"}>
            {health.l7_active
              ? t("overview.intercept.summary.enabled")
              : t("overview.intercept.summary.disabled")}
          </StatusBadge>
        }
      />
    </div>
  )
}

function StatusValue({
  label,
  value,
}: {
  label: string
  value: React.ReactNode
}) {
  return (
    <div className="grid min-w-0 grid-cols-[minmax(0,1fr)_minmax(0,auto)] items-start gap-x-3 gap-y-1">
      <div className="min-w-0 break-words text-muted-foreground">{label}</div>
      <div className="min-w-0 text-right break-words">{value}</div>
    </div>
  )
}

function InterceptCountersCard({ health }: { health?: InterceptHealth }) {
  const { t } = useTranslation()
  const counters = health?.counters
  const kernelQueue = health?.kernel_queue

  if (!counters && !kernelQueue) {
    return null
  }

  const primaryEntries = counters
    ? ([
        ["dnsPackets", counters.dns_packets],
        ["dnsMatched", counters.dns_matched],
        ["l7Packets", counters.l7_packets],
        ["l7Matched", counters.l7_matched],
      ] as const)
    : []
  const detailEntries = counters
    ? ([
        ["dnsParseErrors", counters.dns_parse_errors],
        ["dnsHoldTimeouts", counters.dns_hold_timeouts],
        ["dnsTcpPartial", counters.dns_tcp_partial],
        ["markerHits", counters.marker_hits],
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
    <SectionCard title={t("overview.intercept.countersTitle")}>
      <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-4">
        {primaryEntries.map(([name, value]) => (
          <StackedCounter
            key={name}
            label={t(`overview.intercept.counters.${name}`)}
            value={value}
          />
        ))}
      </div>
      {detailEntries.length > 0 || kernelQueue ? (
        <details className="group">
          <summary className="inline-flex cursor-pointer text-sm font-medium text-primary underline-offset-4 hover:underline focus-visible:rounded focus-visible:ring-2 focus-visible:ring-ring focus-visible:outline-none">
            {t("overview.intercept.moreCounters")}
          </summary>
          <div className="mt-3 grid gap-3 sm:grid-cols-2 lg:grid-cols-4">
            {kernelQueue ? (
              <StackedCounter
                label={t("overview.intercept.kernelQueue.queueTotal")}
                value={kernelQueue.queue_total}
              />
            ) : null}
            {detailEntries.map(([name, value]) => (
              <StackedCounter
                key={name}
                label={t(`overview.intercept.counters.${name}`)}
                value={value}
              />
            ))}
            {kernelQueue
              ? (
                  [
                    ["queueDropped", kernelQueue.queue_dropped],
                    ["userDropped", kernelQueue.user_dropped],
                    ["idSequence", kernelQueue.id_sequence],
                  ] as const
                ).map(([name, value]) => (
                  <StackedCounter
                    key={name}
                    label={t(`overview.intercept.kernelQueue.${name}`)}
                    value={value}
                  />
                ))
              : null}
          </div>
        </details>
      ) : null}
    </SectionCard>
  )
}

function StackedCounter({ label, value }: { label: string; value?: number }) {
  return (
    <div>
      <div className="text-xs text-muted-foreground">{label}</div>
      <div className="text-xl font-semibold tabular-nums">{value ?? 0}</div>
    </div>
  )
}

function InterceptHealthCard({
  health,
  requestedDns,
  requestedL7,
  showHealthyEntries,
}: {
  health?: InterceptHealth
  requestedDns: boolean
  requestedL7: boolean
  showHealthyEntries: boolean
}) {
  const { t } = useTranslation()

  if (!health) {
    return null
  }

  const capabilities = [
    ["nfqueue", health.capabilities.nfqueue, requestedDns],
    ["nflog", health.capabilities.nflog, requestedL7],
    ["connbytes", health.capabilities.connbytes, requestedL7],
  ] as const
  const capabilityRows = getVisibleInterceptDiagnosticEntries(
    capabilities.map(([name, supported, relevant]) => ({
      key: name,
      primary: (
        <span className="font-medium">
          {t(`overview.intercept.capabilities.${name}`)}
        </span>
      ),
      relevant,
      status: supported ? "ok" : "unsupported",
      statusLabel: supported
        ? t("overview.intercept.supported")
        : t("overview.intercept.unsupported"),
    })),
    showHealthyEntries
  )
  const probeRows = getVisibleInterceptDiagnosticEntries(
    (health.probes ?? []).map((probe) => {
      const relevant =
        (probe.feature === "nfqueue" && requestedDns) ||
        ((probe.feature === "nflog" || probe.feature === "connbytes") &&
          requestedL7) ||
        ((requestedDns || requestedL7) &&
          !["nfqueue", "nflog", "connbytes"].includes(probe.feature))
      return {
        key: probe.feature,
        primary: (
          <>
            <span className="font-medium">{probe.feature}</span>
            {probe.reason ? <InlineMeta>{probe.reason}</InlineMeta> : null}
          </>
        ),
        status: probe.status,
        statusLabel: t(`overview.intercept.probes.status.${probe.status}`),
        relevant,
      }
    }),
    showHealthyEntries
  )
  const rows = [...capabilityRows, ...probeRows]
  const errors = collectInterceptDiagnosticErrors(
    health.capabilities,
    health.probes ?? [],
    requestedDns,
    requestedL7
  )

  if (
    rows.length === 0 &&
    errors.length === 0 &&
    (health.warnings?.length ?? 0) === 0 &&
    health.reasons.length === 0
  ) {
    return null
  }

  return (
    <div className="space-y-4 border-b pb-4">
      {rows.length > 0 ? (
        <section className="space-y-2">
          <div className="flex items-center justify-between gap-2">
            <h3 className="text-sm font-semibold">
              {t("overview.intercept.checksTitle")}
            </h3>
            {health.kernel_release ? (
              <span className="text-xs text-muted-foreground">
                {t("overview.intercept.probes.kernel", {
                  release: health.kernel_release,
                })}
              </span>
            ) : null}
          </div>
          <div className="space-y-2" data-testid="intercept-probes">
            {rows.map((row) => (
              <CompactDiagnosticRow
                key={row.key}
                primary={row.primary}
                status={row.status}
                statusLabel={row.statusLabel}
              />
            ))}
          </div>
        </section>
      ) : null}

      {errors.length > 0 ? (
        <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription>
            <div className="font-medium">
              {t("overview.intercept.diagnosticErrors")}
            </div>
            <ul className="list-disc pl-5">
              {errors.map((error) => (
                <li key={error}>{error}</li>
              ))}
            </ul>
          </AlertDescription>
        </Alert>
      ) : null}

      {health.warnings && health.warnings.length > 0 ? (
        <Alert className="border-amber-500/30 bg-amber-500/5 text-amber-700 dark:text-amber-300">
          <AlertDescription>
            <ul className="list-disc pl-5">
              {health.warnings.map((warning) => (
                <li key={warning}>{warning}</li>
              ))}
            </ul>
          </AlertDescription>
        </Alert>
      ) : null}

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
