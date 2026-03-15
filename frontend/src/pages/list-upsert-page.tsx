import { useId } from "react"
import { useLocation } from "wouter"

import {
  Field,
  FieldContent,
  FieldGroup,
  FieldHint,
  FieldLabel,
} from "@/components/shared/field"
import { UpsertPage } from "@/components/shared/upsert-page"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Textarea } from "@/components/ui/textarea"
import { getListDraft, sampleNewList, type ListDraft } from "@/pages/lists-shared"

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
