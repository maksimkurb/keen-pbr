import { Pencil, Trash2 } from "lucide-react"
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
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
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
import { SectionCard } from "@/components/shared/section-card"

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

const strictOptions = ["Default (as in global config)", "Enabled", "Disabled"] as const

export function OutboundsPage() {
  const [editingOutbound, setEditingOutbound] = useState<OutboundDraft | null>(null)

  return (
    <>
      <PageHeader
        actions={<Button onClick={() => setEditingOutbound(sampleNewOutbound)}>New outbound</Button>}
        description="Configured outbounds and urltest behavior."
        title="Outbounds"
      />

      <DataTable
        headers={["Tag", "Type", "Summary", "Actions"]}
        rows={[
          [
            <div className="font-medium" key="vpn-tag">
              vpn
            </div>,
            <Badge key="vpn-type" variant="outline">
              interface
            </Badge>,
            <span className="text-sm text-muted-foreground" key="vpn-summary">
              ifname=tun0
            </span>,
            <ActionButtons
              actions={[
                { icon: <Pencil className="h-4 w-4" />, label: "Edit" },
                { icon: <Trash2 className="h-4 w-4" />, label: "Delete" },
              ]}
              key="vpn-actions"
            />,
          ],
          [
            <div className="font-medium" key="wan-tag">
              wan
            </div>,
            <Badge key="wan-type" variant="outline">
              interface
            </Badge>,
            <span className="text-sm text-muted-foreground" key="wan-summary">
              ifname=eth0
            </span>,
            <ActionButtons
              actions={[
                { icon: <Pencil className="h-4 w-4" />, label: "Edit" },
                { icon: <Trash2 className="h-4 w-4" />, label: "Delete" },
              ]}
              key="wan-actions"
            />,
          ],
          [
            <div className="font-medium" key="auto-tag">
              auto-select
            </div>,
            <Badge key="auto-type">urltest</Badge>,
            <span className="text-sm text-muted-foreground" key="auto-summary">
              outbounds=vpn,wan
            </span>,
            <ActionButtons
              actions={[
                { icon: <Pencil className="h-4 w-4" />, label: "Edit" },
                { icon: <Trash2 className="h-4 w-4" />, label: "Delete" },
              ]}
              key="auto-actions"
            />,
          ],
        ]}
      />

      <Dialog onOpenChange={(open) => !open && setEditingOutbound(null)} open={Boolean(editingOutbound)}>
        <DialogContent className="max-h-[90vh] max-w-3xl overflow-y-auto">
          <DialogHeader>
            <DialogTitle>
              {editingOutbound?.tag ? "Edit outbound" : "Create outbound"}
            </DialogTitle>
            <DialogDescription>Configure interface or urltest outbounds.</DialogDescription>
          </DialogHeader>
          {editingOutbound ? (
            <EditOutboundForm
              draft={editingOutbound}
              mode={editingOutbound.tag ? "edit" : "create"}
              onCancel={() => setEditingOutbound(null)}
              onChange={setEditingOutbound}
              onSubmit={() => setEditingOutbound(null)}
            />
          ) : null}
        </DialogContent>
      </Dialog>
    </>
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
  const tagId = useId()
  const typeId = useId()
  const outboundsId = useId()
  const probeUrlId = useId()
  const intervalId = useId()
  const toleranceId = useId()
  const retryAttemptsId = useId()
  const retryIntervalId = useId()

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
          <FieldLabel htmlFor={tagId}>Tag</FieldLabel>
          <FieldContent>
            <Input
              id={tagId}
              onChange={(event) => onChange({ ...draft, tag: event.target.value })}
              readOnly={mode === "edit"}
              value={draft.tag}
            />
          </FieldContent>
        </Field>
        <Field>
          <FieldLabel htmlFor={typeId}>Type</FieldLabel>
          <FieldContent>
            <Input
              id={typeId}
              onChange={(event) => onChange({ ...draft, type: event.target.value })}
              value={draft.type}
            />
          </FieldContent>
        </Field>
        <Field>
          <FieldLabel htmlFor={outboundsId}>Outbounds</FieldLabel>
          <FieldContent>
            <Input
              id={outboundsId}
              onChange={(event) => onChange({ ...draft, outbounds: event.target.value })}
              value={draft.outbounds}
            />
            <FieldHint description="Outbounds inside a group are selected through combobox/select-style controls." />
          </FieldContent>
        </Field>
        <Field>
          <FieldLabel htmlFor={probeUrlId}>Probe URL</FieldLabel>
          <FieldContent>
            <Input
              id={probeUrlId}
              onChange={(event) => onChange({ ...draft, probeUrl: event.target.value })}
              value={draft.probeUrl}
            />
          </FieldContent>
        </Field>
        <Field>
          <FieldLabel htmlFor={intervalId}>Interval (ms)</FieldLabel>
          <FieldContent>
            <Input
              id={intervalId}
              onChange={(event) => onChange({ ...draft, interval: event.target.value })}
              value={draft.interval}
            />
            <FieldHint description="Interval between probes." />
          </FieldContent>
        </Field>
        <Field>
          <FieldLabel htmlFor={toleranceId}>Tolerance (ms)</FieldLabel>
          <FieldContent>
            <Input
              id={toleranceId}
              onChange={(event) => onChange({ ...draft, tolerance: event.target.value })}
              value={draft.tolerance}
            />
            <FieldHint description="If latency difference is not larger than this value, destination will not change." />
          </FieldContent>
        </Field>
        <Field>
          <FieldLabel htmlFor={retryAttemptsId}>Retry attempts</FieldLabel>
          <FieldContent>
            <Input
              id={retryAttemptsId}
              onChange={(event) => onChange({ ...draft, retryAttempts: event.target.value })}
              value={draft.retryAttempts}
            />
          </FieldContent>
        </Field>
        <Field>
          <FieldLabel htmlFor={retryIntervalId}>Retry interval (ms)</FieldLabel>
          <FieldContent>
            <Input
              id={retryIntervalId}
              onChange={(event) => onChange({ ...draft, retryInterval: event.target.value })}
              value={draft.retryInterval}
            />
          </FieldContent>
        </Field>
      </FieldGroup>

      <SectionCard description="Fallback parameters when probing fails." title="Circuit breaker">
        <div className="grid gap-3 md:grid-cols-2">
          <Input defaultValue="5" />
          <Input defaultValue="2" />
          <Input defaultValue="30000" />
          <Input defaultValue="1" />
        </div>
      </SectionCard>

      <Field>
        <FieldLabel>Strict enforcement</FieldLabel>
        <FieldContent>
          <Select
            onValueChange={(strictEnforcement) =>
              onChange({
                ...draft,
                strictEnforcement: strictEnforcement ?? strictOptions[0],
              })
            }
            value={draft.strictEnforcement}
          >
            <SelectTrigger>
              <SelectValue />
            </SelectTrigger>
            <SelectContent>
              {strictOptions.map((option) => (
                <SelectItem key={option} value={option}>
                  {option}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
          <FieldHint description="Use Default (as in global config), Enabled, or Disabled." />
        </FieldContent>
      </Field>

      <DialogFooter className="px-0 pb-0 pt-3">
        <Button onClick={onCancel} type="button" variant="outline">
          Cancel
        </Button>
        <Button type="submit">
          {mode === "create" ? "Create outbound" : "Save outbound"}
        </Button>
      </DialogFooter>
    </form>
  )
}
