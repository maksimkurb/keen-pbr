import { Button } from "@/components/ui/button"
import { useEffect, useState, type ReactNode } from "react"

type PageKey = "overview" | "general" | "lists" | "outbounds" | "dns" | "routing-rules"

const navItems: { key: PageKey; label: string; group: "main" | "config" }[] = [
  { key: "overview", label: "Dashboard", group: "main" },
  { key: "general", label: "General", group: "config" },
  { key: "lists", label: "Lists", group: "config" },
  { key: "outbounds", label: "Outbounds", group: "config" },
  { key: "dns", label: "DNS", group: "config" },
  { key: "routing-rules", label: "Routes", group: "config" },
]

function getPageFromHash(): PageKey {
  const hash = window.location.hash.replace("#", "")
  const valid = navItems.some((item) => item.key === hash)
  return valid ? (hash as PageKey) : "overview"
}

function App() {
  const [page, setPage] = useState<PageKey>(() => getPageFromHash())

  useEffect(() => {
    const onHashChange = () => setPage(getPageFromHash())
    window.addEventListener("hashchange", onHashChange)
    return () => window.removeEventListener("hashchange", onHashChange)
  }, [])

  return (
    <div className="min-h-svh bg-[#f3f5f8] text-slate-900">
      <TopNav page={page} />
      <main className="mx-auto max-w-[1260px] px-4 pb-8 pt-6">
        <WarningBanner />
        {page === "overview" && <OverviewPage />}
        {page === "general" && <GeneralConfigPage />}
        {page === "lists" && <ListsPage />}
        {page === "outbounds" && <OutboundsPage />}
        {page === "dns" && <DnsPage />}
        {page === "routing-rules" && <RoutingRulesPage />}
      </main>
    </div>
  )
}

function TopNav({ page }: { page: PageKey }) {
  return (
    <header className="border-b bg-white">
      <div className="mx-auto flex h-16 max-w-[1260px] items-center justify-between px-4">
        <div className="flex items-center gap-6">
          <div className="flex items-center gap-2">
            <div className="rounded bg-slate-800 px-2 py-1 text-sm font-bold text-white">kp</div>
            <span className="text-2xl font-semibold">keen-pbr</span>
          </div>
          <nav className="flex items-center gap-2">
            {navItems.map((item) => (
              <a
                className={`rounded-md px-3 py-2 text-sm font-medium transition-colors ${
                  page === item.key ? "bg-[#285f9f] text-white" : "text-slate-800 hover:bg-slate-100"
                }`}
                href={`#${item.key}`}
                key={item.key}
              >
                {item.label}
              </a>
            ))}
          </nav>
        </div>
        <Button variant="outline">Русский</Button>
      </div>
    </header>
  )
}

function WarningBanner() {
  return (
    <div className="mb-5 rounded-lg border border-amber-300 bg-amber-50 px-4 py-3 text-amber-800">
      Configuration has changed. Restart keen-pbr service to apply changes.
    </div>
  )
}

function PageHeader({ title, description, actions }: { title: string; description: string; actions?: ReactNode }) {
  return (
    <div className="mb-4 flex items-start justify-between gap-3">
      <div>
        <h1 className="text-5xl font-semibold tracking-tight">{title}</h1>
        <p className="mt-1 text-xl text-slate-600">{description}</p>
      </div>
      {actions}
    </div>
  )
}

function Card({ title, children }: { title: string; children: ReactNode }) {
  return (
    <section className="rounded-xl border border-slate-200 bg-white p-5 shadow-sm">
      <h3 className="mb-3 text-2xl font-semibold">{title}</h3>
      <div className="space-y-3">{children}</div>
    </section>
  )
}

