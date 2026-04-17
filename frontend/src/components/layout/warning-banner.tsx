import { useEffect, useMemo, useState } from "react"
import { SaveIcon } from "lucide-react"
import { useTranslation } from "react-i18next"

import type { HealthResponse } from "@/api/generated/model"
import { useApplyConfigMutation, useRoutingControlPendingState } from "@/api/mutations"
import { useGetHealthService } from "@/api/queries"
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { useSidebar } from "@/components/ui/sidebar-context"
import { cn } from "@/lib/utils"

const DEFAULT_REFETCH_INTERVAL_MS = 30_000
const CONVERGING_REFETCH_INTERVAL_MS = 1_000
const CONVERGING_WINDOW_MS = 15_000

type WarningBannerMode =
  | "hidden"
  | "draft"
  | "draft-and-dnsmasq"
  | "dnsmasq-stale"
  | "dnsmasq-converging"
  | "dnsmasq-error"

export type WarningBannerState = {
  applyPending: boolean
  isActionDisabled: boolean
  isVisible: boolean
  mode: WarningBannerMode
  progressPercent: number
}

export function useWarningBannerState(): WarningBannerState {
  const [nowMs, setNowMs] = useState(() => Date.now())
  const healthQuery = useGetHealthService({
    query: {
      refetchInterval: (query) => {
        const response = query.state.data
        if (response?.status !== 200) {
          return DEFAULT_REFETCH_INTERVAL_MS
        }

        const mode = getWarningBannerMode(response.data, Date.now())

        return mode === "dnsmasq-converging" || mode === "dnsmasq-error"
          ? CONVERGING_REFETCH_INTERVAL_MS
          : DEFAULT_REFETCH_INTERVAL_MS
      },
      refetchIntervalInBackground: false,
    },
  })
  const { anyPending, applyPending } = useRoutingControlPendingState()
  const serviceHealth = healthQuery.data?.status === 200 ? healthQuery.data.data : null
  const mode = getWarningBannerMode(serviceHealth, nowMs)

  useEffect(() => {
    if (mode !== "dnsmasq-converging") {
      setNowMs(Date.now())
      return
    }

    const timer = window.setInterval(() => {
      setNowMs(Date.now())
    }, 500)

    return () => window.clearInterval(timer)
  }, [mode])

  const progressPercent = useMemo(() => {
    if (mode !== "dnsmasq-converging") {
      return 0
    }

    const fallbackApplyStartedMs = nowMs
    const applyStartedMs =
      typeof serviceHealth?.apply_started_ts === "number"
        ? serviceHealth.apply_started_ts * 1000
        : fallbackApplyStartedMs
    const elapsed = Math.max(0, nowMs - applyStartedMs)

    return Math.min(95, (elapsed / CONVERGING_WINDOW_MS) * 100)
  }, [mode, nowMs, serviceHealth?.apply_started_ts])

  return {
    applyPending,
    isActionDisabled: anyPending || !serviceHealth,
    isVisible: mode !== "hidden",
    mode,
    progressPercent,
  }
}

