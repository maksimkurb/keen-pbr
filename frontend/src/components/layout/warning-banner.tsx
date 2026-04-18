import { useLayoutEffect, useRef } from "react"
import { SaveIcon } from "lucide-react"
import { useTranslation } from "react-i18next"

import { useApplyConfigMutation } from "@/api/mutations"
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { useSidebar } from "@/components/ui/sidebar-context"
import { cn } from "@/lib/utils"
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

  const isConverging = state.mode === "dnsmasq-converging"
  const isError = state.mode === "dnsmasq-error"

  return (
    <div
      ref={containerRef}
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
