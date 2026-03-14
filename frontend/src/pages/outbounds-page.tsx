import { Pencil, Plus, Trash2 } from "lucide-react"
import { useId, useState } from "react"
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
import { MultiSelectList } from "@/components/shared/multi-select-list"
import { OrderedGroupCard } from "@/components/shared/ordered-group-card"
import { SectionCard } from "@/components/shared/section-card"
import { UpsertPage } from "@/components/shared/upsert-page"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectLabel,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"

type OutboundDraft = {
  tag: string
  type: string
  interfaceName: string
  gateway: string
  table: string
  outbounds: string
  probeUrl: string
  interval: string
  tolerance: string
  retryAttempts: string
  retryInterval: string
  strictEnforcement: string
}

type OutboundItem = {
  id: string
  draft: OutboundDraft
  typeVariant?: "default" | "outline" | "secondary"
  summary: string
}

type UrltestGroup = {
  id: string
  outbounds: string[]
}

const sampleNewOutbound: OutboundDraft = {
  tag: "",
  type: "interface",
  interfaceName: "",
  gateway: "",
  table: "",
  outbounds: "",
  probeUrl: "https://www.gstatic.com/generate_204",
  interval: "180000",
  tolerance: "100",
  retryAttempts: "3",
  retryInterval: "1000",
  strictEnforcement: "Default (as in global config)",
}

const strictOptions = ["Default (as in global config)", "Enabled", "Disabled"] as const
const outboundTypeOptions = ["interface", "table", "blackhole", "ignore", "urltest"] as const

const outboundItems: OutboundItem[] = [
  {
    id: "vpn",
    draft: {
      tag: "vpn",
      type: "interface",
      interfaceName: "tun0",
      gateway: "10.8.0.1",
      table: "",
      outbounds: "",
      probeUrl: "",
      interval: "180000",
      tolerance: "100",
      retryAttempts: "3",
      retryInterval: "1000",
      strictEnforcement: "Default (as in global config)",
    },
    typeVariant: "outline",
    summary: "ifname=tun0",
  },
  {
    id: "wan",
    draft: {
      tag: "wan",
      type: "interface",
      interfaceName: "eth0",
      gateway: "192.168.1.1",
      table: "",
      outbounds: "",
      probeUrl: "",
      interval: "180000",
      tolerance: "100",
      retryAttempts: "3",
      retryInterval: "1000",
      strictEnforcement: "Default (as in global config)",
    },
    typeVariant: "outline",
    summary: "ifname=eth0",
  },
  {
    id: "auto-select",
    draft: {
      tag: "auto-select",
      type: "urltest",
      interfaceName: "",
      gateway: "",
      table: "",
      outbounds: "vpn,wan",
      probeUrl: "https://www.gstatic.com/generate_204",
      interval: "180000",
      tolerance: "100",
      retryAttempts: "3",
      retryInterval: "1000",
      strictEnforcement: "Default (as in global config)",
    },
    summary: "outbounds=vpn,wan",
  },
]

export function OutboundsPage() {
  const [, navigate] = useLocation()

  return (
    <div className="space-y-6">
      <PageHeader
        actions={<Button onClick={() => navigate("/outbounds/create")}>New outbound</Button>}
        description="Configured outbounds and urltest behavior."
        title="Outbounds"
      />

      <DataTable
        headers={["Tag", "Type", "Summary", "Actions"]}
        rows={outboundItems.map((outbound) => [
          <div className="font-medium" key={`${outbound.id}-tag`}>
            {outbound.draft.tag}
          </div>,
          <Badge key={`${outbound.id}-type`} variant={outbound.typeVariant}>
            {outbound.draft.type}
          </Badge>,
          <span className="text-sm text-muted-foreground" key={`${outbound.id}-summary`}>
            {outbound.summary}
          </span>,
          <ActionButtons
            actions={[
              {
                icon: <Pencil className="h-4 w-4" />,
                label: "Edit",
                onClick: () => navigate(`/outbounds/${outbound.id}/edit`),
              },
              { icon: <Trash2 className="h-4 w-4" />, label: "Delete" },
            ]}
            key={`${outbound.id}-actions`}
          />,
        ])}
      />
    </div>
  )
}

