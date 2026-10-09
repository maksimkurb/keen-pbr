import { ResponsiveDialog } from "@/components/shared/responsive-dialog"
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
import { Kbd, KbdGroup } from "@/components/ui/kbd"
import { isRoutingTestShortcut } from "@/components/overview/routing-test-shortcut"
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
import { useRoutingTestPanelState } from "@/components/overview/use-routing-test-panel-state"
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
  const routingTargetRef = useRef<HTMLInputElement>(null)
  const diagnosticsRef = useRef<HTMLElement>(null)

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if (!isRoutingTestShortcut(event)) return
      event.preventDefault()
      setIsRoutingTestOpen(true)
    }
    window.addEventListener("keydown", onKeyDown)
    return () => window.removeEventListener("keydown", onKeyDown)
  }, [])

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
  const routingTestPanel = useRoutingTestPanelState()

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
            variant="default"
            className="max-md:h-12 max-md:w-full max-md:gap-2 max-md:px-5 max-md:text-base"
            aria-keyshortcuts="Control+Alt+K"
            onClick={() => setIsRoutingTestOpen(true)}
          >
            <Route />
            {t("overview.routingTest.title")}
            <KbdGroup className="hidden md:inline-flex">
              <Kbd>Ctrl</Kbd>+<Kbd>Alt</Kbd>+<Kbd>K</Kbd>
            </KbdGroup>
          </Button>
        }
        className="mb-4 md:mb-4 md:items-center"
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

      <ResponsiveDialog
        open={isRoutingTestOpen}
        onOpenChange={setIsRoutingTestOpen}
        title={t("overview.routingTest.title")}
        description={t("overview.routingTest.description")}
        className="sm:max-w-[860px]"
        initialFocus={routingTargetRef}
      >
        <RoutingTestPanel
          state={routingTestPanel}
          targetInputRef={routingTargetRef}
        />
      </ResponsiveDialog>

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
