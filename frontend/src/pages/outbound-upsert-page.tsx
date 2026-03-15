import { Plus } from "lucide-react"
import { useId, useState } from "react"

import { useQueryClient } from "@tanstack/react-query"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { Outbound } from "@/api/generated/model/outbound"
import { usePostConfigMutation } from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
import { useGetConfig } from "@/api/queries"
import {
  findOutboundByTag,
  selectConfig,
  selectOutbounds,
} from "@/api/selectors"
import {
  Field,
  FieldContent,
  FieldGroup,
  FieldHint,
  FieldLabel,
} from "@/components/shared/field"
import { MultiSelectList } from "@/components/shared/multi-select-list"
import { OrderedGroupCard } from "@/components/shared/ordered-group-card"
import { SectionCard } from "@/components/shared/section-card"
import { UpsertPage } from "@/components/shared/upsert-page"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import {
  formatValidationErrors,
  getApiErrorMessage,
  getApiValidationErrors,
} from "@/lib/api-errors"
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
  type: Outbound["type"]
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

type UrltestGroup = {
  id: string
  outbounds: string[]
}

type OutboundFieldName =
  | "tag"
  | "type"
  | "interfaceName"
  | "gateway"
  | "table"
  | "outbounds"
  | "probeUrl"
  | "interval"
  | "tolerance"
  | "retryAttempts"
  | "retryInterval"
  | "circuitBreakerFailures"
  | "circuitBreakerSuccesses"
  | "circuitBreakerTimeout"
  | "circuitBreakerHalfOpen"
  | "strictEnforcement"

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

const strictOptions = [
  "Default (as in global config)",
  "Enabled",
  "Disabled",
] as const
const outboundTypeOptions: Outbound["type"][] = [
  "interface",
  "table",
  "blackhole",
  "ignore",
  "urltest",
]

