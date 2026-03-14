import { ExternalLink, Pencil, RefreshCw, Trash2 } from "lucide-react"
import { useId } from "react"
import { useLocation } from "wouter"

import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable } from "@/components/shared/data-table"
import {
  Field,
  FieldContent,
  FieldGroup,
  FieldHint,
  FieldLabel,
} from "@/components/shared/field"
import { PageHeader } from "@/components/shared/page-header"
import { StatsDisplay } from "@/components/shared/stats-display"
import { UpsertPage } from "@/components/shared/upsert-page"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Textarea } from "@/components/ui/textarea"

type ListDraft = {
  name: string
  ttlMs: string
  domains: string
  ipCidrs: string
  url: string
  file: string
}

type ListItem = {
  id: string
  draft: ListDraft
  locationLabel: string
  locationIcon?: "external"
  typeVariant?: "default" | "secondary" | "outline"
  rule: string
  stats: {
    totalHosts: number
    ipv4Subnets: number
    ipv6Subnets: number
  }
  canRefresh?: boolean
}

const sampleNewList: ListDraft = {
  name: "",
  ttlMs: "300000",
  domains: "",
  ipCidrs: "",
  url: "",
  file: "",
}

const listItems: ListItem[] = [
  {
    id: "direct",
    draft: {
      name: "direct",
      ttlMs: "300000",
      domains: "example.com",
      ipCidrs: "93.184.216.34",
      url: "",
      file: "/opt/etc/direct.txt",
    },
    locationLabel: "/opt/etc/direct.txt",
    locationIcon: "external",
    typeVariant: "secondary",
    rule: "kdirect",
    stats: { totalHosts: 1, ipv4Subnets: 0, ipv6Subnets: 0 },
  },
  {
    id: "vpn-local",
    draft: {
      name: "vpn-local",
      ttlMs: "300000",
      domains: "corp.internal",
      ipCidrs: "10.0.0.0/8\nfd00::/8",
      url: "",
      file: "",
    },
    locationLabel: "builtin",
    typeVariant: "outline",
    rule: "kvpn",
    stats: { totalHosts: 104, ipv4Subnets: 10, ipv6Subnets: 5 },
  },
  {
    id: "signal",
    draft: {
      name: "signal",
      ttlMs: "300000",
      domains: "signal.org",
      ipCidrs: "",
      url: "https://example.com/signal.txt",
      file: "",
    },
    locationLabel: "Updated 5 minutes ago",
    rule: "kvpn",
    stats: { totalHosts: 8, ipv4Subnets: 0, ipv6Subnets: 0 },
    canRefresh: true,
  },
]

export function ListsPage() {
  const [, navigate] = useLocation()

  return (
    <div className="space-y-6">
      <PageHeader
        actions={<Button onClick={() => navigate("/lists/create")}>New list</Button>}
        description="Manage domain and IP lists used by routing rules."
        title="Lists"
      />

      <DataTable
        headers={["Name", "Type", "Stats", "Rules", "Actions"]}
        rows={listItems.map((list) => [
          <div className="space-y-1" key={`${list.id}-name`}>
            <div className="flex items-center gap-2 font-medium">
              {list.draft.name}
              {list.locationIcon === "external" ? (
                <ExternalLink className="h-3 w-3 text-muted-foreground" />
              ) : null}
            </div>
            <div className="text-sm text-muted-foreground md:text-xs">{list.locationLabel}</div>
          </div>,
          <Badge key={`${list.id}-type`} variant={list.typeVariant}>
            {getListSourceLabel(list.draft)}
          </Badge>,
          <StatsDisplay
            ipv4Subnets={list.stats.ipv4Subnets}
            ipv6Subnets={list.stats.ipv6Subnets}
            key={`${list.id}-stats`}
            totalHosts={list.stats.totalHosts}
          />,
          <Badge key={`${list.id}-rule`} variant="outline">
            {list.rule}
          </Badge>,
          <ActionButtons
            actions={[
              ...(list.canRefresh
                ? [{ icon: <RefreshCw className="h-4 w-4" />, label: "Update" }]
                : []),
              {
                icon: <Pencil className="h-4 w-4" />,
                label: "Edit",
                onClick: () => navigate(`/lists/${list.id}/edit`),
              },
              { icon: <Trash2 className="h-4 w-4" />, label: "Delete" },
            ]}
            key={`${list.id}-actions`}
          />,
        ])}
      />
    </div>
  )
}

