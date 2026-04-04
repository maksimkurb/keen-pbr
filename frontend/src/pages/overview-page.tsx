import { useMemo } from "react"
import { useTranslation } from "react-i18next"
import {
  Play,
  RotateCw,
  Square,
} from "lucide-react"

import type { ApiError } from "@/api/client"
import type { Outbound, RuntimeOutboundState } from "@/api/generated/model"
import {
  useGetConfig,
  useGetHealthRouting,
  useGetHealthService,
  useGetRuntimeOutbounds,
} from "@/api/queries"
import { usePostReloadMutation, usePostServiceActionMutation } from "@/api/mutations"
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
import { RoutingTestPanel } from "@/components/overview/routing-test-panel"
import { getApiErrorMessage } from "@/lib/api-errors"

export function OverviewPage() {
  const { t } = useTranslation()
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

  const postReloadMutation = usePostReloadMutation()
  const postServiceStartMutation = usePostServiceActionMutation("start")
  const postServiceStopMutation = usePostServiceActionMutation("stop")
  const postServiceRestartMutation = usePostServiceActionMutation("restart")
  const serviceActionPending =
    postReloadMutation.isPending ||
    postServiceStartMutation.isPending ||
    postServiceStopMutation.isPending ||
    postServiceRestartMutation.isPending

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
        runtimeOutbounds.map((runtimeOutbound) => [runtimeOutbound.tag, runtimeOutbound])
      ),
    [runtimeOutbounds]
  )
  const hasResolverHashMismatch =
    Boolean(serviceHealth?.resolver_config_hash) &&
    Boolean(serviceHealth?.resolver_config_hash_actual) &&
    serviceHealth?.resolver_config_hash !== serviceHealth?.resolver_config_hash_actual

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
        outbound.type === "urltest" || outbound.type === "interface" || detailContent ? (
        <div className="space-y-2">
          <div className="flex flex-wrap items-center gap-2">
            <div className="font-medium">
              {outbound.type === "urltest"
                ? t("overview.outbounds.urltestTitle")
                : outbound.tag}
            </div>
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
        <SectionCard title={t("overview.service.title")}>
          {serviceHealthQuery.isLoading ? <ServiceSummarySkeleton /> : null}

          {serviceHealthQuery.isError ? (
            <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
              <AlertDescription>
                {t("overview.service.loadError")}
              </AlertDescription>
            </Alert>
          ) : null}

          {serviceHealth ? (
            <>
              <div className="grid gap-4 md:grid-cols-2">
                <div>
                  <div className="mb-1 text-sm text-muted-foreground">
                    {t("overview.service.version")}
                  </div>
                  <div className="text-lg font-semibold">
                    {serviceHealth.version}
                  </div>
                </div>
                <div>
                  <div className="mb-1 text-sm text-muted-foreground">
                    {t("overview.service.status")}
                  </div>
                  <div className="flex flex-wrap items-center gap-2">
                    <StatusBadge
                      tone={mapServiceStatusTone(serviceHealth.status)}
                    >
                      {serviceHealth.status}
                    </StatusBadge>
                    {hasResolverHashMismatch ? (
                      <StatusBadge tone="warning">
                        {t("overview.service.dnsmasqStale")}
                      </StatusBadge>
                    ) : <StatusBadge tone="healthy">
                        {t("overview.service.dnsmasqGood")}
                      </StatusBadge>}
                  </div>
                </div>
              </div>

              <ButtonGroup className="mt-2 [&>[data-slot=button]]:flex-1">
                <Button
                  size="sm"
                  variant="outline"
                  disabled={serviceActionPending}
                  onClick={() => postServiceStartMutation.mutate()}
                >
                  <Play className="mr-1 h-3 w-3" />
                  {t("overview.service.actions.start")}
                </Button>
                <Button
                  size="sm"
                  variant="outline"
                  disabled={serviceActionPending}
                  onClick={() => postServiceStopMutation.mutate()}
                >
                  <Square className="mr-1 h-3 w-3" />
                  {t("overview.service.actions.stop")}
                </Button>
                <Button
                  size="sm"
                  variant="outline"
                  disabled={serviceActionPending}
                  onClick={() => postServiceRestartMutation.mutate()}
                >
                  <RotateCw className="mr-1 h-3 w-3" />
                  {t("overview.service.actions.restart")}
                </Button>
                <Button
                  size="sm"
                  variant="outline"
                  disabled={serviceActionPending}
                  onClick={() => postReloadMutation.mutate()}
                >
                  <RotateCw className="mr-1 h-3 w-3" />
                  {t("overview.service.actions.reload")}
                </Button>
              </ButtonGroup>
            </>
          ) : null}
        </SectionCard>

        <DnsCheckWidget
          dnsProbeEnabled={Boolean(loadedConfig?.dns?.dns_test_server)}
        />
      </div>

      <SectionCard title={t("overview.outbounds.title")}>
        {configQuery.isLoading ? <TableSkeleton /> : null}
        {configQuery.isError || runtimeOutboundsQuery.isError ? (
          <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
            <AlertDescription>{t("overview.outbounds.loadError")}</AlertDescription>
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
            headers={[
              t("overview.outbounds.headers.tag"),
              t("overview.outbounds.headers.status"),
            ]}
            rows={outboundRows}
          />
        ) : null}
      </SectionCard>

      <SectionCard title={t("overview.routing.title")}>
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
    getApiErrorMessage(error as ApiError | null) || t("overview.routing.loadError")
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