export function OutboundUpsertPage({
  mode,
  outboundId,
}: {
  mode: "create" | "edit"
  outboundId?: string
}) {
  const queryClient = useQueryClient()
  const [, navigate] = useLocation()
  const configQuery = useGetConfig()
  const loadedConfig = selectConfig(configQuery.data)

  const [mutationErrorMessage, setMutationErrorMessage] = useState<
    string | null
  >(null)
  const [serverFieldErrors, setServerFieldErrors] = useState<
    Partial<Record<OutboundFieldName, string>>
  >({})
  const [submittedTag, setSubmittedTag] = useState<string>(outboundId ?? "")

  const draft =
    getOutboundDraft(loadedConfig, mode === "edit" ? outboundId : undefined) ??
    sampleNewOutbound

  const postConfigMutation = usePostConfigMutation({
    mutation: {
      onSuccess: async () => {
        setMutationErrorMessage(null)
        setServerFieldErrors({})
        await Promise.all([
          queryClient.invalidateQueries({ queryKey: queryKeys.config() }),
          queryClient.invalidateQueries({
            queryKey: queryKeys.healthService(),
          }),
          queryClient.invalidateQueries({
            queryKey: queryKeys.healthRouting(),
          }),
        ])

        navigate("/outbounds")
      },
      onError: (error) => {
        const apiError = error as ApiError
        const { fieldErrors, globalError } = splitOutboundApiErrors(
          apiError,
          submittedTag || draft.tag
        )

        setServerFieldErrors(fieldErrors)
        setMutationErrorMessage(globalError)
      },
    },
  })

  if (
    mode === "edit" &&
    outboundId &&
    !findOutboundByTag(loadedConfig, outboundId)
  ) {
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

  const handleSubmit = (payload: Outbound) => {
    if (!loadedConfig) {
      return
    }

    const currentOutbounds = selectOutbounds(loadedConfig)

    const duplicateTagError = validateTagUniqueness(
      currentOutbounds,
      payload.tag,
      mode === "edit" ? outboundId : undefined
    )

    if (duplicateTagError) {
      setServerFieldErrors({})
      setMutationErrorMessage(duplicateTagError)
      return
    }

    const nextOutbounds =
      mode === "create"
        ? [...currentOutbounds, payload]
        : currentOutbounds.map((outbound) =>
            outbound.tag === outboundId ? payload : outbound
          )

    const urltestReferencesError = validateUrltestGroupReferences(nextOutbounds)

    if (urltestReferencesError) {
      setServerFieldErrors({})
      setMutationErrorMessage(urltestReferencesError)
      return
    }

    const updatedConfig: ConfigObject = {
      ...loadedConfig,
      outbounds: nextOutbounds,
    }

    setSubmittedTag(payload.tag)
    setServerFieldErrors({})
    setMutationErrorMessage(null)
    postConfigMutation.mutate({ data: updatedConfig })
  }

  return (
    <UpsertPage
      cardDescription="Configure interface or urltest outbounds."
      cardTitle={mode === "create" ? "Create outbound" : `Edit ${draft.tag}`}
      description="Outbounds define direct interfaces or grouped urltest behavior."
      title={mode === "create" ? "Create outbound" : "Edit outbound"}
    >
      {mutationErrorMessage ? (
        <Alert className="mb-6 border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription className="whitespace-pre-wrap">
            {mutationErrorMessage}
          </AlertDescription>
        </Alert>
      ) : null}

      <OutboundForm
        draft={draft}
        existingOutbounds={selectOutbounds(loadedConfig)}
        mode={mode}
        onCancel={() => navigate("/outbounds")}
        onSubmit={handleSubmit}
        serverFieldErrors={serverFieldErrors}
      />
    </UpsertPage>
  )
}

function OutboundForm({
  mode,
  draft,
  existingOutbounds,
  onCancel,
  onSubmit,
  serverFieldErrors,
}: {
  mode: "create" | "edit"
  draft: OutboundDraft
  existingOutbounds: Outbound[]
  onCancel: () => void
  onSubmit: (payload: Outbound) => void
  serverFieldErrors: Partial<Record<OutboundFieldName, string>>
}) {
  const [outboundType, setOutboundType] = useState<Outbound["type"]>(draft.type)
  const [strictEnforcement, setStrictEnforcement] = useState(
    draft.strictEnforcement
  )
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
  const interfaceOutboundOptions = existingOutbounds
    .filter((item) => item.type === "interface" && item.tag !== draft.tag)
    .map((item) => item.tag)

  return (
    <form
      className="space-y-6"
      onSubmit={(event) => {
        event.preventDefault()
        const formData = new FormData(event.currentTarget)
        const payload = buildOutboundPayload(
          formData,
          outboundType,
          strictEnforcement,
          urltestGroups
        )
        onSubmit(payload)
      }}
    >
      <FieldGroup>
        <Field invalid={Boolean(serverFieldErrors.tag)}>
          <FieldLabel htmlFor={tagId}>Tag</FieldLabel>
          <FieldContent>
            <Input
              defaultValue={draft.tag}
              disabled={mode === "edit"}
              id={tagId}
              name="tag"
            />
            <FieldHint
              description="Use a unique outbound tag that can be referenced by rules, groups, and detours."
              error={serverFieldErrors.tag ?? null}
            />
          </FieldContent>
        </Field>
        <Field invalid={Boolean(serverFieldErrors.type)}>
          <FieldLabel>Type</FieldLabel>
          <FieldContent>
            <Select
              defaultValue={draft.type}
              onValueChange={(value) =>
                setOutboundType((value as Outbound["type"]) ?? draft.type)
              }
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
            <FieldHint
              description="Choose the outbound type defined by the config schema; the form below updates to show only relevant fields."
              error={serverFieldErrors.type ?? null}
            />
          </FieldContent>
        </Field>
      </FieldGroup>

      {isInterface ? (
        <SectionCard
          description="Configure the egress device and optional gateway for interface-based routing."
          title="Interface settings"
        >
          <div className="grid gap-4 md:grid-cols-2">
            <Field invalid={Boolean(serverFieldErrors.interfaceName)}>
              <FieldLabel htmlFor={interfaceId}>Interface</FieldLabel>
              <FieldContent>
                <Input
                  defaultValue={draft.interfaceName}
                  id={interfaceId}
                  name="interfaceName"
                />
                <FieldHint
                  description="Network interface name used for egress, such as tun0 or eth0."
                  error={serverFieldErrors.interfaceName ?? null}
                />
              </FieldContent>
            </Field>
            <Field invalid={Boolean(serverFieldErrors.gateway)}>
              <FieldLabel htmlFor={gatewayId}>Gateway</FieldLabel>
              <FieldContent>
                <Input
                  defaultValue={draft.gateway}
                  id={gatewayId}
                  name="gateway"
                />
                <FieldHint
                  description="Optional gateway IP address for this interface outbound."
                  error={serverFieldErrors.gateway ?? null}
                />
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
          <Field invalid={Boolean(serverFieldErrors.table)}>
            <FieldLabel htmlFor={tableId}>Table ID</FieldLabel>
            <FieldContent>
              <Input defaultValue={draft.table} id={tableId} name="table" />
              <FieldHint
                description="Kernel routing table ID required for the table outbound type."
                error={serverFieldErrors.table ?? null}
              />
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
            No additional fields are required for this type beyond the outbound
            tag.
          </p>
        </SectionCard>
      ) : null}

      {isIgnore ? (
        <SectionCard
          description="Ignore outbounds pass matching traffic through without policy-based routing changes."
          title="Ignore behavior"
        >
          <p className="text-sm text-muted-foreground md:text-xs">
            No additional fields are required for this type beyond the outbound
            tag.
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
                  setUrltestGroups((current) =>
                    moveGroup(current, index, index + 1)
                  )
                }
                onMoveUp={() =>
                  setUrltestGroups((current) =>
                    moveGroup(current, index, index - 1)
                  )
                }
                onRemove={() =>
                  setUrltestGroups((current) =>
                    current.length === 1
                      ? current
                      : current.filter((item) => item.id !== group.id)
                  )
                }
                title={`Group ${index + 1}`}
              >
                <Field invalid={Boolean(serverFieldErrors.outbounds)}>
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
                        unavailable={getUnavailableOutbounds(
                          urltestGroups,
                          group
                        )}
                        value={group.outbounds}
                      />
                    ) : (
                      <div className="rounded-lg border border-border p-3 text-sm text-muted-foreground md:text-xs">
                        Add interface outbounds first so urltest groups have
                        selectable targets.
                      </div>
                    )}
                    <FieldHint error={serverFieldErrors.outbounds ?? null} />
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
                      getNextAvailableOutbounds(
                        interfaceOutboundOptions,
                        current
                      )
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
            <Field invalid={Boolean(serverFieldErrors.probeUrl)}>
              <FieldLabel htmlFor={probeUrlId}>Probe URL</FieldLabel>
              <FieldContent>
                <Input
                  defaultValue={draft.probeUrl}
                  id={probeUrlId}
                  name="probeUrl"
                />
                <FieldHint
                  description="Health checks fetch this URL to measure availability and latency."
                  error={serverFieldErrors.probeUrl ?? null}
                />
              </FieldContent>
            </Field>
            <Field invalid={Boolean(serverFieldErrors.interval)}>
              <FieldLabel htmlFor={intervalId}>Interval (ms)</FieldLabel>
              <FieldContent>
                <Input
                  defaultValue={draft.interval}
                  id={intervalId}
                  name="interval"
                />
                <FieldHint
                  description="Interval between probes."
                  error={serverFieldErrors.interval ?? null}
                />
              </FieldContent>
            </Field>
            <Field invalid={Boolean(serverFieldErrors.tolerance)}>
              <FieldLabel htmlFor={toleranceId}>Tolerance (ms)</FieldLabel>
              <FieldContent>
                <Input
                  defaultValue={draft.tolerance}
                  id={toleranceId}
                  name="tolerance"
                />
                <FieldHint
                  description="If latency difference is not larger than this value, destination will not change."
                  error={serverFieldErrors.tolerance ?? null}
                />
              </FieldContent>
            </Field>
            <Field invalid={Boolean(serverFieldErrors.retryAttempts)}>
              <FieldLabel htmlFor={retryAttemptsId}>Retry attempts</FieldLabel>
              <FieldContent>
                <Input
                  defaultValue={draft.retryAttempts}
                  id={retryAttemptsId}
                  name="retryAttempts"
                />
                <FieldHint
                  description="Number of extra probe attempts before the check is treated as failed."
                  error={serverFieldErrors.retryAttempts ?? null}
                />
              </FieldContent>
            </Field>
            <Field invalid={Boolean(serverFieldErrors.retryInterval)}>
              <FieldLabel htmlFor={retryIntervalId}>
                Retry interval (ms)
              </FieldLabel>
              <FieldContent>
                <Input
                  defaultValue={draft.retryInterval}
                  id={retryIntervalId}
                  name="retryInterval"
                />
                <FieldHint
                  description="Delay between retry attempts after a failed probe."
                  error={serverFieldErrors.retryInterval ?? null}
                />
              </FieldContent>
            </Field>
          </div>
        </SectionCard>
      ) : null}

      {isUrltest ? (
        <SectionCard
          description="Fallback parameters when probing fails."
          title="Circuit breaker"
        >
          <div className="grid gap-4 md:grid-cols-2">
            <Field invalid={Boolean(serverFieldErrors.circuitBreakerFailures)}>
              <FieldLabel htmlFor={circuitBreakerFailuresId}>
                Failures before open
              </FieldLabel>
              <FieldContent>
                <Input
                  defaultValue="5"
                  id={circuitBreakerFailuresId}
                  name="circuitBreakerFailures"
                />
                <FieldHint
                  description="Open the circuit after this many consecutive failed checks."
                  error={serverFieldErrors.circuitBreakerFailures ?? null}
                />
              </FieldContent>
            </Field>
            <Field invalid={Boolean(serverFieldErrors.circuitBreakerSuccesses)}>
              <FieldLabel htmlFor={circuitBreakerSuccessesId}>
                Successes to close
              </FieldLabel>
              <FieldContent>
                <Input
                  defaultValue="2"
                  id={circuitBreakerSuccessesId}
                  name="circuitBreakerSuccesses"
                />
                <FieldHint
                  description="Close the circuit again after this many successful recovery probes."
                  error={serverFieldErrors.circuitBreakerSuccesses ?? null}
                />
              </FieldContent>
            </Field>
            <Field invalid={Boolean(serverFieldErrors.circuitBreakerTimeout)}>
              <FieldLabel htmlFor={circuitBreakerTimeoutId}>
                Open timeout (ms)
              </FieldLabel>
              <FieldContent>
                <Input
                  defaultValue="30000"
                  id={circuitBreakerTimeoutId}
                  name="circuitBreakerTimeout"
                />
                <FieldHint
                  description="How long the circuit stays open before half-open probing resumes."
                  error={serverFieldErrors.circuitBreakerTimeout ?? null}
                />
              </FieldContent>
            </Field>
            <Field invalid={Boolean(serverFieldErrors.circuitBreakerHalfOpen)}>
              <FieldLabel htmlFor={circuitBreakerHalfOpenId}>
                Half-open probes
              </FieldLabel>
              <FieldContent>
                <Input
                  defaultValue="1"
                  id={circuitBreakerHalfOpenId}
                  name="circuitBreakerHalfOpen"
                />
                <FieldHint
                  description="Maximum concurrent probes allowed while testing recovery."
                  error={serverFieldErrors.circuitBreakerHalfOpen ?? null}
                />
              </FieldContent>
            </Field>
          </div>
        </SectionCard>
      ) : null}

      {isInterface ? (
        <Field invalid={Boolean(serverFieldErrors.strictEnforcement)}>
          <FieldLabel>Strict enforcement</FieldLabel>
          <FieldContent>
            <Select
              defaultValue={draft.strictEnforcement}
              onValueChange={(value) =>
                setStrictEnforcement(value ?? draft.strictEnforcement)
              }
            >
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
            <FieldHint
              description="Override the daemon-level strict routing setting for this interface outbound."
              error={serverFieldErrors.strictEnforcement ?? null}
            />
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

function mapOutboundToDraft(outbound: Outbound): OutboundDraft {
  return {
    tag: outbound.tag,
    type: outbound.type,
    interfaceName: outbound.interface ?? "",
    gateway: outbound.gateway ?? "",
    table: outbound.table?.toString() ?? "",
    outbounds:
      outbound.outbound_groups?.flatMap((group) => group.outbounds).join(",") ??
      "",
    probeUrl: outbound.url ?? sampleNewOutbound.probeUrl,
    interval: outbound.interval_ms?.toString() ?? sampleNewOutbound.interval,
    tolerance: outbound.tolerance_ms?.toString() ?? sampleNewOutbound.tolerance,
    retryAttempts:
      outbound.retry?.attempts?.toString() ?? sampleNewOutbound.retryAttempts,
    retryInterval:
      outbound.retry?.interval_ms?.toString() ??
      sampleNewOutbound.retryInterval,
    strictEnforcement: mapStrictEnforcementToOption(
      outbound.strict_enforcement
    ),
  }
}

function buildOutboundPayload(
  formData: FormData,
  outboundType: Outbound["type"],
  strictEnforcement: string,
  urltestGroups: UrltestGroup[]
): Outbound {
  const tag = getFormValue(formData, "tag")

  if (outboundType === "interface") {
    return {
      type: "interface",
      tag,
      interface: getFormValue(formData, "interfaceName") || undefined,
      gateway: getFormValue(formData, "gateway") || undefined,
      strict_enforcement: mapStrictEnforcementToBoolean(strictEnforcement),
    }
  }

  if (outboundType === "table") {
    return {
      type: "table",
      tag,
      table: parseNumber(getFormValue(formData, "table")),
    }
  }

  if (outboundType === "urltest") {
    return {
      type: "urltest",
      tag,
      url: getFormValue(formData, "probeUrl") || undefined,
      interval_ms: parseNumber(getFormValue(formData, "interval")),
      tolerance_ms: parseNumber(getFormValue(formData, "tolerance")),
      outbound_groups: urltestGroups.map((group) => ({
        outbounds: group.outbounds,
      })),
      retry: {
        attempts: parseNumber(getFormValue(formData, "retryAttempts")),
        interval_ms: parseNumber(getFormValue(formData, "retryInterval")),
      },
      circuit_breaker: {
        failure_threshold: parseNumber(
          getFormValue(formData, "circuitBreakerFailures")
        ),
        success_threshold: parseNumber(
          getFormValue(formData, "circuitBreakerSuccesses")
        ),
        timeout_ms: parseNumber(
          getFormValue(formData, "circuitBreakerTimeout")
        ),
        half_open_max_requests: parseNumber(
          getFormValue(formData, "circuitBreakerHalfOpen")
        ),
      },
    }
  }

  return {
    type: outboundType,
    tag,
  }
}

function getOutboundDraft(
  config: ConfigObject | undefined,
  outboundId?: string
) {
  if (!outboundId || !config) {
    return null
  }

  const outbound = findOutboundByTag(config, outboundId)
  return outbound ? mapOutboundToDraft(outbound) : null
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

function getUnavailableOutbounds(
  groups: UrltestGroup[],
  currentGroup: UrltestGroup
) {
  return groups
    .filter((group) => group.id !== currentGroup.id)
    .flatMap((group) => group.outbounds)
}

function getNextAvailableOutbounds(options: string[], groups: UrltestGroup[]) {
  const used = new Set(groups.flatMap((group) => group.outbounds))
  const next = options.find((option) => !used.has(option))
  return next ? [next] : []
}

function mapStrictEnforcementToOption(value: boolean | undefined): string {
  if (value === undefined) {
    return strictOptions[0]
  }

  return value ? strictOptions[1] : strictOptions[2]
}

function mapStrictEnforcementToBoolean(value: string): boolean | undefined {
  if (value === strictOptions[0]) {
    return undefined
  }

  return value === strictOptions[1]
}

function validateTagUniqueness(
  outbounds: Outbound[],
  tag: string,
  existingTag?: string
): string | null {
  const isDuplicate = outbounds.some(
    (outbound) => outbound.tag === tag && outbound.tag !== existingTag
  )
  return isDuplicate ? `Outbound tag "${tag}" already exists.` : null
}

function validateUrltestGroupReferences(outbounds: Outbound[]): string | null {
  const tags = new Set(outbounds.map((outbound) => outbound.tag))

  for (const outbound of outbounds) {
    if (outbound.type !== "urltest") {
      continue
    }

    for (const group of outbound.outbound_groups ?? []) {
      for (const referencedTag of group.outbounds) {
        if (!tags.has(referencedTag)) {
          return `Outbound "${outbound.tag}" references missing outbound tag "${referencedTag}".`
        }
      }
    }
  }

  return null
}

function parseNumber(value: string): number | undefined {
  const trimmed = value.trim()

  if (!trimmed) {
    return undefined
  }

  const parsed = Number(trimmed)
  return Number.isFinite(parsed) ? parsed : undefined
}

function getFormValue(formData: FormData, key: string): string {
  const value = formData.get(key)
  return typeof value === "string" ? value.trim() : ""
}

function splitOutboundApiErrors(error: ApiError, tag: string) {
  const validationErrors = getApiValidationErrors(error)
  if (validationErrors.length === 0) {
    return {
      fieldErrors: {},
      globalError: getApiErrorMessage(error),
    }
  }

  const fieldErrors: Partial<Record<OutboundFieldName, string>> = {}
  const globalErrors: typeof validationErrors = []

  for (const item of validationErrors) {
    const fieldName = resolveOutboundFieldPath(item.path, tag)
    if (!fieldName) {
      globalErrors.push(item)
      continue
    }

    fieldErrors[fieldName] = fieldErrors[fieldName]
      ? `${fieldErrors[fieldName]} ${item.message}`
      : item.message
  }

  return {
    fieldErrors,
    globalError: globalErrors.length > 0 ? formatValidationErrors(globalErrors) : null,
  }
}

function resolveOutboundFieldPath(path: string, tag: string): OutboundFieldName | undefined {
  const normalizedTag = tag.trim()
  if (!normalizedTag) {
    return undefined
  }

  const prefix = `outbounds.${normalizedTag}`
  if (path === prefix || path === `${prefix}.tag`) {
    return "tag"
  }

  if (path === `${prefix}.type`) {
    return "type"
  }

  if (path === `${prefix}.interface`) {
    return "interfaceName"
  }

  if (path === `${prefix}.gateway`) {
    return "gateway"
  }

  if (path === `${prefix}.table`) {
    return "table"
  }

  if (
    path === `${prefix}.outbound_groups` ||
    new RegExp(
      `^${prefix.replaceAll(".", "\\.")}\\.outbound_groups(?:\\[\\d+\\])?(?:\\.outbounds)?$`
    ).test(path)
  ) {
    return "outbounds"
  }

  if (path === `${prefix}.url`) {
    return "probeUrl"
  }

  if (path === `${prefix}.interval_ms`) {
    return "interval"
  }

  if (path === `${prefix}.tolerance_ms`) {
    return "tolerance"
  }

  if (path === `${prefix}.retry.attempts`) {
    return "retryAttempts"
  }

  if (path === `${prefix}.retry.interval_ms`) {
    return "retryInterval"
  }

  if (path === `${prefix}.circuit_breaker.failure_threshold`) {
    return "circuitBreakerFailures"
  }

  if (path === `${prefix}.circuit_breaker.success_threshold`) {
    return "circuitBreakerSuccesses"
  }

  if (path === `${prefix}.circuit_breaker.timeout_ms`) {
    return "circuitBreakerTimeout"
  }

  if (path === `${prefix}.circuit_breaker.half_open_max_requests`) {
    return "circuitBreakerHalfOpen"
  }

  if (path === `${prefix}.strict_enforcement`) {
    return "strictEnforcement"
  }

  return undefined
}