export function ListUpsertPage({
  mode,
  listId,
}: {
  mode: "create" | "edit"
  listId?: string
}) {
  const [, navigate] = useLocation()
  const draft = mode === "edit" ? getListDraft(listId) : sampleNewList

  if (!draft) {
    return (
      <UpsertPage
        cardDescription="The requested list could not be found."
        cardTitle="Missing list"
        description="Return to the lists table and choose a valid entry."
        title="Edit list"
      >
        <div className="flex justify-end">
          <Button onClick={() => navigate("/lists")} variant="outline">
            Back to lists
          </Button>
        </div>
      </UpsertPage>
    )
  }

  return (
    <UpsertPage
      cardDescription="Review the list source, TTL, and matching entries before saving."
      cardTitle={mode === "create" ? "Create list" : `Edit ${draft.name}`}
      description="Lists can be backed by files, builtin sources, or remote URLs."
      title={mode === "create" ? "Create list" : "Edit list"}
    >
      <ListForm
        draft={draft}
        mode={mode}
        onCancel={() => navigate("/lists")}
        onSubmit={() => navigate("/lists")}
      />
    </UpsertPage>
  )
}

function ListForm({
  mode,
  draft,
  onCancel,
  onSubmit,
}: {
  mode: "create" | "edit"
  draft: ListDraft
  onCancel: () => void
  onSubmit: () => void
}) {
  const nameId = useId()
  const urlId = useId()
  const fileId = useId()
  const ttlId = useId()
  const domainsId = useId()
  const ipCidrsId = useId()

  return (
    <form
      className="space-y-6"
      onSubmit={(event) => {
        event.preventDefault()
        onSubmit()
      }}
    >
      <FieldGroup>
        <Field>
          <FieldLabel htmlFor={nameId}>Name</FieldLabel>
          <FieldContent>
            <Input defaultValue={draft.name} disabled={mode === "edit"} id={nameId} />
            <FieldHint description="Use a stable identifier so rules and references remain easy to follow." />
          </FieldContent>
        </Field>
        <Field>
          <FieldLabel htmlFor={ttlId}>TTL ms</FieldLabel>
          <FieldContent>
            <Input defaultValue={draft.ttlMs} id={ttlId} />
            <FieldHint description="How long resolved IPs from domains in this list stay in the IP set; 0 means no timeout." />
          </FieldContent>
        </Field>
        <Field>
          <FieldLabel htmlFor={urlId}>Remote URL</FieldLabel>
          <FieldContent>
            <Input defaultValue={draft.url} id={urlId} />
            <FieldHint description="Optional remote source loaded over HTTP or HTTPS and merged into the list." />
          </FieldContent>
        </Field>
        <Field>
          <FieldLabel htmlFor={fileId}>Local file</FieldLabel>
          <FieldContent>
            <Input defaultValue={draft.file} id={fileId} />
            <FieldHint description="Optional local file path. File entries are merged with any inline domains, IPs, and remote URL data." />
          </FieldContent>
        </Field>
        <Field>
          <FieldLabel htmlFor={domainsId}>Domains</FieldLabel>
          <FieldContent>
            <Textarea className="min-h-24" defaultValue={draft.domains} id={domainsId} />
            <FieldHint description="Inline domain patterns. Writing example.com automatically includes all subdomains." />
          </FieldContent>
        </Field>
        <Field>
          <FieldLabel htmlFor={ipCidrsId}>IP CIDRs</FieldLabel>
          <FieldContent>
            <Textarea className="min-h-24" defaultValue={draft.ipCidrs} id={ipCidrsId} />
            <FieldHint description="Inline IP addresses or CIDR ranges, for example 93.184.216.34, 10.0.0.0/8, or 2001:db8::/32." />
          </FieldContent>
        </Field>
      </FieldGroup>
      <div className="flex justify-end gap-3">
        <Button onClick={onCancel} size="xl" type="button" variant="outline">
          Cancel
        </Button>
        <Button size="xl" type="submit">
          {mode === "create" ? "Create list" : "Save list"}
        </Button>
      </div>
    </form>
  )
}

function getListDraft(listId?: string) {
  if (!listId) {
    return null
  }

  const list = listItems.find((item) => item.id === listId)
  return list ? list.draft : null
}

function getListSourceLabel(draft: ListDraft) {
  const sources = [
    draft.url ? "url" : null,
    draft.file ? "file" : null,
    draft.domains ? "domains" : null,
    draft.ipCidrs ? "ip_cidrs" : null,
  ].filter(Boolean)

  if (sources.length === 0) {
    return "empty"
  }

  if (sources.length === 1) {
    return sources[0]
  }

  return "combined"
}
