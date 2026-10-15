import { Loader2, RefreshCw, SquareTerminal } from "lucide-react"
import { useEffect, useState } from "react"
import type { TFunction } from "i18next"
import { useTranslation } from "react-i18next"

import {
  type DnsCheckStatus,
  type DnsEventFailure,
  describeSseFailure,
  useDnsCheck,
} from "@/hooks/use-dns-check"
import { Button } from "@/components/ui/button"
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip"
import { cn } from "@/lib/utils"

import { DnsCheckModal } from "./dns-check-modal"
import { PanelNote } from "./overview-panel"

export function DnsCheckWidget({
  disabledReason,
  dnsProbeEnabled,
  markerDomain,
  onStatusChange,
}: {
  disabledReason?: "config" | "runtime"
  dnsProbeEnabled: boolean
  markerDomain?: string
  onStatusChange?: (status: DnsCheckStatus) => void
}) {
  const { t } = useTranslation()
  const [showPcCheckDialog, setShowPcCheckDialog] = useState(false)
  const { status, failure, startCheck, reset } = useDnsCheck(markerDomain)

  useEffect(() => {
    onStatusChange?.(status)
  }, [onStatusChange, status])

  useEffect(() => {
    if (!dnsProbeEnabled) {
      reset()
      return
    }

    startCheck(true)
  }, [dnsProbeEnabled, reset, startCheck])

  const isChecking = status === "checking" || status === "idle"
  const isDisabled = !dnsProbeEnabled

  return (
    <>
      {/* Re-check sits next to the status it refreshes; the wide action
          gets the full card width so neither label wraps. */}
      <div className="flex items-start gap-2 px-3.5 pt-2.5">
        <div className="min-w-0 flex-1">
          <DnsStatusNote
            // Two lines tall at minimum: the one-line "checking" note and the
            // two-line result must not shift the card on every re-check.
            className="min-h-[calc(2lh+1rem+2px)]"
            disabled={isDisabled}
            disabledReason={disabledReason}
            failure={failure}
            status={status}
          />
        </div>
        <Tooltip>
          <TooltipTrigger
            render={
              <Button
                aria-label={t("overview.dnsCheck.card.checkAgain")}
                disabled={isDisabled || isChecking}
                onClick={() => {
                  reset()
                  startCheck(true)
                }}
                size="icon"
                type="button"
                variant="outline"
              />
            }
          >
            <RefreshCw
              className={cn(!isDisabled && isChecking && "animate-spin")}
            />
          </TooltipTrigger>
          <TooltipContent>
            {t("overview.dnsCheck.card.checkAgain")}
          </TooltipContent>
        </Tooltip>
      </div>
      <div className="px-3.5 pt-2.5 pb-3.5">
        <Button
          className="w-full"
          disabled={isDisabled}
          onClick={() => setShowPcCheckDialog(true)}
          size="sm"
          type="button"
          variant="outline"
        >
          <SquareTerminal />
          {t("overview.dnsCheck.card.testFromPc")}
        </Button>
      </div>

      <DnsCheckModal
        browserFailure={failure}
        browserStatus={status}
        markerDomain={markerDomain}
        onOpenChange={setShowPcCheckDialog}
        open={showPcCheckDialog}
      />
    </>
  )
}

function DnsStatusNote({
  className,
  disabled,
  disabledReason,
  failure,
  status,
}: {
  className?: string
  disabled: boolean
  disabledReason?: "config" | "runtime"
  failure: DnsEventFailure | null
  status: DnsCheckStatus
}) {
  const { t } = useTranslation()

  if (disabled) {
    return (
      <PanelNote
        className={className}
        tone={disabledReason === "runtime" ? "warn" : "muted"}
      >
        {disabledReason === "runtime"
          ? t("overview.dnsCheck.status.runtimeDisabled")
          : t("overview.dnsCheck.status.disabled")}
      </PanelNote>
    )
  }

  switch (status) {
    case "success":
      return (
        <PanelNote className={className} tone="ok">
          ✓ {t("overview.dnsCheck.status.browserSuccess")}
        </PanelNote>
      )
    case "pc-success":
      return (
        <PanelNote className={className} tone="ok">
          ✓ {t("overview.dnsCheck.status.manualProbeSuccess")}
        </PanelNote>
      )
    case "browser-fail":
      return (
        <PanelNote className={className} tone="bad">
          {t("overview.dnsCheck.status.browserProbeFail")}
        </PanelNote>
      )
    case "sse-fail":
      return (
        <PanelNote className={className} tone="bad">
          {sseFailureText(t, "sseUnavailable", failure)}
        </PanelNote>
      )
    case "idle":
    case "checking":
      return (
        <PanelNote className={className} tone="muted">
          <Loader2 className="size-4 animate-spin" />
          {t("overview.dnsCheck.status.browserChecking")}
        </PanelNote>
      )
  }
}

function sseFailureText(
  t: TFunction,
  fallbackKey: string,
  failure: DnsEventFailure | null
) {
  const { key, params } = describeSseFailure(failure)
  return key
    ? t(`overview.dnsCheck.status.${key}`, params)
    : t(`overview.dnsCheck.status.${fallbackKey}`)
}
