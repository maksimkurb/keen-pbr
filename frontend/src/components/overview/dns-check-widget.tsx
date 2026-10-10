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
      <div className="px-3.5 pt-2.5">
        <DnsStatusNote
          disabled={isDisabled}
          disabledReason={disabledReason}
          failure={failure}
          status={status}
        />
      </div>
      <div className="flex flex-wrap gap-2 px-3.5 pt-2.5 pb-3.5">
        <Button
          disabled={isDisabled || isChecking}
          onClick={() => {
            reset()
            startCheck(true)
          }}
          size="sm"
          type="button"
          variant="outline"
        >
          <RefreshCw
            className={cn(!isDisabled && isChecking && "animate-spin")}
          />
          {t("overview.dnsCheck.card.checkAgain")}
        </Button>
        <Button
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
  disabled,
  disabledReason,
  failure,
  status,
}: {
  disabled: boolean
  disabledReason?: "config" | "runtime"
  failure: DnsEventFailure | null
  status: DnsCheckStatus
}) {
  const { t } = useTranslation()

  if (disabled) {
    return (
      <PanelNote tone={disabledReason === "runtime" ? "warn" : "muted"}>
        {disabledReason === "runtime"
          ? t("overview.dnsCheck.status.runtimeDisabled")
          : t("overview.dnsCheck.status.disabled")}
      </PanelNote>
    )
  }

  switch (status) {
    case "success":
      return (
        <PanelNote tone="ok">
          ✓ {t("overview.dnsCheck.status.browserSuccess")}
        </PanelNote>
      )
    case "pc-success":
      return (
        <PanelNote tone="ok">
          ✓ {t("overview.dnsCheck.status.manualProbeSuccess")}
        </PanelNote>
      )
    case "browser-fail":
      return (
        <PanelNote tone="bad">
          {t("overview.dnsCheck.status.browserProbeFail")}
        </PanelNote>
      )
    case "sse-fail":
      return (
        <PanelNote tone="bad">
          {sseFailureText(t, "sseUnavailable", failure)}
        </PanelNote>
      )
    case "idle":
    case "checking":
      return (
        <PanelNote tone="muted">
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
