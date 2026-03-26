import { AlertCircle, CheckCircle2, Loader2, RefreshCw, SquareTerminal } from "lucide-react"
import { useEffect, useMemo, useState } from "react"
import { useTranslation } from "react-i18next"

import { useDnsCheck } from "@/hooks/use-dns-check"
import { SectionCard } from "@/components/shared/section-card"
import { Button } from "@/components/ui/button"

import { DnsCheckModal } from "./dns-check-modal"

export function DnsCheckWidget({
  dnsProbeEnabled,
}: {
  dnsProbeEnabled: boolean
}) {
  const { t } = useTranslation()
  const [showPcCheckDialog, setShowPcCheckDialog] = useState(false)
  const { status, startCheck, reset } = useDnsCheck()

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

  return (
    <>
      <SectionCard
        className={cardClassName}
        description={
          isDisabled
            ? t("overview.dnsCheck.card.disabledDescription")
            : t("overview.dnsCheck.card.description")
        }
        title={t("overview.dnsCheck.card.title")}
      >
        <div className="space-y-4">
          <div className="flex min-h-20 items-center rounded-lg border border-border/60 bg-background/60 px-4 py-3">
            <DnsStatusSummary disabled={isDisabled} status={status} />
          </div>

          <div className="grid gap-2 sm:grid-cols-2">
            <Button
              disabled={isChecking || isDisabled}
              onClick={() => {
                reset()
                startCheck(true)
              }}
              size="sm"
              variant="outline"
            >
              <RefreshCw className="h-4 w-4" />
              {isChecking
                ? t("overview.dnsCheck.card.checking")
                : t("overview.dnsCheck.card.runAgain")}
            </Button>
            <Button
              disabled={isDisabled}
              onClick={() => setShowPcCheckDialog(true)}
              size="sm"
              variant="outline"
            >
              <SquareTerminal className="h-4 w-4" />
              {t("overview.dnsCheck.card.testFromPc")}
            </Button>
          </div>
        </div>
      </SectionCard>

      <DnsCheckModal
        browserStatus={status}
        onOpenChange={setShowPcCheckDialog}
        open={showPcCheckDialog}
      />
    </>
  )
}

function DnsStatusSummary({
  disabled,
  status,
}: {
  disabled: boolean
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
      return <DnsStatusMessage icon={<CheckCircle2 className="h-5 w-5 text-emerald-600" />} text={t("overview.dnsCheck.status.browserSuccess")} tone="success" />
    case "pc-success":
      return <DnsStatusMessage icon={<CheckCircle2 className="h-5 w-5 text-emerald-600" />} text={t("overview.dnsCheck.status.manualProbeSuccess")} tone="success" />
    case "browser-fail":
      return <DnsStatusMessage icon={<AlertCircle className="h-5 w-5 text-destructive" />} text={t("overview.dnsCheck.status.browserProbeFail")} tone="error" />
    case "sse-fail":
      return <DnsStatusMessage icon={<AlertCircle className="h-5 w-5 text-destructive" />} text={t("overview.dnsCheck.status.sseUnavailable")} tone="error" />
    case "idle":
    case "checking":
      return (
        <div className="flex w-full items-center justify-center">
          <Loader2 className="h-5 w-5 animate-spin text-muted-foreground" />
        </div>
      )
  }
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
          ? "flex w-full items-center gap-2 text-emerald-700 dark:text-emerald-300"
          : tone === "error"
            ? "flex w-full items-center gap-2 text-destructive"
            : "flex w-full items-center gap-2 text-muted-foreground"
      }
    >
      {icon}
      <span>{text}</span>
    </div>
  )
}
