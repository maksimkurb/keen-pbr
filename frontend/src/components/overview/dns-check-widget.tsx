import {
  AlertCircle,
  CheckCircle2,
  Loader2,
  RefreshCw,
  SquareTerminal,
} from "lucide-react"
import { useEffect, useMemo, useState } from "react"
import type { TFunction } from "i18next"
import { useTranslation } from "react-i18next"

import {
  type DnsCheckStatus,
  type DnsEventFailure,
  describeSseFailure,
  useDnsCheck,
} from "@/hooks/use-dns-check"
import { SectionCard } from "@/components/shared/section-card"
import { Button } from "@/components/ui/button"

import { DnsCheckModal } from "./dns-check-modal"

export function DnsCheckWidget({
  disabledReason,
  dnsProbeEnabled,
  markerDomain,
  onStatusChange,
  embedded = false,
  summary,
}: {
  disabledReason?: "config" | "runtime"
  dnsProbeEnabled: boolean
  markerDomain?: string
  onStatusChange?: (status: DnsCheckStatus) => void
  embedded?: boolean
  summary?: React.ReactNode
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

  const isChecking = status === "checking"
  const isDisabled = !dnsProbeEnabled
  const cardClassName = useMemo(() => {
    if (isDisabled) {
      return "border-border bg-muted/20"
    }

    switch (status) {
      case "browser-fail":
      case "sse-fail":
        return "border-destructive/40 bg-destructive/5"
      default:
        return undefined
    }
  }, [isDisabled, status])

  const runCheck = () => {
    reset()
    startCheck(true)
  }
  const checkAgainAction = (
    <Button
      aria-label={t("overview.dnsCheck.card.checkAgain")}
      className="h-auto min-h-7 max-w-full shrink-0 px-2 py-1 text-xs whitespace-normal"
      disabled={isChecking || isDisabled}
      onClick={runCheck}
      size="sm"
      variant="ghost"
    >
      <RefreshCw className="h-3.5 w-3.5 shrink-0" />
      <span className="min-w-0 break-words">
        {isChecking
          ? t("overview.dnsCheck.card.checking")
          : t("overview.dnsCheck.card.checkAgain")}
      </span>
    </Button>
  )

  const content = (
    <div className="flex h-full min-w-0 flex-1 flex-col space-y-4">
      {embedded ? (
        <div className="flex min-w-0 items-start justify-between gap-2">
          <h3 className="min-w-0 font-semibold break-words">
            {t("overview.dnsCheck.card.title")}
          </h3>
          {checkAgainAction}
        </div>
      ) : null}
      {embedded ? summary : null}
      <div className="flex min-h-20 min-w-0 items-center rounded-lg border border-border/60 bg-background/60 px-4 py-3">
        <DnsStatusSummary
          disabled={isDisabled}
          failure={failure}
          status={status}
        />
      </div>

      <div className="mt-auto flex flex-col gap-2">
        <Button
          className="w-full justify-start text-left whitespace-normal"
          disabled={isDisabled}
          onClick={() => setShowPcCheckDialog(true)}
          size="sm"
          variant="outline"
        >
          <SquareTerminal className="h-4 w-4 shrink-0" />
          {t("overview.dnsCheck.card.testFromPc")}
        </Button>
      </div>
    </div>
  )

  return (
    <>
      {embedded ? (
        <div className={cardClassName}>{content}</div>
      ) : (
        <SectionCard
          className={cardClassName}
          contentClassName="flex flex-1 flex-col"
          description={
            isDisabled
              ? t(
                  disabledReason === "runtime"
                    ? "overview.dnsCheck.card.runtimeDisabledDescription"
                    : "overview.dnsCheck.card.disabledDescription"
                )
              : t("overview.dnsCheck.card.description")
          }
          action={checkAgainAction}
          title={t("overview.dnsCheck.card.title")}
        >
          {content}
        </SectionCard>
      )}

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

function DnsStatusSummary({
  disabled,
  failure,
  status,
}: {
  disabled: boolean
  failure: DnsEventFailure | null
  status: ReturnType<typeof useDnsCheck>["status"]
}) {
  const { t } = useTranslation()
  if (disabled) {
    return (
      <DnsStatusMessage
        icon={<AlertCircle className="h-5 w-5 text-muted-foreground" />}
        text={t("overview.dnsCheck.status.disabled")}
        tone="muted"
      />
    )
  }

  switch (status) {
    case "success":
      return (
        <DnsStatusMessage
          icon={<CheckCircle2 className="h-5 w-5 text-emerald-600" />}
          text={t("overview.dnsCheck.status.browserSuccess")}
          tone="success"
        />
      )
    case "pc-success":
      return (
        <DnsStatusMessage
          icon={<CheckCircle2 className="h-5 w-5 text-emerald-600" />}
          text={t("overview.dnsCheck.status.manualProbeSuccess")}
          tone="success"
        />
      )
    case "browser-fail":
      return (
        <DnsStatusMessage
          icon={<AlertCircle className="h-5 w-5 text-destructive" />}
          text={t("overview.dnsCheck.status.browserProbeFail")}
          tone="error"
        />
      )
    case "sse-fail":
      return (
        <DnsStatusMessage
          icon={<AlertCircle className="h-5 w-5 text-destructive" />}
          text={sseFailureText(t, "sseUnavailable", failure)}
          tone="error"
        />
      )
    case "idle":
    case "checking":
      return (
        <div className="flex w-full items-center justify-center">
          <Loader2 className="h-5 w-5 animate-spin text-muted-foreground" />
        </div>
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

function DnsStatusMessage({
  icon,
  text,
  tone,
}: {
  icon: React.ReactNode
  text: string
  tone: "success" | "error" | "muted"
}) {
  return (
    <div
      className={
        tone === "success"
          ? "flex w-full min-w-0 items-center gap-2 text-emerald-700 dark:text-emerald-300"
          : tone === "error"
            ? "flex w-full min-w-0 items-center gap-2 text-destructive"
            : "flex w-full min-w-0 items-center gap-2 text-muted-foreground"
      }
    >
      {icon}
      <span className="min-w-0 break-words">{text}</span>
    </div>
  )
}