export function OutboundUpsertPage({
  mode,
  outboundId,
}: {
  mode: "create" | "edit"
  outboundId?: string
}) {
  const [, navigate] = useLocation()
  const draft = mode === "edit" ? getOutboundDraft(outboundId) : sampleNewOutbound

  if (!draft) {
    return (
      <UpsertPage
        cardDescription="The requested outbound could not be found."
        cardTitle="Missing outbound"
        description="Return to the outbounds table and choose a valid entry."
        title="Edit outbound"
      >
        <div className="flex justify-end">
          <Button onClick={() => navigate("/outbounds")} variant="outline">
            Back to outbounds
          </Button>
        </div>
      </UpsertPage>
    )
  }

  return (
    <UpsertPage
      cardDescription="Configure interface or urltest outbounds."
      cardTitle={mode === "create" ? "Create outbound" : `Edit ${draft.tag}`}
      description="Outbounds define direct interfaces or grouped urltest behavior."
      title={mode === "create" ? "Create outbound" : "Edit outbound"}
    >
      <OutboundForm
        draft={draft}
        mode={mode}
        onCancel={() => navigate("/outbounds")}
        onSubmit={() => navigate("/outbounds")}
      />
    </UpsertPage>
  )
}

function OutboundForm({
  mode,
  draft,
  onCancel,
  onSubmit,
}: {
  mode: "create" | "edit"
  draft: OutboundDraft
  onCancel: () => void
  onSubmit: () => void
}) {
  const [outboundType, setOutboundType] = useState(draft.type)
  const [urltestGroups, setUrltestGroups] = useState<UrltestGroup[]>(
    getInitialUrltestGroups(draft.outbounds)
  )
  const isUrltest = outboundType === "urltest"
  const isInterface = outboundType === "interface"
  const isTable = outboundType === "table"
  const isBlackhole = outboundType === "blackhole"
  const isIgnore = outboundType === "ignore"
  const tagId = useId()
  const interfaceId = useId()
  const gatewayId = useId()
  const tableId = useId()
  const probeUrlId = useId()
  const intervalId = useId()
  const toleranceId = useId()
  const retryAttemptsId = useId()
  const retryIntervalId = useId()
  const circuitBreakerFailuresId = useId()
  const circuitBreakerSuccessesId = useId()
  const circuitBreakerTimeoutId = useId()
  const circuitBreakerHalfOpenId = useId()
  const interfaceOutboundOptions = outboundItems
    .filter((item) => item.draft.type === "interface" && item.draft.tag !== draft.tag)
    .map((item) => item.draft.tag)

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
          <FieldLabel htmlFor={tagId}>Tag</FieldLabel>
          <FieldContent>
            <Input defaultValue={draft.tag} disabled={mode === "edit"} id={tagId} />
            <FieldHint description="Use a unique outbound tag that can be referenced by rules, groups, and detours." />
          </FieldContent>
        </Field>
        <Field>
          <FieldLabel>Type</FieldLabel>
          <FieldContent>
            <Select
              defaultValue={draft.type}
              onValueChange={(value) => setOutboundType(value ?? draft.type)}
            >
              <SelectTrigger>
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectGroup>
                  <SelectLabel>Outbound types</SelectLabel>
                  {outboundTypeOptions.map((option) => (
                    <SelectItem key={option} value={option}>
                      {option}
                    </SelectItem>
                  ))}
                </SelectGroup>
              </SelectContent>
            </Select>
            <FieldHint description="Choose the outbound type defined by the config schema; the form below updates to show only relevant fields." />
          </FieldContent>
        </Field>
      </FieldGroup>

      {isInterface ? (
        <SectionCard
          description="Configure the egress device and optional gateway for interface-based routing."
          title="Interface settings"
        >
          <div className="grid gap-4 md:grid-cols-2">
            <Field>
              <FieldLabel htmlFor={interfaceId}>Interface</FieldLabel>
              <FieldContent>
                <Input defaultValue={draft.interfaceName} id={interfaceId} />
                <FieldHint description="Network interface name used for egress, such as tun0 or eth0." />
              </FieldContent>
            </Field>
            <Field>
              <FieldLabel htmlFor={gatewayId}>Gateway</FieldLabel>
              <FieldContent>
                <Input defaultValue={draft.gateway} id={gatewayId} />
                <FieldHint description="Optional gateway IP address for this interface outbound." />
              </FieldContent>
            </Field>
          </div>
        </SectionCard>
      ) : null}

      {isTable ? (
        <SectionCard
          description="Map this outbound to an existing kernel routing table."
          title="Table settings"
        >
          <Field>
            <FieldLabel htmlFor={tableId}>Table ID</FieldLabel>
            <FieldContent>
              <Input defaultValue={draft.table} id={tableId} />
              <FieldHint description="Kernel routing table ID required for the table outbound type." />
            </FieldContent>
          </Field>
        </SectionCard>
      ) : null}

      {isBlackhole ? (
        <SectionCard
          description="Blackhole outbounds intentionally drop all matching traffic."
          title="Blackhole behavior"
        >
          <p className="text-sm text-muted-foreground md:text-xs">
            No additional fields are required for this type beyond the outbound tag.
          </p>
        </SectionCard>
      ) : null}

      {isIgnore ? (
        <SectionCard
          description="Ignore outbounds pass matching traffic through without policy-based routing changes."
          title="Ignore behavior"
        >
          <p className="text-sm text-muted-foreground md:text-xs">
            No additional fields are required for this type beyond the outbound tag.
          </p>
        </SectionCard>
      ) : null}

      {isUrltest ? (
        <SectionCard
          description="Groups are tried in order. Each group selects from interface outbounds, and order acts as priority."
          title="Outbound groups"
        >
          <div className="space-y-4">
            {urltestGroups.map((group, index) => (
              <OrderedGroupCard
                canMoveDown={index !== urltestGroups.length - 1}
                canMoveUp={index !== 0}
                canRemove={urltestGroups.length !== 1}
                description={`Priority ${index + 1}. Earlier groups are preferred before later ones.`}
                key={group.id}
                onMoveDown={() =>
                  setUrltestGroups((current) => moveGroup(current, index, index + 1))
                }
                onMoveUp={() =>
                  setUrltestGroups((current) => moveGroup(current, index, index - 1))
                }
                onRemove={() =>
                  setUrltestGroups((current) =>
                    current.length === 1 ? current : current.filter((item) => item.id !== group.id)
                  )
                }
                title={`Group ${index + 1}`}
              >
                <Field>
                  <FieldLabel>Interface outbounds</FieldLabel>
                  <FieldContent>
                    {interfaceOutboundOptions.length ? (
                      <MultiSelectList
                        addLabel="Add outbound"
                        emptyMessage="No interface outbounds found."
                        groupLabel="Interface outbounds"
                        onChange={(nextOutbounds) =>
                          setUrltestGroups((current) =>
                            current.map((item) =>
                              item.id === group.id
                                ? { ...item, outbounds: nextOutbounds }
                                : item
                            )
                          )
                        }
                        options={interfaceOutboundOptions}
                        unavailable={getUnavailableOutbounds(urltestGroups, group)}
                        value={group.outbounds}
                      />
                    ) : (
                      <div className="rounded-lg border border-border p-3 text-sm text-muted-foreground md:text-xs">
                        {interfaceOutboundOptions.length ? (
                          "All interface outbounds are already assigned to other groups."
                        ) : (
                          "Add interface outbounds first so urltest groups have selectable targets."
                        )}
                      </div>
                    )}
                  </FieldContent>
                </Field>
              </OrderedGroupCard>
            ))}
            <div className="flex justify-start">
              <Button
                onClick={() =>
                  setUrltestGroups((current) => [
                    ...current,
                    createUrltestGroup(
                      getNextAvailableOutbounds(interfaceOutboundOptions, current)
                    ),
                  ])
                }
                type="button"
                variant="outline"
              >
                <Plus className="h-4 w-4" />
                Add group
              </Button>
            </div>
          </div>
        </SectionCard>
      ) : null}

      {isUrltest ? (
        <SectionCard
          description="Configure how the urltest group probes candidates and retries failed checks."
          title="Probing and retries"
        >
          <div className="grid gap-4 md:grid-cols-2">
            <Field>
              <FieldLabel htmlFor={probeUrlId}>Probe URL</FieldLabel>
              <FieldContent>
                <Input defaultValue={draft.probeUrl} id={probeUrlId} />
                <FieldHint description="Health checks fetch this URL to measure availability and latency." />
              </FieldContent>
            </Field>
            <Field>
              <FieldLabel htmlFor={intervalId}>Interval (ms)</FieldLabel>
              <FieldContent>
                <Input defaultValue={draft.interval} id={intervalId} />
                <FieldHint description="Interval between probes." />
              </FieldContent>
            </Field>
            <Field>
              <FieldLabel htmlFor={toleranceId}>Tolerance (ms)</FieldLabel>
              <FieldContent>
                <Input defaultValue={draft.tolerance} id={toleranceId} />
                <FieldHint description="If latency difference is not larger than this value, destination will not change." />
              </FieldContent>
            </Field>
            <Field>
              <FieldLabel htmlFor={retryAttemptsId}>Retry attempts</FieldLabel>
              <FieldContent>
                <Input defaultValue={draft.retryAttempts} id={retryAttemptsId} />
                <FieldHint description="Number of extra probe attempts before the check is treated as failed." />
              </FieldContent>
            </Field>
            <Field>
              <FieldLabel htmlFor={retryIntervalId}>Retry interval (ms)</FieldLabel>
              <FieldContent>
                <Input defaultValue={draft.retryInterval} id={retryIntervalId} />
                <FieldHint description="Delay between retry attempts after a failed probe." />
              </FieldContent>
            </Field>
          </div>
        </SectionCard>
      ) : null}

      {isUrltest ? (
        <SectionCard description="Fallback parameters when probing fails." title="Circuit breaker">
          <div className="grid gap-4 md:grid-cols-2">
            <Field>
              <FieldLabel htmlFor={circuitBreakerFailuresId}>Failures before open</FieldLabel>
              <FieldContent>
                <Input defaultValue="5" id={circuitBreakerFailuresId} />
                <FieldHint description="Open the circuit after this many consecutive failed checks." />
              </FieldContent>
            </Field>
            <Field>
              <FieldLabel htmlFor={circuitBreakerSuccessesId}>Successes to close</FieldLabel>
              <FieldContent>
                <Input defaultValue="2" id={circuitBreakerSuccessesId} />
                <FieldHint description="Close the circuit again after this many successful recovery probes." />
              </FieldContent>
            </Field>
            <Field>
              <FieldLabel htmlFor={circuitBreakerTimeoutId}>Open timeout (ms)</FieldLabel>
              <FieldContent>
                <Input defaultValue="30000" id={circuitBreakerTimeoutId} />
                <FieldHint description="How long the circuit stays open before half-open probing resumes." />
              </FieldContent>
            </Field>
            <Field>
              <FieldLabel htmlFor={circuitBreakerHalfOpenId}>Half-open probes</FieldLabel>
              <FieldContent>
                <Input defaultValue="1" id={circuitBreakerHalfOpenId} />
                <FieldHint description="Maximum concurrent probes allowed while testing recovery." />
              </FieldContent>
            </Field>
          </div>
        </SectionCard>
      ) : null}

      {isInterface ? (
        <Field>
          <FieldLabel>Strict enforcement</FieldLabel>
          <FieldContent>
            <Select defaultValue={draft.strictEnforcement}>
              <SelectTrigger>
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectGroup>
                  <SelectLabel>Strict enforcement</SelectLabel>
                  {strictOptions.map((option) => (
                    <SelectItem key={option} value={option}>
                      {option}
                    </SelectItem>
                  ))}
                </SelectGroup>
              </SelectContent>
            </Select>
            <FieldHint description="Override the daemon-level strict routing setting for this interface outbound." />
          </FieldContent>
        </Field>
      ) : null}

      <div className="flex justify-end gap-3">
        <Button onClick={onCancel} size="xl" type="button" variant="outline">
          Cancel
        </Button>
        <Button size="xl" type="submit">
          {mode === "create" ? "Create outbound" : "Save outbound"}
        </Button>
      </div>
    </form>
  )
}

