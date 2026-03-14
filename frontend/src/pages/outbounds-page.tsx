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
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable } from "@/components/shared/data-table"
import { FormField } from "@/components/shared/form-field"
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

const strictOptions = [
  "Default (as in global config)",
  "Enabled",
  "Disabled",
] as const

export function OutboundsPage() {
  const [editingOutbound, setEditingOutbound] = useState<OutboundDraft | null>(null)

  return (
    <>
      <PageHeader
        actions={
          <div className="flex gap-2">
            <Button onClick={() => setEditingOutbound(sampleNewOutbound)}>
              New outbound
            </Button>
            <Button
              onClick={() => setEditingOutbound(sampleEditOutbound)}
              variant="outline"
            >
              Edit sample outbound
            </Button>
          </div>
        }
        description="Configured outbounds and urltest behavior."
        title="Outbounds"
      />

      <SectionCard title="Outbounds table">
        <DataTable
          headers={["Tag", "Type", "Summary", "Actions"]}
          rows={[
            ["vpn", "interface", "ifname=tun0", <ActionButtons labels={["Edit", "Delete"]} />],
            ["wan", "interface", "ifname=eth0", <ActionButtons labels={["Edit", "Delete"]} />],
            ["auto-select", "urltest", "outbounds=vpn,wan", <ActionButtons labels={["Edit", "Delete"]} />],
          ]}
        />
      </SectionCard>

      <Dialog
        onOpenChange={(open) => !open && setEditingOutbound(null)}
        open={Boolean(editingOutbound)}
      >
        <DialogContent className="max-h-[90vh] max-w-3xl overflow-y-auto">
          <DialogHeader>
            <DialogTitle>
              {editingOutbound?.tag ? "Edit outbound" : "Create outbound"}
            </DialogTitle>
            <DialogDescription>
              Configure interface or urltest outbounds using shadcn form controls.
            </DialogDescription>
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
      className="space-y-3"
      onSubmit={(event) => {
        event.preventDefault()
        onSubmit()
      }}
    >
      <FormField htmlFor={tagId} label="Tag">
        <Input
          id={tagId}
          onChange={(event) => onChange({ ...draft, tag: event.target.value })}
          readOnly={mode === "edit"}
          value={draft.tag}
        />
      </FormField>
      <FormField htmlFor={typeId} label="Type">
        <Input
          id={typeId}
          onChange={(event) => onChange({ ...draft, type: event.target.value })}
          value={draft.type}
        />
      </FormField>
      <FormField
        description="Outbounds inside a group are selected through combobox/select-style controls."
        htmlFor={outboundsId}
        label="Outbounds"
      >
        <Input
          id={outboundsId}
          onChange={(event) => onChange({ ...draft, outbounds: event.target.value })}
          value={draft.outbounds}
        />
      </FormField>
      <FormField htmlFor={probeUrlId} label="Probe URL">
        <Input
          id={probeUrlId}
          onChange={(event) => onChange({ ...draft, probeUrl: event.target.value })}
          value={draft.probeUrl}
        />
      </FormField>
      <FormField
        description="Interval between probes."
        htmlFor={intervalId}
        label="Interval (ms)"
      >
        <Input
          id={intervalId}
          onChange={(event) => onChange({ ...draft, interval: event.target.value })}
          value={draft.interval}
        />
      </FormField>
      <FormField
        description="If latency difference is not larger than this value, destination will not change."
        htmlFor={toleranceId}
        label="Tolerance (ms)"
      >
        <Input
          id={toleranceId}
          onChange={(event) => onChange({ ...draft, tolerance: event.target.value })}
          value={draft.tolerance}
        />
      </FormField>
      <FormField
        description="How many times to retry probe URL before marking outbound as degraded."
        htmlFor={retryAttemptsId}
        label="Retry attempts"
      >
        <Input
          id={retryAttemptsId}
          onChange={(event) => onChange({ ...draft, retryAttempts: event.target.value })}
          value={draft.retryAttempts}
        />
      </FormField>
      <FormField
        description="Interval between retries (only if last request failed)."
        htmlFor={retryIntervalId}
        label="Retry interval (ms)"
      >
        <Input
          id={retryIntervalId}
          onChange={(event) => onChange({ ...draft, retryInterval: event.target.value })}
          value={draft.retryInterval}
        />
      </FormField>
      <SectionCard title="Circuit breaker">
        <div className="grid gap-2 md:grid-cols-2">
          <FormField label="Failure threshold">
            <Input defaultValue="5" />
          </FormField>
          <FormField label="Success threshold">
            <Input defaultValue="2" />
          </FormField>
          <FormField label="Timeout (ms)">
            <Input defaultValue="30000" />
          </FormField>
          <FormField label="Half-open max requests">
            <Input defaultValue="1" />
          </FormField>
        </div>
      </SectionCard>
      <FormField
        description={
          <>
            Use <code>Default (as in global config)</code>, <code>Enabled</code>, or{" "}
            <code>Disabled</code>.
          </>
        }
        label="Strict enforcement"
      >
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
      </FormField>
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
