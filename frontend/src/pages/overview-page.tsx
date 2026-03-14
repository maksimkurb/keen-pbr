import { Play, RotateCw, Search, Square } from "lucide-react"

import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { ButtonGroup } from "@/components/shared/button-group"
import { DataTable } from "@/components/shared/data-table"
import {
  InputGroup,
  InputGroupAddon,
  InputGroupButton,
  InputGroupInput,
} from "@/components/shared/input-group"
import { SectionCard } from "@/components/shared/section-card"

export function OverviewPage() {
  return (
    <div className="space-y-6">
      <div className="grid gap-4 lg:grid-cols-2">
        <SectionCard className="col-span-2 lg:col-span-1" title="keen-pbr">
          <div className="grid grid-cols-2 gap-4">
            <div>
              <div className="mb-1 text-sm text-muted-foreground">Version</div>
              <div className="text-lg font-semibold">
                3.0.0
                <span className="ml-2 text-xs text-muted-foreground">(dev)</span>
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

          <ButtonGroup className="mt-2">
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

          <ButtonGroup className="mt-2">
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
          headers={["Tag", "Type", "Packets", "Transferred", "Details", "Status"]}
          rows={[
            [
              "vpn",
              "interface",
              "14,220",
              "1.6 GB",
              <span className="text-sm text-muted-foreground" key="vpn-details">
                Main VPN interface
              </span>,
              <StatusBadge key="vpn-status" tone="healthy">
                healthy
              </StatusBadge>,
            ],
            [
              "wan",
              "interface",
              "8,901",
              "840 MB",
              <span className="text-sm text-muted-foreground" key="wan-details">
                Main WAN fallback
              </span>,
              <StatusBadge key="wan-status" tone="healthy">
                healthy
              </StatusBadge>,
            ],
            [
              "auto-select",
              "urltest",
              "23,121",
              "2.4 GB",
              <UrltestDetails key="urltest-details" />,
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
        <InputGroup>
          <InputGroupAddon>
            <Search className="h-4 w-4" />
          </InputGroupAddon>
          <InputGroupInput defaultValue="example.com" />
          <InputGroupAddon align="inline-end">
            <InputGroupButton size="sm" variant="outline">
              Run routing test
            </InputGroupButton>
          </InputGroupAddon>
        </InputGroup>
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

function UrltestDetails() {
  return (
    <div className="space-y-2">
      <UrltestOutboundRow active latency="42 ms" name="vpn" state="healthy" />
      <UrltestOutboundRow latency="5 ms" name="wan" state="degraded" />
    </div>
  )
}

function UrltestOutboundRow({
  name,
  latency,
  state,
  active = false,
}: {
  name: string
  latency: string
  state: "healthy" | "degraded"
  active?: boolean
}) {
  return (
    <div className="flex flex-wrap items-center gap-2 text-sm">
      <span
        className={`relative inline-flex size-2 rounded-full ${
          state === "healthy" ? "bg-success" : "bg-destructive"
        }`}
      />
      <span className="font-medium">{name}</span>
      <span className="text-muted-foreground">{latency}</span>
      {active ? <Badge variant="outline">In use</Badge> : null}
    </div>
  )
}
