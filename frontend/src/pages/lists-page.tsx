import { ExternalLink, Pencil, RefreshCw, Trash2 } from "lucide-react"
import { useId, useState } from "react"

import { Badge } from "@/components/ui/badge"
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
import {
  Field,
  FieldContent,
  FieldGroup,
  FieldHint,
  FieldLabel,
} from "@/components/shared/field"
import { PageHeader } from "@/components/shared/page-header"
import { StatsDisplay } from "@/components/shared/stats-display"

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

export function ListsPage() {
  const [editingList, setEditingList] = useState<ListDraft | null>(null)

  return (
    <div className="space-y-6">
      <PageHeader
        actions={
          <Button onClick={() => setEditingList(sampleNewList)}>
            New list
          </Button>
        }
        description="Manage domain and IP lists used by routing rules."
        title="Lists"
      />

      <DataTable
        headers={["Name", "Type", "Stats", "Rules", "Actions"]}
        rows={[
          [
            <div className="space-y-1" key="direct-name">
              <div className="flex items-center gap-2 font-medium">
                direct
                <ExternalLink className="h-3 w-3 text-muted-foreground" />
              </div>
              <div className="text-sm text-muted-foreground md:text-xs">/opt/etc/direct.txt</div>
            </div>,
            <Badge key="direct-type" variant="secondary">
              file
            </Badge>,
            <StatsDisplay ipv4Subnets={0} ipv6Subnets={0} key="direct-stats" totalHosts={1} />,
            <Badge key="direct-rule" variant="outline">
              kdirect
            </Badge>,
            <ActionButtons
              actions={[
                { icon: <Pencil className="h-4 w-4" />, label: "Edit" },
                { icon: <Trash2 className="h-4 w-4" />, label: "Delete" },
              ]}
              key="direct-actions"
            />,
          ],
          [
            <div className="space-y-1" key="vpn-local-name">
              <div className="font-medium">vpn-local</div>
              <div className="text-sm text-muted-foreground md:text-xs">builtin</div>
            </div>,
            <Badge key="vpn-local-type" variant="outline">
              builtin
            </Badge>,
            <StatsDisplay ipv4Subnets={10} ipv6Subnets={5} key="vpn-local-stats" totalHosts={104} />,
            <Badge key="vpn-local-rule" variant="outline">
              kvpn
            </Badge>,
            <ActionButtons
              actions={[
                { icon: <Pencil className="h-4 w-4" />, label: "Edit" },
                { icon: <Trash2 className="h-4 w-4" />, label: "Delete" },
              ]}
              key="vpn-local-actions"
            />,
          ],
          [
            <div className="space-y-1" key="signal-name">
              <div className="font-medium">signal</div>
              <div className="text-sm text-muted-foreground md:text-xs">Updated 5 minutes ago</div>
            </div>,
            <Badge key="signal-type">url</Badge>,
            <StatsDisplay ipv4Subnets={0} ipv6Subnets={0} key="signal-stats" totalHosts={8} />,
            <Badge key="signal-rule" variant="outline">
              kvpn
            </Badge>,
            <ActionButtons
              actions={[
                { icon: <RefreshCw className="h-4 w-4" />, label: "Update" },
                { icon: <Pencil className="h-4 w-4" />, label: "Edit" },
                { icon: <Trash2 className="h-4 w-4" />, label: "Delete" },
              ]}
              key="signal-actions"
            />,
          ],
        ]}
      />

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
    </div>
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
      className="space-y-4"
      onSubmit={(event) => {
        event.preventDefault()
        onSubmit()
      }}
    >
      <FieldGroup>
        <Field>
          <FieldLabel htmlFor={nameId}>Name</FieldLabel>
          <FieldContent>
            <Input
              id={nameId}
              onChange={(event) => onChange({ ...draft, name: event.target.value })}
              readOnly={mode === "edit"}
              value={draft.name}
            />
          </FieldContent>
        </Field>
        <Field>
          <FieldLabel htmlFor={sourceId}>Source</FieldLabel>
          <FieldContent>
            <Input
              id={sourceId}
              onChange={(event) => onChange({ ...draft, source: event.target.value })}
              value={draft.source}
            />
          </FieldContent>
        </Field>
        <Field>
          <FieldLabel htmlFor={urlId}>Remote URL</FieldLabel>
          <FieldContent>
            <Input
              id={urlId}
              onChange={(event) => onChange({ ...draft, url: event.target.value })}
              value={draft.url}
            />
          </FieldContent>
        </Field>
        <Field>
          <FieldLabel htmlFor={ttlId}>TTL ms</FieldLabel>
          <FieldContent>
            <Input
              id={ttlId}
              onChange={(event) => onChange({ ...draft, ttlMs: event.target.value })}
              value={draft.ttlMs}
            />
            <FieldHint description="How long dynamically resolved IPs stay in cache-backed sets; 0 means no timeout." />
          </FieldContent>
        </Field>
        <Field>
          <FieldLabel htmlFor={domainsId}>Domains</FieldLabel>
          <FieldContent>
            <Textarea
              className="min-h-24"
              id={domainsId}
              onChange={(event) => onChange({ ...draft, domains: event.target.value })}
              value={draft.domains}
            />
            <FieldHint description="If you write example.com, all subdomains are automatically included." />
          </FieldContent>
        </Field>
        <Field>
          <FieldLabel htmlFor={ipCidrsId}>IP CIDRs</FieldLabel>
          <FieldContent>
            <Textarea
              className="min-h-24"
              id={ipCidrsId}
              onChange={(event) => onChange({ ...draft, ipCidrs: event.target.value })}
              value={draft.ipCidrs}
            />
            <FieldHint description="Examples: 93.184.216.34, 10.0.0.0/8, 2001:db8::/32." />
          </FieldContent>
        </Field>
      </FieldGroup>
      <DialogFooter className="px-0 pb-0 pt-3">
        <Button onClick={onCancel} type="button" variant="outline">
          Cancel
        </Button>
        <Button type="submit">{mode === "create" ? "Create list" : "Save list"}</Button>
      </DialogFooter>
    </form>
  )
}