function OverviewPage() {
  return (
    <>
      <div className="grid gap-4 lg:grid-cols-2">
        <Card title="keen-pbr">
          <div className="grid gap-3 md:grid-cols-2">
            <div>
              <p className="text-sm text-slate-500">Version</p>
              <p className="text-3xl font-semibold">3.0.0</p>
            </div>
            <div>
              <p className="text-sm text-slate-500">Service status</p>
              <div className="mt-2 flex items-center gap-2">
                <span className="rounded-full bg-emerald-100 px-2 py-1 text-xs font-semibold text-emerald-700">running</span>
                <span className="rounded-full bg-amber-100 px-2 py-1 text-xs font-semibold text-amber-700">stale config</span>
              </div>
            </div>
          </div>
          <div className="grid grid-cols-3 gap-2">
            <Button variant="outline">Start</Button>
            <Button variant="outline">Stop</Button>
            <Button>Restart</Button>
          </div>
        </Card>

        <Card title="DNS server self-check">
          <div className="rounded-lg border border-rose-300 bg-rose-50 p-4 text-rose-700">
            DNS in your browser appears misconfigured.
          </div>
          <p className="font-mono text-sm text-slate-600">upstream: udp://127.0.0.1:2253</p>
          <div className="grid grid-cols-2 gap-2">
            <Button variant="outline">Run again</Button>
            <Button variant="outline">Test from PC</Button>
          </div>
        </Card>
      </div>

      <div className="mt-4 grid gap-4">
        <Card title="Outbounds health">
          <DataTable
            headers={["Tag", "Type", "Status", "Packets", "Transferred", "Selected", "Details"]}
            rows={[
              ["vpn", "interface", "healthy", "14,220", "1.6 GB", "-", "Main VPN interface"],
              ["wan", "interface", "healthy", "8,901", "840 MB", "-", "Main WAN fallback"],
              ["auto-select", "urltest", "healthy", "23,121", "2.4 GB", "vpn", "vpn: 42 ms, closed; wan: 5 ms, closed"],
            ]}
          />
        </Card>

        <Card title="Routing rule health">
          <DataTable
            headers={["Rule", "Match", "Outbound", "Fwmark / counters", "Route table", "Policy rule", "Status"]}
            rows={[
              ["#1", "list: my-domains,my-ips", "vpn", "0x00010000, 14,220 packets", "table 150, tun0", "priority 1000", "ok"],
              ["#2", "list: local-list", "auto-select", "0x00020000, 8,901 packets", "table 151, selected vpn", "priority 1001", "ok"],
            ]}
          />
        </Card>

        <Card title="Domain/IP routing test">
          <div className="flex items-center gap-2 rounded-md border border-slate-200 p-2">
            <input className="flex-1 rounded-md border border-slate-300 bg-white px-3 py-2" defaultValue="example.com" />
            <Button>Run routing test</Button>
          </div>
          <p className="text-sm text-slate-600">Expected outbound: vpn · Actual outbound: vpn · Matched lists: my-domains via dns</p>
        </Card>
      </div>
    </>
  )
}

function GeneralConfigPage() {
  return (
    <>
      <PageHeader description="Daemon defaults, global list refresh, and advanced routing values." title="Settings" />
      <div className="space-y-4">
        <Card title="General">
          <label className="flex items-center gap-2 text-base">
            <input defaultChecked type="checkbox" /> Global strict enforcement
          </label>
          <label className="flex items-center gap-2 text-base">
            <input defaultChecked type="checkbox" /> Enable global lists autoupdate
          </label>
          <LabeledInput label="Cron" value="0 */6 * * *" />
          <p className="text-sm text-slate-500">Global schedule for refreshing remote lists.</p>
        </Card>

        <Card title="Advanced routing settings">
          <div className="grid gap-3 md:grid-cols-3">
            <div>
              <LabeledInput label="fwmark.start" value="0x00010000" />
              <p className="text-sm text-slate-500">First fwmark assigned to outbounds. Enter as hexadecimal value.</p>
            </div>
            <div>
              <LabeledInput label="fwmark.mask" value="0xffff0000" />
              <p className="text-sm text-slate-500">Hex only. Must contain one consecutive run of <code>f</code> digits, e.g. <code>0x00ff0000</code>.</p>
            </div>
            <div>
              <LabeledInput label="iproute.table_start" value="150" />
              <p className="text-sm text-slate-500">Base routing table number used for per-outbound policy tables.</p>
            </div>
          </div>
        </Card>

        <div className="flex justify-end gap-2">
          <Button variant="outline">Cancel</Button>
          <Button>Save</Button>
        </div>
      </div>
    </>
  )
}

type ListDraft = {
  name: string
  source: string
  ttlMs: string
  domains: string
  ipCidrs: string
  url: string
}

type OutboundDraft = {
  tag: string
  type: string
  outbounds: string
  probeUrl: string
  interval: string
  tolerance: string
  retryAttempts: string
  retryInterval: string
  strictEnforcement: string
}

