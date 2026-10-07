import { useEffect, useMemo, useRef, useState } from "react"
import { useTranslation } from "react-i18next"
import { Route } from "lucide-react"
import { Link } from "wouter"

import type { ApiError } from "@/api/client"
import {
  DNS_CHECK_DOMAIN_SUFFIX,
  type DnsCheckStatus,
} from "@/hooks/use-dns-check"
import {
  useGetConfig,
  useGetHealthRouting,
  useGetHealthService,
  useGetRuntimeOutbounds,
} from "@/api/queries"
import { selectConfig } from "@/api/selectors"
import { useAuth } from "@/auth/auth-context"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog"
import { PageHeader } from "@/components/shared/page-header"
import { CountersPanel } from "@/components/overview/counters-panel"
import { DiagnosticsDownloadDialog } from "@/components/overview/diagnostics-download-dialog"
import { DiagnosticsPanel } from "@/components/overview/diagnostics-panel"
import { DnsRulesPanel } from "@/components/overview/dns-rules-panel"
import { InterceptPanel } from "@/components/overview/intercept-panel"
import {
  collectHealthySummaries,
  collectOverviewIssues,
} from "@/components/overview/overview-issues"
import { OutboundsOverviewPanel } from "@/components/overview/outbounds-overview-panel"
import { RoutingTestPanel } from "@/components/overview/routing-test-panel"
import { ServiceStatusBar } from "@/components/overview/service-status-bar"
import { getApiErrorMessage } from "@/lib/api-errors"

const HIGHLIGHT_MS = 1800

export function OverviewPage() {
  const { t } = useTranslation()
  const auth = useAuth()
  const [dnsCheckStatus, setDnsCheckStatus] = useState<DnsCheckStatus>("idle")
  const [isDiagnosticsDialogOpen, setIsDiagnosticsDialogOpen] = useState(false)
  const [isRoutingTestOpen, setIsRoutingTestOpen] = useState(false)
  const [showHealthyDiagnostics, setShowHealthyDiagnostics] = useState(false)
  const [highlightDiagnostics, setHighlightDiagnostics] = useState(false)
  const diagnosticsRef = useRef<HTMLElement>(null)

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
    () => new Map(runtimeOutbounds.map((state) => [state.tag, state])),
    [runtimeOutbounds]
  )

  const isServiceRunning =
    serviceHealth?.status === "running" || serviceHealth?.status === "degraded"
  const interceptEnabled =
    Boolean(loadedConfig) && loadedConfig?.intercept?.enabled !== false
  const requestedInterceptDns =
    interceptEnabled && loadedConfig?.intercept?.dns?.enabled !== false
  const requestedInterceptL7 =
    interceptEnabled && loadedConfig?.intercept?.l7?.enabled !== false
  const routingHealthErrorMessage = routingHealthQuery.isError
    ? getRoutingHealthErrorMessage(routingHealthQuery.error, t)
    : null

  const issues = useMemo(
    () =>
      collectOverviewIssues({
        serviceRunning: isServiceRunning,
        routingHealth,
        routingHealthError: routingHealthErrorMessage,
        intercept: serviceHealth?.intercept,
        requestedDns: requestedInterceptDns,
        requestedL7: requestedInterceptL7,
        dnsmasq: serviceHealth?.dnsmasq,
        runtimeOutbounds,
        t,
      }),
    [
      isServiceRunning,
      routingHealth,
      routingHealthErrorMessage,
      serviceHealth?.intercept,
      serviceHealth?.dnsmasq,
      requestedInterceptDns,
      requestedInterceptL7,
      runtimeOutbounds,
      t,
    ]
  )
  const healthySummaries = useMemo(
    () =>
      collectHealthySummaries({
        routingHealth,
        intercept: serviceHealth?.intercept,
        requestedDns: requestedInterceptDns,
        requestedL7: requestedInterceptL7,
        runtimeOutbounds,
        t,
      }),
    [
      routingHealth,
      serviceHealth?.intercept,
      requestedInterceptDns,
      requestedInterceptL7,
      runtimeOutbounds,
      t,
    ]
  )

  useEffect(() => {
    if (!highlightDiagnostics) {
      return
    }
    const timer = window.setTimeout(
      () => setHighlightDiagnostics(false),
      HIGHLIGHT_MS
    )
    return () => window.clearTimeout(timer)
  }, [highlightDiagnostics])

  const showIssues = () => {
    diagnosticsRef.current?.scrollIntoView({
      behavior: "smooth",
      block: "nearest",
    })
    diagnosticsRef.current?.focus({ preventScroll: true })
    setHighlightDiagnostics(true)
  }

  const dimmed = !isServiceRunning

  return (
    <div className="space-y-4">
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
        actions={
          <Button
            type="button"
            variant="outline"
            onClick={() => setIsRoutingTestOpen(true)}
          >
            <Route />
            {t("overview.routingTest.title")}
          </Button>
        }
        className="mb-0 md:mb-0 md:items-center"
        title={t("nav.items.systemMonitor")}
      />

      <ServiceStatusBar
        health={serviceHealth}
        isError={serviceHealthQuery.isError}
        isLoading={serviceHealthQuery.isLoading}
        issueCount={issues.length}
        onShowIssues={showIssues}
      />

      <div className="grid items-start gap-4 xl:grid-cols-[minmax(0,1.7fr)_minmax(320px,0.8fr)]">
        <div className="flex min-w-0 flex-col gap-4">
          <OutboundsOverviewPanel
            dimmed={dimmed}
            isLoading={configQuery.isLoading}
            loadError={configQuery.isError || runtimeOutboundsQuery.isError}
            outbounds={loadedConfig?.outbounds ?? []}
            runtimeByTag={runtimeOutboundByTag}
          />
          <DiagnosticsPanel
            healthy={healthySummaries}
            highlighted={highlightDiagnostics}
            isLoading={
              routingHealthQuery.isLoading || serviceHealthQuery.isLoading
            }
            issues={issues}
            onDownload={() => setIsDiagnosticsDialogOpen(true)}
            onShowHealthyChange={setShowHealthyDiagnostics}
            ref={diagnosticsRef}
            showHealthy={showHealthyDiagnostics}
          />
        </div>

        <div className="flex min-w-0 flex-col gap-4">
          <InterceptPanel
            health={serviceHealth?.intercept}
            markerDomain={
              loadedConfig?.intercept?.dns?.marker?.domain ??
              DNS_CHECK_DOMAIN_SUFFIX
            }
            onDnsCheckStatusChange={setDnsCheckStatus}
            requestedDns={requestedInterceptDns}
            requestedL7={requestedInterceptL7}
            serviceRunning={isServiceRunning}
          />
          <DnsRulesPanel dimmed={dimmed} health={serviceHealth?.dnsmasq} />
          <CountersPanel dimmed={dimmed} health={serviceHealth?.intercept} />
        </div>
      </div>

      <Dialog onOpenChange={setIsRoutingTestOpen} open={isRoutingTestOpen}>
        <DialogContent className="max-h-[calc(100dvh-2rem)] overflow-y-auto sm:max-w-[860px]">
          <DialogHeader>
            <DialogTitle>{t("overview.routingTest.title")}</DialogTitle>
            <DialogDescription>
              {t("overview.routingTest.description")}
            </DialogDescription>
          </DialogHeader>
          <RoutingTestPanel />
        </DialogContent>
      </Dialog>

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
