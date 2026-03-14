import { Play, RotateCw, Square } from "lucide-react"

import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Input } from "@/components/ui/input"
import { ButtonGroup } from "@/components/shared/button-group"
import { DataTable } from "@/components/shared/data-table"
import { SectionCard } from "@/components/shared/section-card"

export function OverviewPage() {
  return (
    <div className="space-y-6">
      <div className="grid gap-4 lg:grid-cols-2">
        <SectionCard className="col-span-2 lg:col-span-1" title="keen-pbr">
          <div className="grid gap-4 md:grid-cols-2">
            <div>
              <div className="mb-1 text-sm text-muted-foreground">Version</div>
              <div className="text-lg font-semibold">
                3.0.0
                <span className="ml-2 text-sm text-muted-foreground md:text-xs">(dev)</span>
              </div>
            </div>
            <div>
              <div className="mb-1 text-sm text-muted-foreground">Service status</div>
              <div className="flex items-center gap-2">
                <StatusBadge tone="healthy">running</StatusBadge>
                <StatusBadge tone="warning">stale config</StatusBadge>
              </div>
            </div>
          </div>

          <ButtonGroup className="mt-2 [&>[data-slot=button]]:flex-1">
            <Button size="sm" variant="outline">
              <Play className="mr-1 h-3 w-3" />
              Start
            </Button>
            <Button size="sm" variant="outline">
              <Square className="mr-1 h-3 w-3" />
              Stop
            </Button>
            <Button size="sm" variant="outline">
              <RotateCw className="mr-1 h-3 w-3" />
              Restart
            </Button>
          </ButtonGroup>
        </SectionCard>

        <SectionCard className="col-span-2 lg:col-span-1" title="DNS server self-check">
          <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
            <AlertDescription>
              DNS in your browser appears misconfigured.
            </AlertDescription>
          </Alert>

        <div className="text-sm text-muted-foreground">upstream: udp://127.0.0.1:2253</div>

          <ButtonGroup className="mt-2 [&>[data-slot=button]]:flex-1">
            <Button size="sm" variant="outline">
              Run again
            </Button>
            <Button size="sm" variant="outline">
              Test from PC
            </Button>
          </ButtonGroup>
        </SectionCard>
      </div>

      <SectionCard title="Outbounds health">
        <DataTable
          headers={["Tag", "Type", "Packets", "Transferred", "Status"]}
          rows={[
            [
              "vpn",
              "interface",
              "14,220",
              "1.6 GB",
              <StatusBadge key="vpn-status" tone="healthy">
                healthy
              </StatusBadge>,
            ],
            [
              "wan",
              "interface",
              "8,901",
              "840 MB",
              <StatusBadge key="wan-status" tone="healthy">
                healthy
              </StatusBadge>,
            ],
            [
              <UrltestTagTree key="urltest-tag" />,
              "urltest",
              "23,121",
              "2.4 GB",
              <StatusBadge key="urltest-status" tone="healthy">
                healthy
              </StatusBadge>,
            ],
          ]}
        />
      </SectionCard>

      <SectionCard title="Routing rule health">
        <DataTable
          headers={[
            "Rule",
            "Match",
            "Outbound",
            "Fwmark / counters",
            "Route table",
            "Policy rule",
            "Status",
          ]}
          rows={[
            [
              "#1",
              "list: my-domains,my-ips",
              "vpn",
              "0x00010000, 14,220 packets",
              "table 150, tun0",
              "priority 1000",
              <StatusBadge key="rule-1-status" tone="healthy">
                ok
              </StatusBadge>,
            ],
            [
              "#2",
              "list: local-list",
              "auto-select",
              "0x00020000, 8,901 packets",
              "table 151, selected vpn",
              "priority 1001",
              <StatusBadge key="rule-2-status" tone="healthy">
                ok
              </StatusBadge>,
            ],
          ]}
        />
      </SectionCard>

      <SectionCard title="Domain/IP routing test">
        <ButtonGroup>
          <Input
            className="min-w-0 flex-1 rounded-none border-0 shadow-none focus-visible:ring-0"
            defaultValue="example.com"
          />
          <Button className="whitespace-nowrap">
            Run routing test
          </Button>
        </ButtonGroup>
        <div className="text-sm text-muted-foreground">
          Expected outbound: vpn · Actual outbound: vpn · Matched lists: my-domains via
          dns
        </div>
      </SectionCard>
    </div>
  )
}

function StatusBadge({
  tone,
  children,
}: {
  tone: "healthy" | "warning" | "degraded"
  children: string
}) {
  if (tone === "warning") {
    return <Badge className="bg-warning text-warning-foreground">{children}</Badge>
  }

  if (tone === "degraded") {
    return <Badge className="bg-destructive text-destructive-foreground">{children}</Badge>
  }

  return <Badge className="bg-success text-success-foreground">{children}</Badge>
}

function UrltestTagTree() {
  return (
    <div className="space-y-2">
      <div className="font-medium">auto-select</div>
      <div>
        <UrltestOutboundRow active latency="42 ms" name="vpn" state="healthy" />
        <UrltestOutboundRow isLast latency="5 ms" name="wan" state="degraded" />
      </div>
    </div>
  )
}

function UrltestOutboundRow({
  name,
  latency,
  state,
  active = false,
  isLast = false,
}: {
  name: string
  latency: string
  state: "healthy" | "degraded"
  active?: boolean
  isLast?: boolean
}) {
  return (
    <div className="flex flex-wrap items-center text-base md:text-sm ml-1">
      <TreeConnector isLast={isLast} />
      <span
        className={`relative ml-2 inline-flex size-2 rounded-full ${
          state === "healthy" ? "bg-success" : "bg-destructive"
        }`}
      />
      <span className="ml-2 font-medium">{name}</span>
      <span className="ml-2 text-muted-foreground">{latency}</span>
      {active ? <Badge className="ml-2" variant="outline">In use</Badge> : null}
    </div>
  )
}

function TreeConnector({ isLast }: { isLast: boolean }) {
  return (
    <svg
      aria-hidden="true"
      className="mr-0.5 h-full shrink-0 self-stretch"
      preserveAspectRatio="none"
      viewBox="0 0 16 24"
      width="16"
    >
      {isLast ? (
        <path
          d="M2 0V12H14"
          fill="none"
          stroke="currentColor"
          strokeOpacity="0.3"
          strokeWidth="1"
        />
      ) : (
        <path
          d="M2 0V24M2 12H14"
          fill="none"
          stroke="currentColor"
          strokeOpacity="0.3"
          strokeWidth="1"
        />
      )}
    </svg>
  )
}
