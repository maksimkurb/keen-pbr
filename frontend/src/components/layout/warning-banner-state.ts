import { useEffect, useMemo, useState } from "react"
import { useTranslation } from "react-i18next"

import type {
  HealthResponse,
  LifecycleOperation,
  LifecycleOperationType,
} from "@/api/generated/model"
import { useRoutingControlPendingState } from "@/api/mutations"
import { useGetHealthService } from "@/api/queries"

const SUCCESS_RETENTION_MS = 1_500

export type WarningBannerMode =
  | "hidden"
  | "draft"
  | "draft-and-dnsmasq"
  | "dnsmasq-stale"
  | "dnsmasq-error"
  | "lifecycle-running"
  | "lifecycle-success"
  | "lifecycle-error"

export type WarningBannerStep = {
  id: string
  status: "failed" | "pending" | "running" | "skipped" | "succeeded"
  title: string
}

export type WarningBannerState = {
  actionPending: boolean
  dismissFailure: () => void
  hasDraftConfig: boolean
  isActionDisabled: boolean
  isVisible: boolean
  mode: WarningBannerMode
  operationType?: LifecycleOperationType
  operationSteps: WarningBannerStep[]
}

export function useWarningBannerState(): WarningBannerState {
  const { t } = useTranslation()
  const healthQuery = useGetHealthService()
  const { anyPending } = useRoutingControlPendingState()
  const serviceHealth =
    healthQuery.data?.status === 200 ? healthQuery.data.data : null
  const observedOperation = serviceHealth?.lifecycle_operation
  const [retainedOperation, setRetainedOperation] =
    useState<LifecycleOperation | null>(null)
  const [dismissedId, setDismissedId] = useState<string | null>(null)

  useEffect(() => {
    if (!observedOperation) return
    const timer = window.setTimeout(() => {
      setRetainedOperation((previous) =>
        retainLifecycleOperation(previous, observedOperation)
      )
    }, 0)
    return () => window.clearTimeout(timer)
  }, [observedOperation])

  useEffect(() => {
    if (retainedOperation?.status !== "succeeded") return
    const id = retainedOperation.id
    const timer = window.setTimeout(() => {
      setRetainedOperation((current) => (current?.id === id ? null : current))
    }, SUCCESS_RETENTION_MS)
    return () => window.clearTimeout(timer)
  }, [retainedOperation])

  const visibleOperation =
    retainedOperation?.status === "failed" &&
    (dismissedId === retainedOperation.id ||
      sessionStorage.getItem(
        `keen-pbr.lifecycle.dismissed.${retainedOperation.id}`
      ))
      ? null
      : retainedOperation
  const mode =
    retainedOperation && !visibleOperation
      ? "hidden"
      : getWarningBannerMode(serviceHealth, visibleOperation)
  const operationSteps = useMemo(
    () =>
      visibleOperation?.stages.map((stage) => ({
        id: stage.id,
        status: stage.status,
        title: t(`lifecycle.stages.${stage.id}`, {
          defaultValue: stage.title,
        }),
      })) ?? [],
    [t, visibleOperation]
  )
  return {
    actionPending: anyPending,
    dismissFailure: () => {
      if (retainedOperation?.status === "failed") {
        sessionStorage.setItem(
          `keen-pbr.lifecycle.dismissed.${retainedOperation.id}`,
          "1"
        )
        setDismissedId(retainedOperation.id)
      }
    },
    hasDraftConfig: serviceHealth?.config_is_draft ?? false,
    isActionDisabled:
      anyPending || visibleOperation?.status === "running" || !serviceHealth,
    isVisible: mode !== "hidden",
    mode,
    operationType: visibleOperation?.type,
    operationSteps,
  }
}

export function getWarningBannerMode(
  serviceHealth: HealthResponse | null,
  retainedOperation: LifecycleOperation | null =
    serviceHealth?.lifecycle_operation ?? null
): WarningBannerMode {
  if (retainedOperation?.status === "running") return "lifecycle-running"
  if (retainedOperation?.status === "succeeded") return "lifecycle-success"
  if (retainedOperation?.status === "failed") return "lifecycle-error"
  if (!serviceHealth) return "hidden"

  if (serviceHealth.config_is_draft) {
    return serviceHealth.resolver_config_sync_state === "stale"
      ? "draft-and-dnsmasq"
      : "draft"
  }
  if (serviceHealth.resolver_config_sync_state === "stale") {
    return "dnsmasq-stale"
  }
  if (serviceHealth.resolver_config_probe_status === "query_failed") {
    return "dnsmasq-error"
  }
  return "hidden"
}

export function retainLifecycleOperation(
  previous: LifecycleOperation | null,
  incoming: LifecycleOperation | null | undefined
): LifecycleOperation | null {
  if (!incoming) return previous
  if (
    incoming.status === "succeeded" &&
    (!previous || previous.id !== incoming.id)
  ) {
    return null
  }
  if (!previous || previous.id !== incoming.id) return incoming
  const rank = { pending: 0, running: 1, skipped: 2, succeeded: 2, failed: 2 }
  const previousStages = new Map(previous.stages.map((stage) => [stage.id, stage]))
  return {
    ...incoming,
    stages: incoming.stages.map((stage) => {
      const old = previousStages.get(stage.id)
      return old && rank[old.status] > rank[stage.status] ? old : stage
    }),
  }
}
