import { AlertCircle, CheckCircle2, Loader2, RefreshCw, SquareTerminal } from "lucide-react"
import { useEffect, useMemo, useState } from "react"

import type { DnsServer } from "@/api/generated/model"
import { useDnsCheck } from "@/hooks/use-dns-check"
import { ButtonGroup } from "@/components/shared/button-group"
import { SectionCard } from "@/components/shared/section-card"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Separator } from "@/components/ui/separator"

import { DnsCheckModal } from "./dns-check-modal"

export function DnsCheckWidget({ dnsServers }: { dnsServers: DnsServer[] }) {
  const [showPcCheckDialog, setShowPcCheckDialog] = useState(false)
  const { status, startCheck, reset } = useDnsCheck()

  useEffect(() => {
    startCheck(true)
  }, [startCheck])

  const isChecking = status === "checking"

  const cardClassName = useMemo(() => {
    switch (status) {
      case "browser-fail":
      case "sse-fail":
        return "border-destructive/40 bg-destructive/5"
      default:
        return undefined
    }
  }, [status])

  return (
    <>
      <SectionCard
        className={cardClassName}
        description="Confirms that DNS lookups reach the built-in test server from the browser and from another device."
        title="DNS server self-check"
      >
        <div className="flex flex-col gap-4 md:flex-row">
          <div className="flex-1 space-y-3">
            <div className="text-sm font-medium text-muted-foreground">
              Configured DNS servers
            </div>
            {dnsServers.length === 0 ? (
              <div className="text-sm text-muted-foreground">
                No upstream DNS servers are configured in `config.dns.servers`.
              </div>
            ) : (
              <div className="space-y-2">
                {dnsServers.map((server) => (
                  <div
                    className="flex flex-wrap items-center gap-2 rounded-md border px-3 py-2"
                    key={server.tag}
                  >
                    <Badge variant="outline">{server.tag}</Badge>
                    <span className="font-mono text-xs text-muted-foreground md:text-sm">
                      {server.address}
                    </span>
                    {server.detour ? (
                      <span className="text-xs text-muted-foreground">
                        via {server.detour}
                      </span>
                    ) : null}
                  </div>
                ))}
              </div>
            )}
          </div>

          <Separator className="hidden h-auto self-stretch md:block" orientation="vertical" />
          <Separator className="md:hidden" orientation="horizontal" />

          <div className="flex flex-1 flex-col justify-between gap-4">
            <DnsStatusSummary status={status} />

            <ButtonGroup className="mt-auto [&>[data-slot=button]]:flex-1">
              <Button
                disabled={isChecking}
                onClick={() => {
                  reset()
                  startCheck(true)
                }}
                size="sm"
                variant="outline"
              >
                <RefreshCw className="h-4 w-4" />
                {isChecking ? "Checking..." : "Run again"}
              </Button>
              <Button
                onClick={() => setShowPcCheckDialog(true)}
                size="sm"
                variant="outline"
              >
                <SquareTerminal className="h-4 w-4" />
                Test from PC
              </Button>
            </ButtonGroup>
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
  status,
}: {
  status: ReturnType<typeof useDnsCheck>["status"]
}) {
  switch (status) {
    case "success":
      return <DnsStatusMessage icon={<CheckCircle2 className="h-5 w-5 text-emerald-600" />} text="Browser DNS lookup reached the test server." tone="success" />
    case "pc-success":
      return <DnsStatusMessage icon={<CheckCircle2 className="h-5 w-5 text-emerald-600" />} text="Manual device DNS lookup reached the test server." tone="success" />
    case "browser-fail":
      return <DnsStatusMessage icon={<AlertCircle className="h-5 w-5 text-destructive" />} text="Browser request completed, but the DNS probe did not see the lookup." tone="error" />
    case "sse-fail":
      return <DnsStatusMessage icon={<AlertCircle className="h-5 w-5 text-destructive" />} text="The live DNS event stream is unavailable, so the check could not start." tone="error" />
    case "idle":
    case "checking":
      return (
        <div className="flex min-h-24 items-center justify-center">
          <Loader2 className="h-8 w-8 animate-spin text-muted-foreground" />
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
  tone: "success" | "error"
}) {
  return (
    <div
      className={
        tone === "success"
          ? "flex items-center gap-2 text-emerald-700 dark:text-emerald-300"
          : "flex items-center gap-2 text-destructive"
      }
    >
      {icon}
      <span>{text}</span>
    </div>
  )
}
