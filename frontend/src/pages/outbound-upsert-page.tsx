import { Plus } from "lucide-react"
import { useEffect, useId, useState } from "react"
import { useTranslation } from "react-i18next"

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
  urltestGroups: string[][]
  probeUrl: string
  interval: string
  tolerance: string
  retryAttempts: string
  retryInterval: string
  circuitBreakerFailures: string
  circuitBreakerSuccesses: string
  circuitBreakerTimeout: string
  circuitBreakerHalfOpen: string
  strictEnforcement: string
}

type UrltestGroup = {
  id: string
  outbounds: string[]
}

let nextUrltestGroupId = 1

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
  urltestGroups: [[]],
  probeUrl: "https://www.gstatic.com/generate_204",
  interval: "180000",
  tolerance: "100",
  retryAttempts: "3",
  retryInterval: "1000",
  circuitBreakerFailures: "5",
  circuitBreakerSuccesses: "2",
  circuitBreakerTimeout: "30000",
  circuitBreakerHalfOpen: "1",
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
  const { t } = useTranslation()
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
        cardDescription={t("pages.outboundUpsert.missing.cardDescription")}
        cardTitle={t("pages.outboundUpsert.missing.cardTitle")}
        description={t("pages.outboundUpsert.missing.description")}
        title={t("pages.outboundUpsert.editTitle")}
      >
        <div className="flex justify-end">
          <Button onClick={() => navigate("/outbounds")} variant="outline">
            {t("pages.outboundUpsert.missing.back")}
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
      mode === "edit" ? outboundId : undefined,
      t
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

    const urltestReferencesError = validateUrltestGroupReferences(nextOutbounds, t)

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
      cardDescription={t("pages.outboundUpsert.cardDescription")}
      cardTitle={
        mode === "create"
          ? t("pages.outboundUpsert.createTitle")
          : t("pages.outboundUpsert.editCardTitle", { tag: draft.tag })
      }
      description={t("pages.outboundUpsert.description")}
      title={
        mode === "create"
          ? t("pages.outboundUpsert.createTitle")
          : t("pages.outboundUpsert.editTitle")
      }
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
        isPending={postConfigMutation.isPending}
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
  isPending,
  onCancel,
  onSubmit,
  serverFieldErrors,
}: {
  mode: "create" | "edit"
  draft: OutboundDraft
  existingOutbounds: Outbound[]
  isPending: boolean
  onCancel: () => void
  onSubmit: (payload: Outbound) => void
  serverFieldErrors: Partial<Record<OutboundFieldName, string>>
}) {
  const { t } = useTranslation()
  const [outboundType, setOutboundType] = useState<Outbound["type"]>(draft.type)
  const [strictEnforcement, setStrictEnforcement] = useState(
    draft.strictEnforcement
  )
  const [urltestGroups, setUrltestGroups] = useState<UrltestGroup[]>(
    getInitialUrltestGroups(draft.urltestGroups)
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

  useEffect(() => {
    setOutboundType(draft.type)
    setStrictEnforcement(draft.strictEnforcement)
    setUrltestGroups(getInitialUrltestGroups(draft.urltestGroups))
  }, [draft])

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
          <FieldLabel htmlFor={tagId}>{t("pages.outboundUpsert.fields.tag")}</FieldLabel>
          <FieldContent>
            <Input
              defaultValue={draft.tag}
              id={tagId}
              name="tag"
              readOnly={mode === "edit"}
            />
            <FieldHint
              description={t("pages.outboundUpsert.fields.tagHint")}
              error={serverFieldErrors.tag ?? null}
            />
          </FieldContent>
        </Field>
        <Field invalid={Boolean(serverFieldErrors.type)}>
          <FieldLabel>{t("pages.outboundUpsert.fields.type")}</FieldLabel>
          <FieldContent>
            <Select
              defaultValue={draft.type}
              onValueChange={(value) =>
                setOutboundType((value as Outbound["type"]) ?? draft.type)
              }
              items={outboundTypeOptions.map((type) => ({ value: type, label: t(`pages.outboundUpsert.fields.typeOptions.${type}`) }))}
            >
              <SelectTrigger>
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectGroup>
                  <SelectLabel>{t("pages.outboundUpsert.fields.outboundTypes")}</SelectLabel>
                  {outboundTypeOptions.map((option) => (
                    <SelectItem key={option} value={option}>
                      {t(`pages.outboundUpsert.fields.typeOptions.${option}`)}
                    </SelectItem>
                  ))}
                </SelectGroup>
              </SelectContent>
            </Select>
            <FieldHint error={serverFieldErrors.type ?? null} />
          </FieldContent>
        </Field>
      </FieldGroup>

      {isInterface ? (
        <SectionCard
          description={t("pages.outboundUpsert.interface.description")}
          title={t("pages.outboundUpsert.interface.title")}
        >
          <div className="grid gap-4 md:grid-cols-2">
            <Field invalid={Boolean(serverFieldErrors.interfaceName)}>
              <FieldLabel htmlFor={interfaceId}>{t("pages.outboundUpsert.interface.interface")}</FieldLabel>
              <FieldContent>
                <Input
                  defaultValue={draft.interfaceName}
                  id={interfaceId}
                  name="interfaceName"
                />
                <FieldHint
                  description={t("pages.outboundUpsert.interface.interfaceHint")}
                  error={serverFieldErrors.interfaceName ?? null}
                />
              </FieldContent>
            </Field>
            <Field invalid={Boolean(serverFieldErrors.gateway)}>
              <FieldLabel htmlFor={gatewayId}>{t("pages.outboundUpsert.interface.gateway")}</FieldLabel>
              <FieldContent>
                <Input
                  defaultValue={draft.gateway}
                  id={gatewayId}
                  name="gateway"
                />
                <FieldHint
                  description={t("pages.outboundUpsert.interface.gatewayHint")}
                  error={serverFieldErrors.gateway ?? null}
                />
              </FieldContent>
            </Field>
          </div>
        </SectionCard>
      ) : null}

      {isTable ? (
        <SectionCard
          description={t("pages.outboundUpsert.table.description")}
          title={t("pages.outboundUpsert.table.title")}
        >
          <Field invalid={Boolean(serverFieldErrors.table)}>
            <FieldLabel htmlFor={tableId}>{t("pages.outboundUpsert.table.field")}</FieldLabel>
            <FieldContent>
              <Input defaultValue={draft.table} id={tableId} name="table" />
              <FieldHint
                description={t("pages.outboundUpsert.table.hint")}
                error={serverFieldErrors.table ?? null}
              />
            </FieldContent>
          </Field>
        </SectionCard>
      ) : null}

      {isBlackhole ? (
        <SectionCard
          description={t("pages.outboundUpsert.blackhole.description")}
          title={t("pages.outboundUpsert.blackhole.title")}
        >
          <p className="text-sm text-muted-foreground md:text-xs">
            {t("pages.outboundUpsert.common.noExtraFields")}
          </p>
        </SectionCard>
      ) : null}

      {isIgnore ? (
        <SectionCard
          description={t("pages.outboundUpsert.ignore.description")}
          title={t("pages.outboundUpsert.ignore.title")}
        >
          <p className="text-sm text-muted-foreground md:text-xs">
            {t("pages.outboundUpsert.common.noExtraFields")}
          </p>
        </SectionCard>
      ) : null}

      {isUrltest ? (
        <SectionCard
          description={t("pages.outboundUpsert.urltest.groupsDescription")}
          title={t("pages.outboundUpsert.urltest.groupsTitle")}
        >
          <div className="space-y-4">
            {urltestGroups.map((group, index) => (
              <OrderedGroupCard
                canMoveDown={index !== urltestGroups.length - 1}
                canMoveUp={index !== 0}
                canRemove={urltestGroups.length !== 1}
                description={t("pages.outboundUpsert.urltest.groupDescription", { index: index + 1 })}
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
                title={t("pages.outboundUpsert.urltest.groupTitle", { index: index + 1 })}
              >
                <Field invalid={Boolean(serverFieldErrors.outbounds)}>
                  <FieldLabel>{t("pages.outboundUpsert.urltest.interfaceOutbounds")}</FieldLabel>
                  <FieldContent>
                    {interfaceOutboundOptions.length ? (
                      <MultiSelectList
                        addLabel={t("pages.outboundUpsert.urltest.addOutbound")}
                        emptyMessage={t("pages.outboundUpsert.urltest.noInterfaceOutbounds")}
                        groupLabel={t("pages.outboundUpsert.urltest.interfaceOutbounds")}
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
                        {t("pages.outboundUpsert.urltest.addInterfaceOutboundsFirst")}
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
                {t("pages.outboundUpsert.urltest.addGroup")}
              </Button>
            </div>
          </div>
        </SectionCard>
      ) : null}

      {isUrltest ? (
        <SectionCard
          description={t("pages.outboundUpsert.urltest.probingDescription")}
          title={t("pages.outboundUpsert.urltest.probingTitle")}
        >
          <div className="grid gap-4 md:grid-cols-2">
            <Field invalid={Boolean(serverFieldErrors.probeUrl)}>
              <FieldLabel htmlFor={probeUrlId}>{t("pages.outboundUpsert.urltest.probeUrl")}</FieldLabel>
              <FieldContent>
                <Input
                  defaultValue={draft.probeUrl}
                  id={probeUrlId}
                  name="probeUrl"
                />
                <FieldHint
                  description={t("pages.outboundUpsert.urltest.probeUrlHint")}
                  error={serverFieldErrors.probeUrl ?? null}
                />
              </FieldContent>
            </Field>
            <Field invalid={Boolean(serverFieldErrors.interval)}>
              <FieldLabel htmlFor={intervalId}>{t("pages.outboundUpsert.urltest.interval")}</FieldLabel>
              <FieldContent>
                <Input
                  defaultValue={draft.interval}
                  id={intervalId}
                  name="interval"
                />
                <FieldHint
                  description={t("pages.outboundUpsert.urltest.intervalHint")}
                  error={serverFieldErrors.interval ?? null}
                />
              </FieldContent>
            </Field>
            <Field invalid={Boolean(serverFieldErrors.tolerance)}>
              <FieldLabel htmlFor={toleranceId}>{t("pages.outboundUpsert.urltest.tolerance")}</FieldLabel>
              <FieldContent>
                <Input
                  defaultValue={draft.tolerance}
                  id={toleranceId}
                  name="tolerance"
                />
                <FieldHint
                  description={t("pages.outboundUpsert.urltest.toleranceHint")}
                  error={serverFieldErrors.tolerance ?? null}
                />
              </FieldContent>
            </Field>
            <Field invalid={Boolean(serverFieldErrors.retryAttempts)}>
              <FieldLabel htmlFor={retryAttemptsId}>{t("pages.outboundUpsert.urltest.retryAttempts")}</FieldLabel>
              <FieldContent>
                <Input
                  defaultValue={draft.retryAttempts}
                  id={retryAttemptsId}
                  name="retryAttempts"
                />
                <FieldHint
                  description={t("pages.outboundUpsert.urltest.retryAttemptsHint")}
                  error={serverFieldErrors.retryAttempts ?? null}
                />
              </FieldContent>
            </Field>
            <Field invalid={Boolean(serverFieldErrors.retryInterval)}>
              <FieldLabel htmlFor={retryIntervalId}>
                {t("pages.outboundUpsert.urltest.retryInterval")}
              </FieldLabel>
              <FieldContent>
                <Input
                  defaultValue={draft.retryInterval}
                  id={retryIntervalId}
                  name="retryInterval"
                />
                <FieldHint
                  description={t("pages.outboundUpsert.urltest.retryIntervalHint")}
                  error={serverFieldErrors.retryInterval ?? null}
                />
              </FieldContent>
            </Field>
          </div>
        </SectionCard>
      ) : null}

      {isUrltest ? (
        <SectionCard
          description={t("pages.outboundUpsert.circuitBreaker.description")}
          title={t("pages.outboundUpsert.circuitBreaker.title")}
        >
          <div className="grid gap-4 md:grid-cols-2">
            <Field invalid={Boolean(serverFieldErrors.circuitBreakerFailures)}>
              <FieldLabel htmlFor={circuitBreakerFailuresId}>
                {t("pages.outboundUpsert.circuitBreaker.failures")}
              </FieldLabel>
              <FieldContent>
                <Input
                  defaultValue={draft.circuitBreakerFailures}
                  id={circuitBreakerFailuresId}
                  name="circuitBreakerFailures"
                />
                <FieldHint
                  description={t("pages.outboundUpsert.circuitBreaker.failuresHint")}
                  error={serverFieldErrors.circuitBreakerFailures ?? null}
                />
              </FieldContent>
            </Field>
            <Field invalid={Boolean(serverFieldErrors.circuitBreakerSuccesses)}>
              <FieldLabel htmlFor={circuitBreakerSuccessesId}>
                {t("pages.outboundUpsert.circuitBreaker.successes")}
              </FieldLabel>
              <FieldContent>
                <Input
                  defaultValue={draft.circuitBreakerSuccesses}
                  id={circuitBreakerSuccessesId}
                  name="circuitBreakerSuccesses"
                />
                <FieldHint
                  description={t("pages.outboundUpsert.circuitBreaker.successesHint")}
                  error={serverFieldErrors.circuitBreakerSuccesses ?? null}
                />
              </FieldContent>
            </Field>
            <Field invalid={Boolean(serverFieldErrors.circuitBreakerTimeout)}>
              <FieldLabel htmlFor={circuitBreakerTimeoutId}>
                {t("pages.outboundUpsert.circuitBreaker.timeout")}
              </FieldLabel>
              <FieldContent>
                <Input
                  defaultValue={draft.circuitBreakerTimeout}
                  id={circuitBreakerTimeoutId}
                  name="circuitBreakerTimeout"
                />
                <FieldHint
                  description={t("pages.outboundUpsert.circuitBreaker.timeoutHint")}
                  error={serverFieldErrors.circuitBreakerTimeout ?? null}
                />
              </FieldContent>
            </Field>
            <Field invalid={Boolean(serverFieldErrors.circuitBreakerHalfOpen)}>
              <FieldLabel htmlFor={circuitBreakerHalfOpenId}>
                {t("pages.outboundUpsert.circuitBreaker.halfOpen")}
              </FieldLabel>
              <FieldContent>
                <Input
                  defaultValue={draft.circuitBreakerHalfOpen}
                  id={circuitBreakerHalfOpenId}
                  name="circuitBreakerHalfOpen"
                />
                <FieldHint
                  description={t("pages.outboundUpsert.circuitBreaker.halfOpenHint")}
                  error={serverFieldErrors.circuitBreakerHalfOpen ?? null}
                />
              </FieldContent>
            </Field>
          </div>
        </SectionCard>
      ) : null}

      {isInterface ? (
        <Field invalid={Boolean(serverFieldErrors.strictEnforcement)}>
          <FieldLabel>{t("pages.outboundUpsert.strictEnforcement.label")}</FieldLabel>
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
                  <SelectLabel>{t("pages.outboundUpsert.strictEnforcement.label")}</SelectLabel>
                  {strictOptions.map((option) => (
                    <SelectItem key={option} value={option}>
                      {getStrictOptionLabel(option, t)}
                    </SelectItem>
                  ))}
                </SelectGroup>
              </SelectContent>
            </Select>
            <FieldHint
              description={t("pages.outboundUpsert.strictEnforcement.hint")}
              error={serverFieldErrors.strictEnforcement ?? null}
            />
          </FieldContent>
        </Field>
      ) : null}

      <div className="flex justify-end gap-3">
        <Button onClick={onCancel} size="xl" type="button" variant="outline">
          {t("common.cancel")}
        </Button>
        <Button disabled={isPending} size="xl" type="submit">
          {mode === "create"
            ? t("pages.outboundUpsert.actions.create")
            : t("pages.outboundUpsert.actions.save")}
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
    urltestGroups:
      outbound.outbound_groups?.map((group) => [...group.outbounds]) ??
      sampleNewOutbound.urltestGroups,
    probeUrl: outbound.url ?? sampleNewOutbound.probeUrl,
    interval: outbound.interval_ms?.toString() ?? sampleNewOutbound.interval,
    tolerance: outbound.tolerance_ms?.toString() ?? sampleNewOutbound.tolerance,
    retryAttempts:
      outbound.retry?.attempts?.toString() ?? sampleNewOutbound.retryAttempts,
    retryInterval:
      outbound.retry?.interval_ms?.toString() ??
      sampleNewOutbound.retryInterval,
    circuitBreakerFailures:
      outbound.circuit_breaker?.failure_threshold?.toString() ??
      sampleNewOutbound.circuitBreakerFailures,
    circuitBreakerSuccesses:
      outbound.circuit_breaker?.success_threshold?.toString() ??
      sampleNewOutbound.circuitBreakerSuccesses,
    circuitBreakerTimeout:
      outbound.circuit_breaker?.timeout_ms?.toString() ??
      sampleNewOutbound.circuitBreakerTimeout,
    circuitBreakerHalfOpen:
      outbound.circuit_breaker?.half_open_max_requests?.toString() ??
      sampleNewOutbound.circuitBreakerHalfOpen,
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

function getInitialUrltestGroups(groups: string[][]) {
  if (!groups.length) {
    return [createUrltestGroup([])]
  }

  return groups.map((group) =>
    createUrltestGroup(group.map((value) => value.trim()).filter(Boolean))
  )
}

function createUrltestGroup(outbounds: string[]): UrltestGroup {
  return {
    id: createClientId(),
    outbounds,
  }
}

function createClientId(): string {
  const id = nextUrltestGroupId
  nextUrltestGroupId += 1
  return `urltest-group-${id}`
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

function getStrictOptionLabel(
  value: (typeof strictOptions)[number],
  t: (key: string) => string
) {
  if (value === strictOptions[0]) {
    return t("pages.outboundUpsert.strictEnforcement.default")
  }

  if (value === strictOptions[1]) {
    return t("common.enabled")
  }

  return t("common.disabled")
}

function validateTagUniqueness(
  outbounds: Outbound[],
  tag: string,
  existingTag: string | undefined,
  t: (key: string, options?: Record<string, unknown>) => string
): string | null {
  const isDuplicate = outbounds.some(
    (outbound) => outbound.tag === tag && outbound.tag !== existingTag
  )
  return isDuplicate
    ? t("pages.outboundUpsert.validation.duplicateTag", { tag })
    : null
}

function validateUrltestGroupReferences(
  outbounds: Outbound[],
  t: (key: string, options?: Record<string, unknown>) => string
): string | null {
  const tags = new Set(outbounds.map((outbound) => outbound.tag))

  for (const outbound of outbounds) {
    if (outbound.type !== "urltest") {
      continue
    }

    for (const group of outbound.outbound_groups ?? []) {
      for (const referencedTag of group.outbounds) {
        if (!tags.has(referencedTag)) {
          return t("pages.outboundUpsert.validation.missingReference", {
            outbound: outbound.tag,
            referenced: referencedTag,
          })
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