export function WarningBanner({
  className,
  state,
}: {
  className?: string
  state: WarningBannerState
}) {
  const { t } = useTranslation()
  const { isMobile, open } = useSidebar()
  const applyConfigMutation = useApplyConfigMutation()

  if (!state.isVisible) {
    return null
  }

  const isConverging = state.mode === "dnsmasq-converging"
  const isError = state.mode === "dnsmasq-error"

  return (
    <div
      className={cn(
        "pointer-events-none fixed inset-x-0 bottom-0 z-50 px-4 pb-5",
        !isMobile && open ? "left-[calc(var(--sidebar-width)+1rem)] right-4" : null,
        className
      )}
    >
      <Alert
        variant={isError ? "destructive" : isConverging ? "default" : "warning"}
        className="pointer-events-auto w-full w-full gap-4 rounded-2xl border border-white/10 px-4 py-4 shadow-[0_10px_30px_hsl(0_0%_0%/0.18),0_24px_80px_hsl(0_0%_0%/0.4)] ring-1 ring-black/5 dark:border-white/12 dark:ring-white/6"
      >
        <div className="flex flex-col gap-3 sm:flex-row sm:items-center sm:justify-between">
          <div className="min-w-0 space-y-1">
            <AlertTitle>{t(getWarningBannerTitleKey(state.mode))}</AlertTitle>
            <AlertDescription>
              {t(getWarningBannerDescriptionKey(state.mode))}
            </AlertDescription>
          </div>

          {!isConverging ? (
            <Button
              disabled={state.isActionDisabled}
              onClick={() => applyConfigMutation.mutate()}
              size="lg"
              variant="outline"
              className="shrink-0 bg-background hover:bg-background dark:bg-card dark:hover:bg-card"
            >
              <SaveIcon className="mr-1 h-4 w-4" />
              {state.applyPending
                ? t("warning.actions.applyingAndRestarting")
                : t("warning.actions.applyAndRestart")}
            </Button>
          ) : null}
        </div>

        {isConverging ? (
          <div className="h-2 rounded bg-muted">
            <div
              className="h-2 rounded bg-primary transition-[width] duration-700"
              style={{ width: `${state.progressPercent}%` }}
            />
          </div>
        ) : null}
      </Alert>
    </div>
  )
}

function getWarningBannerMode(
  serviceHealth: HealthResponse | null,
  nowMs: number
): WarningBannerMode {
  if (!serviceHealth) {
    return "hidden"
  }

  if (serviceHealth.config_is_draft) {
    return serviceHealth.resolver_config_sync_state === "stale"
      ? "draft-and-dnsmasq"
      : "draft"
  }

  if (isResolverUnavailable(serviceHealth.resolver_live_status)) {
    return isRecentApply(serviceHealth.apply_started_ts, nowMs)
      ? "dnsmasq-converging"
      : "dnsmasq-error"
  }

  if (serviceHealth.resolver_config_sync_state === "stale") {
    return "dnsmasq-stale"
  }

  if (serviceHealth.resolver_config_sync_state === "converging") {
    return "dnsmasq-converging"
  }

  return "hidden"
}

function isRecentApply(
  applyStartedTs: number | undefined,
  nowMs: number
): boolean {
  if (typeof applyStartedTs !== "number") {
    return false
  }

  return nowMs - applyStartedTs * 1000 <= CONVERGING_WINDOW_MS
}

function isResolverUnavailable(status: HealthResponse["resolver_live_status"]) {
  return status === "degraded" || status === "unavailable"
}

function getWarningBannerTitleKey(mode: WarningBannerMode) {
  switch (mode) {
    case "draft":
      return "warning.compact.keenRestartRequired"
    case "draft-and-dnsmasq":
      return "warning.compact.keenAndDnsmasqRestartRequired"
    case "dnsmasq-stale":
      return "warning.compact.dnsmasqRestartRequired"
    case "dnsmasq-converging":
      return "warning.compact.dnsmasqRestarting"
    case "dnsmasq-error":
      return "warning.compact.dnsmasqUnavailable"
    case "hidden":
      return "warning.compact.keenRestartRequired"
  }
}

function getWarningBannerDescriptionKey(mode: WarningBannerMode) {
  switch (mode) {
    case "draft":
      return "warning.compact.keenRestartRequiredDescription"
    case "draft-and-dnsmasq":
      return "warning.compact.keenAndDnsmasqRestartRequiredDescription"
    case "dnsmasq-stale":
      return "warning.compact.dnsmasqRestartRequiredDescription"
    case "dnsmasq-converging":
      return "warning.compact.dnsmasqRestartingDescription"
    case "dnsmasq-error":
      return "warning.compact.dnsmasqUnavailableDescription"
    case "hidden":
      return "warning.compact.keenRestartRequiredDescription"
  }
}
