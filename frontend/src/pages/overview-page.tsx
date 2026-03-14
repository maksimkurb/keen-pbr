import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { DataTable } from "@/components/shared/data-table"
import { SectionCard } from "@/components/shared/section-card"

export function OverviewPage() {
  return (
    <>
      <div className="grid gap-4 lg:grid-cols-2">
        <SectionCard title="keen-pbr">
          <div className="grid gap-3 md:grid-cols-2">
            <div>
              <p className="text-sm text-slate-500">Version</p>
              <p className="text-3xl font-semibold">3.0.0</p>
            </div>
            <div>
              <p className="text-sm text-slate-500">Service status</p>
              <div className="mt-2 flex items-center gap-2">
                <Badge className="bg-emerald-100 text-emerald-700 hover:bg-emerald-100">
                  running
                </Badge>
                <Badge className="bg-amber-100 text-amber-700 hover:bg-amber-100">
                  stale config
                </Badge>
              </div>
            </div>
          </div>
          <div className="grid grid-cols-3 gap-2">
            <Button variant="outline">Start</Button>
            <Button variant="outline">Stop</Button>
            <Button>Restart</Button>
          </div>
        </SectionCard>

        <SectionCard title="DNS server self-check">
          <div className="rounded-lg border border-rose-300 bg-rose-50 p-4 text-rose-700">
            DNS in your browser appears misconfigured.
          </div>
          <p className="font-mono text-sm text-slate-600">
            upstream: udp://127.0.0.1:2253
          </p>
          <div className="grid grid-cols-2 gap-2">
            <Button variant="outline">Run again</Button>
            <Button variant="outline">Test from PC</Button>
          </div>
        </SectionCard>
      </div>

      <div className="mt-4 grid gap-4">
        <SectionCard title="Outbounds health">
          <DataTable
            headers={["Tag", "Type", "Packets", "Transferred", "Details", "Status"]}
            rows={[
              [
                "vpn",
                "interface",
                "14,220",
                "1.6 GB",
                "Main VPN interface",
                <HealthBadge key="vpn-status" tone="healthy" />,
              ],
              [
                "wan",
                "interface",
                "8,901",
                "840 MB",
                "Main WAN fallback",
                <HealthBadge key="wan-status" tone="healthy" />,
              ],
              [
                "auto-select",
                "urltest",
                "23,121",
                "2.4 GB",
                <UrltestDetails key="urltest-details" />,
                <HealthBadge key="urltest-status" tone="healthy" />,
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
                <HealthBadge key="rule-1-status" tone="healthy">
                  ok
                </HealthBadge>,
              ],
              [
                "#2",
                "list: local-list",
                "auto-select",
                "0x00020000, 8,901 packets",
                "table 151, selected vpn",
                "priority 1001",
                <HealthBadge key="rule-2-status" tone="healthy">
                  ok
                </HealthBadge>,
              ],
            ]}
          />
        </SectionCard>

        <SectionCard title="Domain/IP routing test">
          <div className="flex items-center gap-2 rounded-md border border-slate-200 p-2">
            <Input
              className="flex-1 border-0 shadow-none focus-visible:ring-0"
              defaultValue="example.com"
            />
            <Button>Run routing test</Button>
          </div>
          <p className="text-sm text-slate-600">
            Expected outbound: vpn · Actual outbound: vpn · Matched lists: my-domains
            via dns
          </p>
        </SectionCard>
      </div>
    </>
  )
}

function HealthBadge({
  tone,
  children,
}: {
  tone: "healthy" | "degraded"
  children?: string
}) {
  if (tone === "degraded") {
    return (
      <Badge className="bg-rose-100 text-rose-700 hover:bg-rose-100">
        {children ?? "degraded"}
      </Badge>
    )
  }

  return (
    <Badge className="bg-emerald-100 text-emerald-700 hover:bg-emerald-100">
      {children ?? "healthy"}
    </Badge>
  )
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
    <div className="flex flex-wrap items-center gap-2">
      <span
        className={`size-2 rounded-full ${
          state === "healthy" ? "bg-emerald-500" : "bg-rose-500"
        }`}
      />
      <span className="font-medium">{name}</span>
      <span className="text-slate-500">{latency}</span>
      {active ? <Badge variant="outline">In use</Badge> : null}
    </div>
  )
}
