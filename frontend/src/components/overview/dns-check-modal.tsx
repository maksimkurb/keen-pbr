import { AlertCircle, CheckCircle2, Loader2 } from "lucide-react"
import { useEffect } from "react"
import { useTranslation } from "react-i18next"

import type { DnsCheckStatus, DnsEventFailure } from "@/hooks/use-dns-check"
import {
  DNS_CHECK_DOMAIN_SUFFIX,
  normalizeDnsMarkerDomain,
  describeSseFailure,
  useDnsCheck,
} from "@/hooks/use-dns-check"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { CommandCopyField } from "@/components/shared/command-copy-field"
import { ResponsiveDialog } from "@/components/shared/responsive-dialog"

export function DnsCheckModal({
  open,
  onOpenChange,
  browserFailure,
  browserStatus,
  markerDomain,
}: {
  open: boolean
  onOpenChange: (open: boolean) => void
  browserFailure?: DnsEventFailure | null
  browserStatus: DnsCheckStatus
  markerDomain?: string
}) {
  const { t } = useTranslation()
  const {
    status: pcStatus,
    checkState: pcCheckState,
    startCheck: startPcCheck,
    reset: resetPcCheck,
  } = useDnsCheck(markerDomain)
  useEffect(() => {
    if (open) {
      startPcCheck(false)
    }
  }, [open, startPcCheck])

  const command = pcCheckState.randomString
    ? `nslookup ${pcCheckState.randomString}.${normalizeDnsMarkerDomain(markerDomain || DNS_CHECK_DOMAIN_SUFFIX)}`
    : ""

  const isBrowserSuccess = browserStatus === "success"
  const isPcSuccess = pcStatus === "pc-success"
  const handleClose = () => {
    resetPcCheck()
    onOpenChange(false)
  }

  return (
    <ResponsiveDialog
      onOpenChange={(nextOpen) => {
        onOpenChange(nextOpen)
        if (!nextOpen) {
          resetPcCheck()
        }
      }}
      open={open}
      className="sm:max-w-xl"
      title={t("overview.dnsCheck.modal.title")}
      description={t("overview.dnsCheck.modal.description")}
    >
      <div className="space-y-4">
        <div className="space-y-2 text-sm">
          <StatusLine
            icon={
              isBrowserSuccess ? (
                <CheckCircle2 className="h-4 w-4 text-emerald-600" />
              ) : browserStatus === "checking" ? (
                <Loader2 className="h-4 w-4 animate-spin" />
              ) : (
                <AlertCircle className="h-4 w-4 text-destructive" />
              )
            }
            text={getBrowserStatusText(
              browserStatus,
              browserFailure ?? null,
              t
            )}
          />
          <StatusLine
            icon={
              isPcSuccess ? (
                <CheckCircle2 className="h-4 w-4 text-emerald-600" />
              ) : pcCheckState.waiting ? (
                <Loader2 className="h-4 w-4 animate-spin" />
              ) : (
                <AlertCircle className="h-4 w-4 text-muted-foreground" />
              )
            }
            text={getPcStatusText(isPcSuccess, pcCheckState.waiting, t)}
          />
        </div>

        {pcCheckState.waiting && command ? (
          <div className="space-y-2">
            <div className="text-sm text-muted-foreground">
              {t("overview.dnsCheck.modal.copyCommand")}
            </div>
            <CommandCopyField
              key={command}
              command={command}
              ariaLabel={t("overview.dnsCheck.modal.copyAria")}
            />
          </div>
        ) : null}

        {pcCheckState.showWarning ? (
          <Alert className="border-amber-500/30 bg-amber-500/5 text-amber-700 dark:text-amber-300">
            <AlertCircle className="text-amber-600 dark:text-amber-300" />
            <AlertDescription className="text-amber-700 dark:text-amber-300">
              {t("overview.dnsCheck.modal.warning")}
            </AlertDescription>
          </Alert>
        ) : null}

        {isPcSuccess ? (
          <Button className="w-full" onClick={handleClose} variant="outline">
            {t("common.close")}
          </Button>
        ) : null}
      </div>
    </ResponsiveDialog>
  )
}

function StatusLine({ icon, text }: { icon: React.ReactNode; text: string }) {
  return (
    <div className="flex items-center gap-2">
      {icon}
      <span>{text}</span>
    </div>
  )
}

function getBrowserStatusText(
  status: DnsCheckStatus,
  failure: DnsEventFailure | null,
  t: (key: string, params?: Record<string, unknown>) => string
) {
  switch (status) {
    case "success":
      return t("overview.dnsCheck.status.browserSuccess")
    case "browser-fail":
      return t("overview.dnsCheck.status.browserFail")
    case "sse-fail": {
      const { key, params } = describeSseFailure(failure)
      return t(`overview.dnsCheck.status.${key ?? "sseFail"}`, params)
    }
    case "checking":
      return t("overview.dnsCheck.status.browserChecking")
    default:
      return t("overview.dnsCheck.status.browserUnknown")
  }
}

function getPcStatusText(
  isPcSuccess: boolean,
  isWaiting: boolean,
  t: (key: string) => string
) {
  if (isPcSuccess) {
    return t("overview.dnsCheck.status.manualSuccess")
  }

  if (isWaiting) {
    return t("overview.dnsCheck.status.manualWaiting")
  }

  return t("overview.dnsCheck.status.manualIncomplete")
}
