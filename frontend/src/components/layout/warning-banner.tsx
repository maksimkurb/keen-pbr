import { useEffect, useMemo, useState } from "react"
import { SaveIcon } from "lucide-react"
import { useTranslation } from "react-i18next"

import { useGetHealthService, useGetConfig } from "@/api/queries"
import {
  useApplyConfigMutation,
  usePostServiceActionMutation,
  useRoutingControlPendingState,
} from "@/api/mutations"
import { selectConfigIsDraft } from "@/api/selectors"
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { cn } from "@/lib/utils"

export function WarningBanner({ className }: { className?: string }) {
  const { t } = useTranslation()
  const [applyMonitor, setApplyMonitor] = useState<{
    applyStartedTs: number
    startedAtMs: number
    nowMs: number
    timedOut: boolean
  } | null>(null)
  const configQuery = useGetConfig()
  const healthQuery = useGetHealthService({
    query: {
      refetchInterval: 30_000,
      refetchIntervalInBackground: false,
    },
  })
  const applyConfigMutation = useApplyConfigMutation({
    mutation: {
      onSuccess: (response) => {
        const applyStartedTs =
          response.status === 200
            ? (response.data.apply_started_ts ?? Math.floor(Date.now() / 1000))
            : Math.floor(Date.now() / 1000)
        setApplyMonitor({
          applyStartedTs,
          startedAtMs: Date.now(),
          nowMs: Date.now(),
          timedOut: false,
        })
      },
    },
  })
  const restartRoutingMutation = usePostServiceActionMutation("restart")
  const { anyPending, applyPending, restartPending } =
    useRoutingControlPendingState()

  const serviceHealth =
    healthQuery.data?.status === 200 ? healthQuery.data.data : null
  const isDraft =
    serviceHealth?.config_is_draft ?? selectConfigIsDraft(configQuery.data)
  const expectedResolverHash = serviceHealth?.resolver_config_hash
  const actualResolverHash = serviceHealth?.resolver_config_hash_actual
  const hasResolverHashMismatch =
    Boolean(expectedResolverHash) &&
    Boolean(actualResolverHash) &&
    expectedResolverHash !== actualResolverHash
  const resolverSyncState = serviceHealth?.resolver_config_sync_state
  const isResolverConverging = resolverSyncState === "converging"
  const isResolverStale = resolverSyncState === "stale" || hasResolverHashMismatch
  const isServiceRunning = serviceHealth?.status === "running"
  const progressPercent = useMemo(() => {
    if (!applyMonitor) {
      return 0
    }
    const elapsed = Math.max(0, applyMonitor.nowMs - applyMonitor.startedAtMs)
    return Math.min(100, (elapsed / 15_000) * 100)
  }, [applyMonitor])
  const formattedActualTs = formatUnixTimestampLabel(
    serviceHealth?.resolver_config_hash_actual_ts
  )

  useEffect(() => {
    if (!applyMonitor || applyMonitor.timedOut) {
      return
    }

    const timer = window.setInterval(() => {
      setApplyMonitor((current) => {
        if (!current || current.timedOut) {
          return current
        }
        const nowMs = Date.now()
        const timedOut = nowMs - current.startedAtMs >= 15_000
        return { ...current, nowMs, timedOut }
      })
      void healthQuery.refetch()
    }, 1_000)

    return () => window.clearInterval(timer)
  }, [applyMonitor, healthQuery.refetch])

  if (!isDraft && !isResolverStale && !isResolverConverging) {
    return null
  }

  return (
    <div className={cn("space-y-2", className)}>
      {isDraft ? (
        <Alert variant="warning">
          <AlertDescription>{t("warning.draftChanged")}</AlertDescription>
          <Button
            disabled={anyPending}
            onClick={() => applyConfigMutation.mutate()}
            size="xs"
            variant="outline"
            className="mt-2"
          >
            <SaveIcon className="mr-1 h-3 w-3" />
            {applyPending
              ? t("warning.actions.applying")
              : t("warning.actions.apply")}
          </Button>
        </Alert>
      ) : null}

      {applyMonitor && !applyMonitor.timedOut && isResolverConverging ? (
        <Alert>
          <AlertTitle>{t("warning.compact.waitingForReload")}</AlertTitle>
          <AlertDescription>
            {t("warning.compact.waitingForReloadDescription")}
          </AlertDescription>
          <div className="mt-2 h-2 rounded bg-muted">
            <div
              className="h-2 rounded bg-primary transition-[width] duration-700"
              style={{ width: `${progressPercent}%` }}
            />
          </div>
        </Alert>
      ) : null}

      {isResolverStale ? (
        <Alert variant="warning">
          <AlertTitle>{t("warning.compact.resolverStale")}</AlertTitle>
          {applyMonitor?.timedOut && formattedActualTs ? (
            <AlertDescription>
              {t("warning.compact.staleAfterTimeout", {
                actualTs: formattedActualTs,
              })}
            </AlertDescription>
          ) : null}
          <Button
            disabled={anyPending || !serviceHealth || !isServiceRunning}
            onClick={() => restartRoutingMutation.mutate()}
            size="xs"
            variant="outline"
            className="mt-2"
          >
            {restartPending
              ? t("warning.actions.restarting")
              : t("warning.actions.restart")}
          </Button>
        </Alert>
      ) : null}
    </div>
  )
}

function formatUnixTimestampLabel(value?: number): string | null {
  if (!value || !Number.isFinite(value)) {
    return null
  }
  try {
    return new Intl.DateTimeFormat(undefined, {
      dateStyle: "medium",
      timeStyle: "medium",
    }).format(new Date(value * 1000))
  } catch {
    return null
  }
}
