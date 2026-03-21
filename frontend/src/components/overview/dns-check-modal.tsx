import { AlertCircle, CheckCircle2, Copy, Loader2, Terminal } from "lucide-react"
import { useEffect, useState } from "react"

import type { DnsCheckStatus } from "@/hooks/use-dns-check"
import { useDnsCheck } from "@/hooks/use-dns-check"
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
    ? `nslookup ${pcCheckState.randomString}.dns-check.keen-pbr.internal`
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
          <DialogTitle>Test DNS from another device</DialogTitle>
          <DialogDescription>
            Run the generated `nslookup` command on your PC or phone while this dialog
            stays open.
          </DialogDescription>
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
              text={getBrowserStatusText(browserStatus)}
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
              text={getPcStatusText(isPcSuccess, pcCheckState.waiting)}
            />
          </div>

          {pcCheckState.waiting && command ? (
            <div className="space-y-2">
              <div className="text-sm text-muted-foreground">
                Copy and run this command:
              </div>
              <CommandCopyField key={command} command={command} />
            </div>
          ) : null}

          {pcCheckState.showWarning ? (
            <Alert className="border-amber-500/30 bg-amber-500/5 text-amber-700 dark:text-amber-300">
              <AlertCircle className="text-amber-600 dark:text-amber-300" />
              <AlertDescription className="text-amber-700 dark:text-amber-300">
                The DNS test query has not arrived yet. Make sure the device is using
                your router DNS and try the command again.
              </AlertDescription>
            </Alert>
          ) : null}

          {isPcSuccess ? (
            <Button className="w-full" onClick={() => onOpenChange(false)} variant="outline">
              Close
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
                aria-label="Copy command"
                onClick={() => void copyCommand(command, setCopyFeedback)}
                size="icon-xs"
              />
            }
          >
            <Copy />
          </TooltipTrigger>
          <TooltipContent>
            {copyFeedback === "copied"
              ? "Copied"
              : copyFeedback === "failed"
                ? "Clipboard unavailable"
                : "Copy"}
          </TooltipContent>
        </Tooltip>
      </InputGroupAddon>
    </InputGroup>
  )
}

function getBrowserStatusText(status: DnsCheckStatus) {
  switch (status) {
    case "success":
      return "Browser DNS lookup reached the test server."
    case "browser-fail":
      return "Browser request ran, but the DNS lookup was not observed."
    case "sse-fail":
      return "Live DNS event stream is not connected."
    case "checking":
      return "Checking browser DNS path..."
    default:
      return "Browser DNS status is not known yet."
  }
}

function getPcStatusText(isPcSuccess: boolean, isWaiting: boolean) {
  if (isPcSuccess) {
    return "Manual device test reached the DNS probe."
  }

  if (isWaiting) {
    return "Waiting for your manual nslookup command..."
  }

  return "Manual device test has not completed yet."
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