function ListsPage() {
  const [editingList, setEditingList] = useState<ListDraft | null>(null)

  return (
    <>
      <PageHeader
        actions={
          <div className="flex gap-2">
            <Button onClick={() => setEditingList(sampleNewList)} variant="outline">
              New list
            </Button>
            <Button onClick={() => setEditingList(sampleEditList)} variant="outline">
              Edit sample list
            </Button>
          </div>
        }
        description="Manage domain/IP lists used by routing rules."
        title="Lists"
      />

      <Card title="List inventory">
        <DataTable
          headers={["List", "Type", "Domains / IPv4 / IPv6", "Rules", "Actions"]}
          rows={[
            ["direct", "file", "1 / 0 / 0", "kdirect", <ActionButtons labels={["Edit", "Delete"]} />],
            ["vpn-local", "builtin", "104 / 10 / 5", "kvpn", <ActionButtons labels={["Edit", "Delete"]} />],
            ["signal", "url", "8 / 0 / 0", "kvpn", <ActionButtons labels={["Update", "Edit", "Delete"]} />],
          ]}
        />
      </Card>

      <Modal onClose={() => setEditingList(null)} open={Boolean(editingList)} title={editingList?.name ? "Edit list" : "Create list"}>
        {editingList && (
          <EditListForm
            draft={editingList}
            mode={editingList.name ? "edit" : "create"}
            onCancel={() => setEditingList(null)}
            onChange={setEditingList}
            onSubmit={() => setEditingList(null)}
          />
        )}
      </Modal>
    </>
  )
}

const sampleNewList: ListDraft = {
  name: "",
  source: "url",
  ttlMs: "300000",
  domains: "",
  ipCidrs: "",
  url: "",
}

const sampleEditList: ListDraft = {
  name: "vpn-local",
  source: "file",
  ttlMs: "86400000",
  domains: "example.com",
  ipCidrs: "10.0.0.0/8",
  url: "https://example.com/vpn-local.txt",
}

function OutboundsPage() {
  const [editingOutbound, setEditingOutbound] = useState<OutboundDraft | null>(null)

  return (
    <>
      <PageHeader
        actions={
          <div className="flex gap-2">
            <Button onClick={() => setEditingOutbound(sampleNewOutbound)}>New outbound</Button>
            <Button onClick={() => setEditingOutbound(sampleEditOutbound)} variant="outline">
              Edit sample outbound
            </Button>
          </div>
        }
        description="Configured outbounds and urltest behavior."
        title="Outbounds"
      />

      <Card title="Outbounds table">
        <DataTable
          headers={["Tag", "Type", "Summary", "Actions"]}
          rows={[
            ["vpn", "interface", "ifname=tun0", <ActionButtons labels={["Edit", "Delete"]} />],
            ["wan", "interface", "ifname=eth0", <ActionButtons labels={["Edit", "Delete"]} />],
            ["auto-select", "urltest", "outbounds=vpn,wan", <ActionButtons labels={["Edit", "Delete"]} />],
          ]}
        />
      </Card>

      <Modal onClose={() => setEditingOutbound(null)} open={Boolean(editingOutbound)} title={editingOutbound?.tag ? "Edit outbound" : "Create outbound"}>
        {editingOutbound && (
          <EditOutboundForm
            draft={editingOutbound}
            mode={editingOutbound.tag ? "edit" : "create"}
            onCancel={() => setEditingOutbound(null)}
            onChange={setEditingOutbound}
            onSubmit={() => setEditingOutbound(null)}
          />
        )}
      </Modal>
    </>
  )
}

const sampleNewOutbound: OutboundDraft = {
  tag: "",
  type: "interface",
  outbounds: "",
  probeUrl: "https://www.gstatic.com/generate_204",
  interval: "180000",
  tolerance: "100",
  retryAttempts: "3",
  retryInterval: "1000",
  strictEnforcement: "Default (as in global config)",
}

const sampleEditOutbound: OutboundDraft = {
  tag: "auto-select",
  type: "urltest",
  outbounds: "vpn,wan",
  probeUrl: "https://www.gstatic.com/generate_204",
  interval: "180000",
  tolerance: "100",
  retryAttempts: "3",
  retryInterval: "1000",
  strictEnforcement: "Default (as in global config)",
}

