import { AlertCircle, CheckCircle2, Copy, Loader2, Terminal } from "lucide-react"
import { useEffect, useState } from "react"
import { useTranslation } from "react-i18next"

import type { DnsCheckStatus } from "@/hooks/use-dns-check"
import { DNS_CHECK_DOMAIN_SUFFIX, useDnsCheck } from "@/hooks/use-dns-check"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog"
import {
  InputGroup,
  InputGroupAddon,
  InputGroupButton,
  InputGroupInput,
  InputGroupText,
} from "@/components/ui/input-group"
import { Tooltip, TooltipContent, TooltipTrigger } from "@/components/ui/tooltip"

export function DnsCheckModal({
  open,
  onOpenChange,
  browserStatus,
}: {
  open: boolean
  onOpenChange: (open: boolean) => void
  browserStatus: DnsCheckStatus
}) {
  const { t } = useTranslation()
  const {
    status: pcStatus,
    checkState: pcCheckState,
    startCheck: startPcCheck,
    reset: resetPcCheck,
  } = useDnsCheck()
  useEffect(() => {
    if (open) {
      startPcCheck(false)
    }
  }, [open, startPcCheck])

  const command = pcCheckState.randomString
    ? `nslookup ${pcCheckState.randomString}.${DNS_CHECK_DOMAIN_SUFFIX}`
    : ""

  const isBrowserSuccess = browserStatus === "success"
  const isPcSuccess = pcStatus === "pc-success"

  return (
    <Dialog
      onOpenChange={(nextOpen) => {
        onOpenChange(nextOpen)
        if (!nextOpen) {
          resetPcCheck()
        }
      }}
      open={open}
    >
      <DialogContent>
        <DialogHeader>
          <DialogTitle>{t("overview.dnsCheck.modal.title")}</DialogTitle>
          <DialogDescription>{t("overview.dnsCheck.modal.description")}</DialogDescription>
        </DialogHeader>

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
              text={getBrowserStatusText(browserStatus, t)}
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
              <CommandCopyField key={command} command={command} />
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
            <Button className="w-full" onClick={() => onOpenChange(false)} variant="outline">
              {t("common.close")}
            </Button>
          ) : null}
        </div>
      </DialogContent>
    </Dialog>
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

function CommandCopyField({ command }: { command: string }) {
  const { t } = useTranslation()
  const [copyFeedback, setCopyFeedback] = useState<"idle" | "copied" | "failed">(
    "idle"
  )

  return (
    <InputGroup>
      <InputGroupAddon>
        <InputGroupText>
          <Terminal className="h-4 w-4" />
        </InputGroupText>
      </InputGroupAddon>
      <InputGroupInput
        className="cursor-pointer font-mono text-sm"
        onClick={(event) => {
          event.currentTarget.select()
          void copyCommand(command, setCopyFeedback)
        }}
        readOnly
        value={command}
      />
      <InputGroupAddon align="inline-end">
        <Tooltip>
          <TooltipTrigger
            render={
              <InputGroupButton
                aria-label={t("overview.dnsCheck.modal.copyAria")}
                onClick={() => void copyCommand(command, setCopyFeedback)}
                size="icon-xs"
              />
            }
          >
            <Copy />
          </TooltipTrigger>
          <TooltipContent>
            {copyFeedback === "copied"
              ? t("common.copied")
              : copyFeedback === "failed"
                ? t("common.clipboardUnavailable")
                : t("common.copy")}
          </TooltipContent>
        </Tooltip>
      </InputGroupAddon>
    </InputGroup>
  )
}

function getBrowserStatusText(status: DnsCheckStatus, t: (key: string) => string) {
  switch (status) {
    case "success":
      return t("overview.dnsCheck.status.browserSuccess")
    case "browser-fail":
      return t("overview.dnsCheck.status.browserFail")
    case "sse-fail":
      return t("overview.dnsCheck.status.sseFail")
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

async function copyCommand(
  command: string,
  setCopyFeedback: (value: "idle" | "copied" | "failed") => void
) {
  if (!navigator.clipboard?.writeText) {
    setCopyFeedback("failed")
    return
  }

  try {
    await navigator.clipboard.writeText(command)
    setCopyFeedback("copied")
  } catch {
    setCopyFeedback("failed")
  }
}
