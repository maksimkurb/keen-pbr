import { useId, useState } from "react"

import { Button } from "@/components/ui/button"
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog"
import { Input } from "@/components/ui/input"
import { Textarea } from "@/components/ui/textarea"
import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable } from "@/components/shared/data-table"
import { FormField } from "@/components/shared/form-field"
import { PageHeader } from "@/components/shared/page-header"
import { SectionCard } from "@/components/shared/section-card"

type ListDraft = {
  name: string
  source: string
  ttlMs: string
  domains: string
  ipCidrs: string
  url: string
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

export function ListsPage() {
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

      <SectionCard title="List inventory">
        <DataTable
          headers={["List", "Type", "Domains / IPv4 / IPv6", "Rules", "Actions"]}
          rows={[
            ["direct", "file", "1 / 0 / 0", "kdirect", <ActionButtons labels={["Edit", "Delete"]} />],
            ["vpn-local", "builtin", "104 / 10 / 5", "kvpn", <ActionButtons labels={["Edit", "Delete"]} />],
            ["signal", "url", "8 / 0 / 0", "kvpn", <ActionButtons labels={["Update", "Edit", "Delete"]} />],
          ]}
        />
      </SectionCard>

      <Dialog onOpenChange={(open) => !open && setEditingList(null)} open={Boolean(editingList)}>
        <DialogContent className="max-h-[90vh] max-w-3xl overflow-y-auto">
          <DialogHeader>
            <DialogTitle>{editingList?.name ? "Edit list" : "Create list"}</DialogTitle>
            <DialogDescription>
              Review the list source, TTL, and matching entries before saving.
            </DialogDescription>
          </DialogHeader>
          {editingList ? (
            <EditListForm
              draft={editingList}
              mode={editingList.name ? "edit" : "create"}
              onCancel={() => setEditingList(null)}
              onChange={setEditingList}
              onSubmit={() => setEditingList(null)}
            />
          ) : null}
        </DialogContent>
      </Dialog>
    </>
  )
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
  const nameId = useId()
  const sourceId = useId()
  const urlId = useId()
  const ttlId = useId()
  const domainsId = useId()
  const ipCidrsId = useId()

  return (
    <form
      className="space-y-3"
      onSubmit={(event) => {
        event.preventDefault()
        onSubmit()
      }}
    >
      <FormField htmlFor={nameId} label="Name">
        <Input
          id={nameId}
          onChange={(event) => onChange({ ...draft, name: event.target.value })}
          readOnly={mode === "edit"}
          value={draft.name}
        />
      </FormField>
      <FormField htmlFor={sourceId} label="Source (url/file/domains/ip_cidrs)">
        <Input
          id={sourceId}
          onChange={(event) => onChange({ ...draft, source: event.target.value })}
          value={draft.source}
        />
      </FormField>
      <FormField htmlFor={urlId} label="Remote URL">
        <Input
          id={urlId}
          onChange={(event) => onChange({ ...draft, url: event.target.value })}
          value={draft.url}
        />
      </FormField>
      <FormField
        description={
          <>
            How long dynamically resolved IPs stay in cache-backed sets;{" "}
            <code>0</code> means no timeout.
          </>
        }
        htmlFor={ttlId}
        label="TTL ms"
      >
        <Input
          id={ttlId}
          onChange={(event) => onChange({ ...draft, ttlMs: event.target.value })}
          value={draft.ttlMs}
        />
      </FormField>
      <FormField
        description={
          <>
            If you write <code>example.com</code>, all subdomains are automatically
            included.
          </>
        }
        htmlFor={domainsId}
        label="Domains"
      >
        <Textarea
          className="min-h-24"
          id={domainsId}
          onChange={(event) => onChange({ ...draft, domains: event.target.value })}
          value={draft.domains}
        />
      </FormField>
      <FormField
        description={
          <>
            Examples: <code>93.184.216.34</code>, <code>10.0.0.0/8</code>,{" "}
            <code>2001:db8::/32</code>.
          </>
        }
        htmlFor={ipCidrsId}
        label="IP CIDRs"
      >
        <Textarea
          className="min-h-24"
          id={ipCidrsId}
          onChange={(event) => onChange({ ...draft, ipCidrs: event.target.value })}
          value={draft.ipCidrs}
        />
      </FormField>
      <DialogFooter className="px-0 pb-0 pt-3">
        <Button onClick={onCancel} type="button" variant="outline">
          Cancel
        </Button>
        <Button type="submit">{mode === "create" ? "Create list" : "Save list"}</Button>
      </DialogFooter>
    </form>
  )
}