function EditListForm({
  mode,
  draft,
  onChange,
  onCancel,
  onSubmit,
}: {
  mode: "create" | "edit"
  draft: ListDraft
  onChange: (next: ListDraft) => void
  onCancel: () => void
  onSubmit: () => void
}) {
  return (
    <form
      className="space-y-3"
      onSubmit={(event) => {
        event.preventDefault()
        onSubmit()
      }}
    >
      <LabeledInput label="Name" onValueChange={(name) => onChange({ ...draft, name })} readOnly={mode === "edit"} value={draft.name} />
      <LabeledInput label="Source (url/file/domains/ip_cidrs)" onValueChange={(source) => onChange({ ...draft, source })} value={draft.source} />
      <LabeledInput label="Remote URL" onValueChange={(url) => onChange({ ...draft, url })} value={draft.url} />
      <LabeledInput label="TTL ms" onValueChange={(ttlMs) => onChange({ ...draft, ttlMs })} value={draft.ttlMs} />
      <p className="text-sm text-slate-500">How long dynamically resolved IPs stay in cache-backed sets; <code>0</code> means no timeout.</p>
      <label className="text-sm font-medium">Domains</label>
      <textarea
        className="min-h-24 w-full rounded-md border border-slate-300 bg-white px-3 py-2 text-sm"
        onChange={(event) => onChange({ ...draft, domains: event.target.value })}
        value={draft.domains}
      />
      <p className="text-sm text-slate-500">If you write <code>example.com</code>, all subdomains are automatically included.</p>
      <label className="text-sm font-medium">IP CIDRs</label>
      <textarea
        className="min-h-24 w-full rounded-md border border-slate-300 bg-white px-3 py-2 text-sm"
        onChange={(event) => onChange({ ...draft, ipCidrs: event.target.value })}
        value={draft.ipCidrs}
      />
      <p className="text-sm text-slate-500">Examples: <code>93.184.216.34</code>, <code>10.0.0.0/8</code>, <code>2001:db8::/32</code>.</p>
      <div className="flex justify-end gap-2">
        <Button onClick={onCancel} type="button" variant="outline">
          Cancel
        </Button>
        <Button type="submit">{mode === "create" ? "Create list" : "Save list"}</Button>
      </div>
    </form>
  )
}

function EditOutboundForm({
  mode,
  draft,
  onChange,
  onCancel,
  onSubmit,
}: {
  mode: "create" | "edit"
  draft: OutboundDraft
  onChange: (next: OutboundDraft) => void
  onCancel: () => void
  onSubmit: () => void
}) {
  return (
    <form
      className="space-y-3"
      onSubmit={(event) => {
        event.preventDefault()
        onSubmit()
      }}
    >
      <LabeledInput label="Tag" onValueChange={(tag) => onChange({ ...draft, tag })} readOnly={mode === "edit"} value={draft.tag} />
      <LabeledInput label="Type" onValueChange={(type) => onChange({ ...draft, type })} value={draft.type} />
      <LabeledInput label="Outbounds" onValueChange={(outbounds) => onChange({ ...draft, outbounds })} value={draft.outbounds} />
      <p className="text-sm text-slate-500">Outbounds inside a group are selected through combobox/select-style controls.</p>
      <LabeledInput label="Probe URL" onValueChange={(probeUrl) => onChange({ ...draft, probeUrl })} value={draft.probeUrl} />
      <LabeledInput label="Interval (ms)" onValueChange={(interval) => onChange({ ...draft, interval })} value={draft.interval} />
      <p className="text-sm text-slate-500">Interval between probes.</p>
      <LabeledInput label="Tolerance (ms)" onValueChange={(tolerance) => onChange({ ...draft, tolerance })} value={draft.tolerance} />
      <p className="text-sm text-slate-500">If latency difference is not larger than this value, destination will not change.</p>
      <LabeledInput label="Retry attempts" onValueChange={(retryAttempts) => onChange({ ...draft, retryAttempts })} value={draft.retryAttempts} />
      <p className="text-sm text-slate-500">How many times to retry probe URL before marking outbound as degraded.</p>
      <LabeledInput label="Retry interval (ms)" onValueChange={(retryInterval) => onChange({ ...draft, retryInterval })} value={draft.retryInterval} />
      <p className="text-sm text-slate-500">Interval between retries (only if last request failed).</p>
      <div className="rounded-md border border-slate-200 p-3">
        <p className="mb-2 font-medium">Circuit breaker</p>
        <div className="grid gap-2 md:grid-cols-2">
          <LabeledInput label="Failure threshold" value="5" />
          <LabeledInput label="Success threshold" value="2" />
          <LabeledInput label="Timeout (ms)" value="30000" />
          <LabeledInput label="Half-open max requests" value="1" />
        </div>
      </div>
      <LabeledInput label="Strict enforcement" onValueChange={(strictEnforcement) => onChange({ ...draft, strictEnforcement })} value={draft.strictEnforcement} />
      <p className="text-sm text-slate-500">Use <code>Default (as in global config)</code>, <code>Enabled</code>, or <code>Disabled</code>.</p>
      <div className="flex justify-end gap-2">
        <Button onClick={onCancel} type="button" variant="outline">
          Cancel
        </Button>
        <Button type="submit">{mode === "create" ? "Create outbound" : "Save outbound"}</Button>
      </div>
    </form>
  )
}

