import { useLayoutEffect, useRef } from "react"
import {
  CircleCheckBigIcon,
  CircleIcon,
  CircleXIcon,
  LoaderCircleIcon,
  SaveIcon,
  XIcon,
} from "lucide-react"
import { useTranslation } from "react-i18next"

import {
  useApplyConfigMutation,
  usePostServiceActionMutation,
} from "@/api/mutations"
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { useSidebar } from "@/components/ui/sidebar-context"
import { cn } from "@/lib/utils"
import type { LifecycleOperationType } from "@/api/generated/model"
import type {
  WarningBannerMode,
  WarningBannerState,
} from "@/components/layout/warning-banner-state"

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
  const restartServiceMutation = usePostServiceActionMutation("restart")
  const containerRef = useRef<HTMLDivElement>(null)

  useLayoutEffect(() => {
    const rootStyle = document.documentElement.style

    if (!state.isVisible) {
      rootStyle.setProperty("--warning-banner-height", "0px")
      return
    }

    const element = containerRef.current

    if (!element) {
      return
    }

    const updateHeight = () => {
      rootStyle.setProperty(
        "--warning-banner-height",
        `${element.getBoundingClientRect().height}px`
      )
    }

    updateHeight()

    const resizeObserver = new ResizeObserver(updateHeight)
    resizeObserver.observe(element)

    return () => {
      resizeObserver.disconnect()
      rootStyle.setProperty("--warning-banner-height", "0px")
    }
  }, [state.isVisible])

  if (!state.isVisible) {
    return null
  }

  const isLifecycleRunning = state.mode === "lifecycle-running"
  const isLifecycleSuccess = state.mode === "lifecycle-success"
  const isProgressing = isLifecycleRunning || isLifecycleSuccess
  const isError = state.mode === "dnsmasq-error" || state.mode === "lifecycle-error"
  const handleApplyAndReload = () => {
    if (state.hasDraftConfig) {
      applyConfigMutation.mutate()
      return
    }

    restartServiceMutation.mutate()
  }

  return (
    <div
      ref={containerRef}
      className={cn(
        "pointer-events-none fixed inset-x-0 bottom-0 z-50 px-4 pb-5",
        !isMobile && open
          ? "right-4 left-[calc(var(--sidebar-width)+1rem)]"
          : null,
        className
      )}
    >
      <Alert
        variant={isError ? "destructive" : isProgressing ? "default" : "warning"}
        className="pointer-events-auto mx-auto w-full max-w-7xl gap-4 rounded-2xl border border-white/10 px-4 py-4 shadow-[0_10px_30px_hsl(0_0%_0%/0.18),0_24px_80px_hsl(0_0%_0%/0.4)] ring-1 ring-black/5 dark:border-white/12 dark:ring-white/6"
      >
        <div className="flex flex-col gap-3 sm:flex-row sm:items-center sm:justify-between">
          <div className="min-w-0 space-y-1">
            <AlertTitle>
              {t(getWarningBannerTitleKey(state.mode, state.operationType))}
            </AlertTitle>
            {!isProgressing ? (
              <AlertDescription>
                {t(getWarningBannerDescriptionKey(state.mode))}
              </AlertDescription>
            ) : null}
          </div>

          {!isProgressing ? (
            <div className="flex shrink-0 gap-2">
            <Button
              disabled={state.isActionDisabled}
              onClick={handleApplyAndReload}
              size="lg"
              variant="outline"
              className="shrink-0 bg-background hover:bg-background dark:bg-card dark:hover:bg-card"
            >
              <SaveIcon className="mr-1 h-4 w-4" />
              {state.actionPending
                ? t("warning.actions.applyingAndRestarting")
                : t("warning.actions.applyAndRestart")}
            </Button>
            {state.mode === "lifecycle-error" ? (
              <Button
                aria-label={t("common.close")}
                onClick={state.dismissFailure}
                size="icon-lg"
                variant="ghost"
              >
                <XIcon className="h-4 w-4" />
              </Button>
            ) : null}
            </div>
          ) : null}
        </div>

        {isProgressing ? (
          <ol className="space-y-3 text-sm">
            {state.operationSteps.map((step, index) => (
              <li
                className={cn(
                  "relative grid grid-cols-[20px_minmax(0,1fr)] items-center gap-2",
                  index < state.operationSteps.length - 1 &&
                    "after:absolute after:top-5 after:bottom-[-0.75rem] after:left-[9px] after:w-px after:bg-border"
                )}
                key={step.id}
              >
                <StepIcon status={step.status} />
                <span>{step.title}</span>
              </li>
            ))}
          </ol>
        ) : null}
      </Alert>
    </div>
  )
}

function StepIcon({ status }: { status: WarningBannerState["operationSteps"][number]["status"] }) {
  const markerClassName =
    "relative z-10 flex size-5 shrink-0 items-center justify-center rounded-full"

  if (status === "succeeded") {
    return <CircleCheckBigIcon className="relative z-10 size-5 shrink-0 text-emerald-600" />
  }
  if (status === "running") {
    return <LoaderCircleIcon className="relative z-10 size-5 shrink-0 animate-spin text-primary" />
  }
  if (status === "failed") {
    return <CircleXIcon className="relative z-10 size-5 shrink-0 text-destructive" />
  }
  return (
    <span className={cn(markerClassName, "bg-card text-muted-foreground")}>
      <CircleIcon className="size-4" />
    </span>
  )
}

function getWarningBannerTitleKey(
  mode: WarningBannerMode,
  operationType?: LifecycleOperationType
) {
  switch (mode) {
    case "draft":
      return "warning.compact.keenRestartRequired"
    case "draft-and-dnsmasq":
      return "warning.compact.keenAndDnsmasqRestartRequired"
    case "dnsmasq-stale":
      return "warning.compact.dnsmasqRestartRequired"
    case "dnsmasq-error":
      return "warning.compact.dnsmasqUnavailable"
    case "lifecycle-running":
      return getLifecycleOperationTitleKey("running", operationType)
    case "lifecycle-success":
      return getLifecycleOperationTitleKey("succeeded", operationType)
    case "lifecycle-error":
      return getLifecycleOperationTitleKey("failed", operationType)
    case "hidden":
      return "warning.compact.keenRestartRequired"
  }
}

function getLifecycleOperationTitleKey(
  status: "running" | "succeeded" | "failed",
  operationType?: LifecycleOperationType
) {
  if (status === "running") {
    switch (operationType) {
      case "apply_config":
        return "warning.compact.runtimeApplying"
      case "start":
        return "warning.compact.runtimeStarting"
      case "stop":
        return "warning.compact.runtimeStopping"
      default:
        return "warning.compact.runtimeReloading"
    }
  }

  const suffix = status === "succeeded" ? "Succeeded" : "Failed"
  switch (operationType) {
    case "apply_config":
      return `warning.compact.runtimeApply${suffix}`
    case "start":
      return `warning.compact.runtimeStart${suffix}`
    case "stop":
      return `warning.compact.runtimeStop${suffix}`
    default:
      return `warning.compact.runtimeReload${suffix}`
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
    case "dnsmasq-error":
      return "warning.compact.dnsmasqUnavailableDescription"
    case "lifecycle-running":
      return "warning.compact.runtimeReloadingDescription"
    case "lifecycle-success":
      return "warning.compact.runtimeReloadSucceededDescription"
    case "lifecycle-error":
      return "warning.compact.runtimeReloadFailedDescription"
    case "hidden":
      return "warning.compact.keenRestartRequiredDescription"
  }
}
