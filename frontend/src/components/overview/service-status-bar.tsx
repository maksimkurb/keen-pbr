import { useState } from "react"
import { useTranslation } from "react-i18next"
import {
  Check,
  Loader2,
  Play,
  RotateCw,
  Square,
  TriangleAlert,
} from "lucide-react"

import type { HealthResponse } from "@/api/generated/model"
import {
  usePostServiceActionMutation,
  useRoutingControlPendingState,
} from "@/api/mutations"
import { Button } from "@/components/ui/button"
import { Skeleton } from "@/components/ui/skeleton"
import { cn } from "@/lib/utils"

type BusyKind = "start" | "stop" | "restart" | "apply"

export function ServiceStatusBar({
  health,
  isLoading,
  isError,
  issueCount,
  onShowIssues,
}: {
  health?: HealthResponse
  isLoading: boolean
  isError: boolean
  issueCount: number
  onShowIssues: () => void
}) {
  const { t } = useTranslation()
  const [confirmStop, setConfirmStop] = useState(false)
  const startMutation = usePostServiceActionMutation("start")
  const stopMutation = usePostServiceActionMutation("stop")
  const restartMutation = usePostServiceActionMutation("restart")
  const pending = useRoutingControlPendingState()

  const busy = getBusyKind(health, pending)
  const running = health?.status === "running" || health?.status === "degraded"
  const hasIssues = issueCount > 0 || health?.status === "degraded"
  const state: "ok" | "warn" | "off" = busy
    ? "off"
    : !health || !running
      ? isError
        ? "warn"
        : "off"
      : hasIssues
        ? "warn"
        : "ok"

  if (isLoading && !health) {
    return (
      <Shell state="off">
        <Skeleton className="size-9 rounded-full" />
        <div className="space-y-1.5">
          <Skeleton className="h-4 w-48" />
          <Skeleton className="h-3 w-64" />
        </div>
      </Shell>
    )
  }

  const Icon = busy
    ? Loader2
    : state === "ok"
      ? Check
      : state === "warn"
        ? TriangleAlert
        : Square

  const title = busy
    ? t(`overview.status.busy.${busy}`)
    : isError && !health
      ? t("overview.runtime.loadError")
      : !running
        ? t("overview.status.stopped")
        : issueCount > 0
          ? t("overview.status.issuesPrefix")
          : health?.status === "degraded"
            ? t("overview.status.degraded")
            : t("overview.status.ok")

  return (
    <Shell
      state={state}
      icon={<Icon className={cn("size-4", busy && "animate-spin")} />}
      actions={
        busy ? (
          <Button disabled type="button" variant="outline">
            <Loader2 className="animate-spin" />
            {t(`overview.status.busyAction.${busy}`)}
          </Button>
        ) : !health ? null : !running ? (
          <Button type="button" onClick={() => startMutation.mutate()}>
            <Play />
            {t("overview.runtime.actions.start")}
          </Button>
        ) : confirmStop ? (
          <>
            <span className="text-[13px] text-muted-foreground">
              {t("overview.status.confirmStop")}
            </span>
            <Button
              type="button"
              variant="destructive"
              onClick={() => {
                setConfirmStop(false)
                stopMutation.mutate()
              }}
            >
              {t("overview.status.confirmStopAction")}
            </Button>
            <Button
              type="button"
              variant="outline"
              onClick={() => setConfirmStop(false)}
            >
              {t("common.cancel")}
            </Button>
          </>
        ) : (
          <>
            <Button
              type="button"
              variant="outline"
              onClick={() => restartMutation.mutate()}
            >
              <RotateCw />
              {t("overview.runtime.actions.restart")}
            </Button>
            <Button
              type="button"
              variant="outline"
              onClick={() => setConfirmStop(true)}
            >
              <Square />
              {t("overview.runtime.actions.stop")}
            </Button>
          </>
        )
      }
    >
      <div className="min-w-0">
        <div className="text-[15px] font-semibold">
          {title}
          {!busy && running && issueCount > 0 ? (
            <>
              {" "}
              <button
                className="cursor-pointer font-semibold text-warning-foreground underline underline-offset-2"
                type="button"
                onClick={onShowIssues}
              >
                {t("overview.status.issueCount", { count: issueCount })}
              </button>
            </>
          ) : null}
        </div>
        {health ? (
          <div className="mt-px text-xs break-words text-muted-foreground">
            {t("overview.status.versionLine", {
              version: health.version,
              build: health.build,
              os: `${health.os_type} ${health.os_version}`.trim(),
            })}
          </div>
        ) : null}
      </div>
    </Shell>
  )
}

function Shell({
  state,
  icon,
  actions,
  children,
}: {
  state: "ok" | "warn" | "off"
  icon?: React.ReactNode
  actions?: React.ReactNode
  children: React.ReactNode
}) {
  return (
    <section className="flex flex-wrap items-center justify-between gap-4 rounded-xl bg-card px-4 py-3 ring-1 ring-foreground/10">
      <div className="flex min-w-0 items-center gap-3">
        {icon ? (
          <div
            className={cn(
              "flex size-9 shrink-0 items-center justify-center rounded-full",
              state === "ok" && "bg-success/12 text-success",
              state === "warn" && "bg-warning/15 text-warning-foreground",
              state === "off" && "bg-muted text-muted-foreground"
            )}
          >
            {icon}
          </div>
        ) : null}
        {children}
      </div>
      {actions ? (
        <div className="flex flex-wrap items-center gap-2">{actions}</div>
      ) : null}
    </section>
  )
}

function getBusyKind(
  health: HealthResponse | undefined,
  pending: ReturnType<typeof useRoutingControlPendingState>
): BusyKind | null {
  if (pending.startPending) return "start"
  if (pending.stopPending) return "stop"
  if (pending.restartPending) return "restart"
  if (pending.applyPending || pending.rollbackPending) return "apply"

  const operation = health?.lifecycle_operation
  if (operation?.status === "running") {
    if (operation.type === "start") return "start"
    if (operation.type === "stop") return "stop"
    if (operation.type === "restart") return "restart"
    return "apply"
  }

  if (health?.runtime_state === "starting") return "start"
  if (health?.runtime_state === "shutting_down") return "stop"
  if (health?.runtime_state === "applying") return "apply"
  return null
}