function DnsPage() {
  return (
    <>
      <PageHeader description="DNS servers, list-to-server rules, and fallback selection." title="DNS" />
      <div className="space-y-4">
        <Card title="Servers">
          <DataTable
            headers={["Tag", "Address", "Detour", "Actions"]}
            rows={[
              ["vpn-dns", "10.8.0.1", "vpn", <ActionButtons labels={["Delete"]} />],
              ["google-dns", "8.8.8.8", "None", <ActionButtons labels={["Delete"]} />],
            ]}
          />
          <Button variant="outline">Add DNS server</Button>
        </Card>

        <Card title="Rules">
          <DataTable
            headers={["List", "Server tag", "Actions"]}
            rows={[
              ["my-domains", "vpn-dns", <ActionButtons labels={["Delete"]} />],
              ["remote-list", "vpn-dns", <ActionButtons labels={["Delete"]} />],
            ]}
          />
          <Button variant="outline">Add DNS rule</Button>
        </Card>

        <Card title="Fallback">
          <LabeledInput label="Fallback server tag" value="google-dns" />
        </Card>
      </div>
    </>
  )
}

function RoutingRulesPage() {
  return (
    <>
      <PageHeader
        actions={<Button>New rule</Button>}
        description="Rule list with ordering controls and constrained select fields."
        title="Routing rules"
      />
      <Card title="Rules table">
        <DataTable
          headers={["Order", "List", "Outbound", "Proto", "Dest addr", "Dest port", "Actions"]}
          rows={[
            ["1", "my-domains", "vpn", "tcp/udp", "", "", <ActionButtons labels={["↑", "↓", "Edit", "Delete"]} />],
            ["2", "remote-list", "auto-select", "tcp", "", "443", <ActionButtons labels={["↑", "↓", "Edit", "Delete"]} />],
            ["3", "my-ips", "wan", "any", "198.51.100.0/24", "", <ActionButtons labels={["↑", "↓", "Edit", "Delete"]} />],
          ]}
        />
      </Card>
    </>
  )
}

function Modal({ title, open, onClose, children }: { title: string; open: boolean; onClose: () => void; children: ReactNode }) {
  if (!open) {
    return null
  }

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-slate-900/40 p-4" onClick={onClose}>
      <div className="max-h-[90vh] w-full max-w-3xl overflow-y-auto rounded-xl border border-slate-200 bg-white p-5" onClick={(event) => event.stopPropagation()}>
        <div className="mb-4 flex items-center justify-between">
          <h3 className="text-2xl font-semibold">{title}</h3>
          <Button onClick={onClose} size="sm" variant="outline">
            Close
          </Button>
        </div>
        {children}
      </div>
    </div>
  )
}


function ActionButtons({ labels }: { labels: string[] }) {
  return (
    <div className="flex flex-wrap gap-1">
      {labels.map((label) => (
        <Button key={label} size="sm" variant="outline">
          {label}
        </Button>
      ))}
    </div>
  )
}

function DataTable({ headers, rows }: { headers: string[]; rows: ReactNode[][] }) {
  return (
    <div className="overflow-x-auto rounded-md border border-slate-200">
      <table className="w-full min-w-[760px] border-collapse text-sm">
        <thead className="bg-slate-50">
          <tr>
            {headers.map((header) => (
              <th className="border-b border-slate-200 px-3 py-2 text-left font-semibold" key={header}>
                {header}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {rows.map((row, index) => (
            <tr className="bg-white" key={`${row[0]}-${index}`}>
              {row.map((cell, cellIndex) => (
                <td className="border-b border-slate-100 px-3 py-2 align-top" key={cellIndex}>
                  {cell}
                </td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  )
}

function LabeledInput({
  label,
  value,
  onValueChange,
  readOnly,
}: {
  label: string
  value: string
  onValueChange?: (value: string) => void
  readOnly?: boolean
}) {
  return (
    <div>
      <label className="mb-1 block text-sm font-medium">{label}</label>
      <input
        className="w-full rounded-md border border-slate-300 bg-white px-3 py-2 text-sm"
        onChange={(event) => onValueChange?.(event.target.value)}
        readOnly={readOnly}
        value={value}
      />
    </div>
  )
}

export default App