function getOutboundDraft(outboundId?: string) {
  if (!outboundId) {
    return null
  }

  const outbound = outboundItems.find((item) => item.id === outboundId)
  return outbound ? outbound.draft : null
}

function getInitialUrltestGroups(outbounds: string) {
  const parsedOutbounds = outbounds
    .split(",")
    .map((value) => value.trim())
    .filter(Boolean)

  return parsedOutbounds.length
    ? [createUrltestGroup(parsedOutbounds)]
    : [createUrltestGroup([])]
}

function createUrltestGroup(outbounds: string[]): UrltestGroup {
  return {
    id: crypto.randomUUID(),
    outbounds,
  }
}

function moveGroup(groups: UrltestGroup[], fromIndex: number, toIndex: number) {
  const next = [...groups]
  const [moved] = next.splice(fromIndex, 1)
  next.splice(toIndex, 0, moved)
  return next
}

function getUnavailableOutbounds(groups: UrltestGroup[], currentGroup: UrltestGroup) {
  return groups
    .filter((group) => group.id !== currentGroup.id)
    .flatMap((group) => group.outbounds)
}

function getNextAvailableOutbounds(options: string[], groups: UrltestGroup[]) {
  const used = new Set(groups.flatMap((group) => group.outbounds))
  const next = options.find((option) => !used.has(option))
  return next ? [next] : []
}
