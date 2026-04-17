import { useEffect, useMemo, useState } from "react"

import type { HealthResponse } from "@/api/generated/model"
import { useRoutingControlPendingState } from "@/api/mutations"
import { useGetHealthService } from "@/api/queries"

const DEFAULT_REFETCH_INTERVAL_MS = 30_000
const CONVERGING_REFETCH_INTERVAL_MS = 1_000
const CONVERGING_WINDOW_MS = 15_000

export type WarningBannerMode =
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
  const referenceNowMs = useMemo(
    () => Math.max(nowMs, healthQuery.dataUpdatedAt || 0),
    [healthQuery.dataUpdatedAt, nowMs]
  )
  const shouldTrackNowMs = useMemo(() => {
    if (!serviceHealth) {
      return false
    }

    if (serviceHealth.resolver_config_sync_state === "converging") {
      return true
    }

    return (
      isResolverUnavailable(serviceHealth.resolver_live_status) &&
      isRecentApply(serviceHealth.apply_started_ts, referenceNowMs)
    )
  }, [
    referenceNowMs,
    serviceHealth,
  ])
  const effectiveNowMs = shouldTrackNowMs ? nowMs : referenceNowMs
  const mode = getWarningBannerMode(serviceHealth, effectiveNowMs)

  useEffect(() => {
    if (!shouldTrackNowMs) {
      return
    }

    const timer = window.setInterval(() => {
      setNowMs(Date.now())
    }, 500)

    return () => window.clearInterval(timer)
  }, [shouldTrackNowMs])

  const progressPercent = useMemo(() => {
    if (mode !== "dnsmasq-converging") {
      return 0
    }

    const fallbackApplyStartedMs = effectiveNowMs
    const applyStartedMs =
      typeof serviceHealth?.apply_started_ts === "number"
        ? serviceHealth.apply_started_ts * 1000
        : fallbackApplyStartedMs
    const elapsed = Math.max(0, effectiveNowMs - applyStartedMs)

    return Math.min(95, (elapsed / CONVERGING_WINDOW_MS) * 100)
  }, [effectiveNowMs, mode, serviceHealth?.apply_started_ts])

  return {
    applyPending,
    isActionDisabled: anyPending || !serviceHealth,
    isVisible: mode !== "hidden",
    mode,
    progressPercent,
  }
}

export function getWarningBannerMode(
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
