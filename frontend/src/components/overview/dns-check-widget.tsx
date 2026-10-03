import {
  AlertCircle,
  CheckCircle2,
  Loader2,
  RefreshCw,
  SquareTerminal,
} from "lucide-react"
import { useEffect, useMemo, useState } from "react"
import { useTranslation } from "react-i18next"

import {
  type DnsCheckStatus,
  type InterceptMonitorStatus,
  useDnsCheck,
} from "@/hooks/use-dns-check"
import type { DnsTestInterceptEvent } from "@/api/generated/model"
import { SectionCard } from "@/components/shared/section-card"
import { Button } from "@/components/ui/button"

import { DnsCheckModal } from "./dns-check-modal"

export function DnsCheckWidget({
  disabledReason,
  dnsProbeEnabled,
  liveEvent,
  liveMonitorStatus,
  markerDomain,
  onStatusChange,
}: {
  disabledReason?: "config" | "runtime"
  dnsProbeEnabled: boolean
  liveEvent?: DnsTestInterceptEvent | null
  liveMonitorStatus?: InterceptMonitorStatus
  markerDomain?: string
  onStatusChange?: (status: DnsCheckStatus) => void
}) {
  const { t } = useTranslation()
  const [showPcCheckDialog, setShowPcCheckDialog] = useState(false)
  const { lastEvent, status, startCheck, reset } = useDnsCheck(markerDomain)

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
  const observedEvent = liveEvent ?? lastEvent

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
        title={t("overview.dnsCheck.card.title")}
      >
        <div className="flex h-full flex-1 flex-col space-y-4">
          <div className="flex min-h-20 items-center rounded-lg border border-border/60 bg-background/60 px-4 py-3">
            <DnsStatusSummary disabled={isDisabled} status={status} />
          </div>

          {liveMonitorStatus && liveMonitorStatus !== "disabled" ? (
            <InterceptMonitorStatusMessage status={liveMonitorStatus} />
          ) : null}

          {observedEvent ? (
            <InterceptEventSummary event={observedEvent} />
          ) : null}

          <div className="mt-auto grid gap-2 sm:grid-cols-2">
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
        markerDomain={markerDomain}
        onOpenChange={setShowPcCheckDialog}
        open={showPcCheckDialog}
      />
    </>
  )
}

function InterceptMonitorStatusMessage({
  status,
}: {
  status: Exclude<InterceptMonitorStatus, "disabled">
}) {
  const { t } = useTranslation()
  const text = t(`overview.dnsCheck.monitor.${status}`)
  const className =
    status === "connected"
      ? "text-emerald-700 dark:text-emerald-300"
      : status === "error"
        ? "text-destructive"
        : "text-muted-foreground"

  return <div className={`text-xs ${className}`}>{text}</div>
}

function InterceptEventSummary({ event }: { event: DnsTestInterceptEvent }) {
  const { t } = useTranslation()
  const values = [
    ["source", event.source],
    ["domain", event.domain],
    ["lists", event.lists.join(", ") || t("common.noneShort")],
    ["ips", event.ips.join(", ") || t("common.noneShort")],
    ["hold_us", String(event.hold_us)],
    ["timed_out", event.timed_out ? t("common.enabled") : t("common.disabled")],
  ] as const

  return (
    <div className="rounded-lg border border-border/60 bg-muted/20 px-4 py-3 text-sm">
      <div className="mb-2 font-medium">
        {t("overview.dnsCheck.event.title")}
      </div>
      <dl className="grid gap-x-4 gap-y-1 sm:grid-cols-2">
        {values.map(([name, value]) => (
          <div className="flex min-w-0 justify-between gap-3" key={name}>
            <dt className="text-muted-foreground">
              {t(`overview.dnsCheck.event.${name}`)}
            </dt>
            <dd className="truncate text-right font-mono text-xs">{value}</dd>
          </div>
        ))}
      </dl>
    </div>
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
          text={t("overview.dnsCheck.status.sseUnavailable")}
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
