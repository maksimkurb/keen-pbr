import { Plus } from "lucide-react"
import { useId } from "react"
import { useTranslation } from "react-i18next"

import { revalidateLogic, useForm } from "@tanstack/react-form"
import { useQueryClient } from "@tanstack/react-query"
import { useStore } from "@tanstack/react-store"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { Outbound } from "@/api/generated/model/outbound"
import type { RuntimeInterfaceInventoryEntry } from "@/api/generated/model/runtimeInterfaceInventoryEntry"
import { usePostConfigMutation } from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
import { useGetConfig, useGetRuntimeInterfaces } from "@/api/queries"
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
import {
  InterfacePicker,
  OutboundInterfaceLabel,
} from "@/components/shared/interface-picker"
import { MultiSelectList } from "@/components/shared/multi-select-list"
import { OrderedGroupCard } from "@/components/shared/ordered-group-card"
import { SectionCard } from "@/components/shared/section-card"
import { ServerValidationAlert } from "@/components/shared/server-validation-alert"
import { UpsertPage } from "@/components/shared/upsert-page"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import {
  clearFormServerErrors,
  setFormServerErrors,
  splitFormApiErrors,
} from "@/lib/form-api-errors"
import { getTagNameValidationError } from "@/lib/tag-name-validation"
import { getInterfaceSearchText } from "@/lib/runtime-interfaces"
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
  gateway6: string
  table: string
  outboundGroups: OutboundGroupDraft[]
  probeUrl: string
  interval: string
  tolerance: string
  count: string
  maxFailed: string
  packetInterval: string
  probeTimeout: string
  maxRtt: string
  retryAttempts: string
  retryInterval: string
  circuitBreakerFailures: string
  circuitBreakerSuccesses: string
  circuitBreakerTimeout: string
  circuitBreakerHalfOpen: string
  strictEnforcement: string
}

type OutboundGroupDraft = {
  outbounds: string[]
  candidates: IcmptestCandidateDraft[]
}

type IcmptestCandidateDraft = {
  outbound: string
  target: string
}

const OUTBOUND_FIELD_NAMES = {
  tag: "tag",
  type: "type",
  interfaceName: "interfaceName",
  gateway: "gateway",
  gateway6: "gateway6",
  table: "table",
  outboundGroups: "outboundGroups",
  probeUrl: "probeUrl",
  interval: "interval",
  tolerance: "tolerance",
  count: "count",
  maxFailed: "maxFailed",
  packetInterval: "packetInterval",
  probeTimeout: "probeTimeout",
  maxRtt: "maxRtt",
  retryAttempts: "retryAttempts",
  retryInterval: "retryInterval",
  circuitBreakerFailures: "circuitBreakerFailures",
  circuitBreakerSuccesses: "circuitBreakerSuccesses",
  circuitBreakerTimeout: "circuitBreakerTimeout",
  circuitBreakerHalfOpen: "circuitBreakerHalfOpen",
  strictEnforcement: "strictEnforcement",
} as const

const sampleNewOutbound: OutboundDraft = {
  tag: "",
  type: "interface",
  interfaceName: "",
  gateway: "",
  gateway6: "",
  table: "",
  outboundGroups: [{ outbounds: [], candidates: [] }],
  probeUrl: "https://www.gstatic.com/generate_204",
  interval: "180000",
  tolerance: "100",
  count: "3",
  maxFailed: "0",
  packetInterval: "200",
  probeTimeout: "1000",
  maxRtt: "500",
  retryAttempts: "3",
  retryInterval: "1000",
  circuitBreakerFailures: "5",
  circuitBreakerSuccesses: "2",
  circuitBreakerTimeout: "30000",
  circuitBreakerHalfOpen: "1",
  strictEnforcement: "default",
}

const strictOptions = ["default", "enabled", "disabled"] as const

const outboundTypeOptions: Outbound["type"][] = [
  "interface",
  "table",
  "blackhole",
  "ignore",
  "urltest",
  "icmptest",
]

export function OutboundUpsertPage({
  mode,
  outboundId,
}: {
  mode: "create" | "edit"
  outboundId?: string
}) {
  const { t } = useTranslation()
  const [, navigate] = useLocation()
  const configQuery = useGetConfig()
  const loadedConfig = selectConfig(configQuery.data)

  if (!loadedConfig) {
    return (
      <UpsertPage
        cardDescription={t("pages.outboundUpsert.cardDescription")}
        cardTitle={
          mode === "create"
            ? t("pages.outboundUpsert.createTitle")
            : t("pages.outboundUpsert.editTitle")
        }
        description={t("pages.outboundUpsert.description")}
        title={
          mode === "create"
            ? t("pages.outboundUpsert.createTitle")
            : t("pages.outboundUpsert.editTitle")
        }
      >
        <div className="space-y-3">
          <div className="h-8 rounded-lg bg-muted" />
          <div className="h-24 rounded-lg bg-muted" />
          <div className="h-8 rounded-lg bg-muted" />
          <div className="h-8 rounded-lg bg-muted" />
        </div>
      </UpsertPage>
    )
  }

  const draft =
    getOutboundDraft(loadedConfig, mode === "edit" ? outboundId : undefined) ??
    sampleNewOutbound

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
      <OutboundForm
        key={`${mode}:${outboundId ?? "new"}`}
        draft={draft}
        loadedConfig={loadedConfig}
        mode={mode}
        onCancel={() => navigate("/outbounds")}
        outboundId={outboundId}
      />
    </UpsertPage>
  )
}

function OutboundForm({
  mode,
  draft,
  loadedConfig,
  onCancel,
  outboundId,
}: {
  mode: "create" | "edit"
  draft: OutboundDraft
  loadedConfig: ConfigObject
  onCancel: () => void
  outboundId?: string
}) {
  const { t } = useTranslation()
  const queryClient = useQueryClient()
  const [, navigate] = useLocation()
  const existingOutbounds = selectOutbounds(loadedConfig)
  const runtimeInterfacesQuery = useGetRuntimeInterfaces()
  const runtimeInterfaces =
    runtimeInterfacesQuery.data?.status === 200
      ? runtimeInterfacesQuery.data.data.interfaces
      : []
  const runtimeInterfaceByName = new Map(
    runtimeInterfaces.map((runtimeInterface) => [
      runtimeInterface.name,
      runtimeInterface,
    ])
  )
  const candidateOutboundByTag = new Map(
    existingOutbounds
      .filter(
        (item) =>
          (item.type === "interface" || item.type === "table") &&
          item.tag !== draft.tag
      )
      .map((item) => [item.tag, item])
  )
  const candidateOutboundOptions = existingOutbounds
    .filter(
      (item) =>
        (item.type === "interface" || item.type === "table") &&
        item.tag !== draft.tag
    )
    .map((item) => item.tag)
  const strictSelectItems = strictOptions.map((option) => ({
    value: option,
    label: getStrictOptionLabel(option, t),
  }))
  const form = useForm({
    defaultValues: draft,
    validationLogic: revalidateLogic({
      mode: "submit",
      modeAfterSubmission: "change",
    }),
    validators: {
      onSubmitAsync: async ({ value }) => {
        clearFormServerErrors(form)
        const duplicateTagError = validateTagUniqueness(
          existingOutbounds,
          value.tag,
          mode === "edit" ? outboundId : undefined,
          t
        )
        if (duplicateTagError) {
          setFormServerErrors(form, {
            fields: {
              [OUTBOUND_FIELD_NAMES.tag]: duplicateTagError,
            },
          })
          return undefined
        }

        const payload = buildOutboundPayload(value)
        const nextOutbounds =
          mode === "create"
            ? [...existingOutbounds, payload]
            : existingOutbounds.map((outbound) =>
                outbound.tag === outboundId ? payload : outbound
              )

        const urltestReferencesError = validateUrltestGroupReferences(
          nextOutbounds,
          t
        )
        if (urltestReferencesError) {
          setFormServerErrors(form, {
            form: urltestReferencesError,
            fields: {},
          })
          return undefined
        }

        try {
          await postConfigMutation.mutateAsync({
            data: {
              ...loadedConfig,
              outbounds: nextOutbounds,
            } satisfies ConfigObject,
          })
          clearFormServerErrors(form)
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
          return undefined
        } catch (error) {
          const apiError = error as ApiError
          const result = splitFormApiErrors({
            error: apiError,
            resolvePath: (path) =>
              resolveOutboundFieldPath(path, payload.tag || draft.tag),
          })

          setFormServerErrors(form, {
            form: result.formError ?? undefined,
            fields: result.fieldErrors,
            unmapped: result.unmappedErrors,
          })

          return undefined
        }
      },
    },
  })

  const postConfigMutation = usePostConfigMutation()

  const outboundType = useStore(form.store, (state) => state.values.type)
  const selectedGroups = useStore(
    form.store,
    (state) => state.values.outboundGroups
  )
  const apiErrorMessage = useStore(
    form.store,
    (state) =>
      (state.errorMap.onServer as { form?: string } | undefined)?.form ?? null
  )
  const unmappedServerErrors = useStore(
    form.store,
    (state) =>
      (
        state.errorMap.onServer as
          | { unmapped?: { path: string; message: string }[] }
          | undefined
      )?.unmapped ?? []
  )

  const isInterface = outboundType === "interface"
  const isTable = outboundType === "table"
  const isBlackhole = outboundType === "blackhole"
  const isIgnore = outboundType === "ignore"
  const isUrltest = outboundType === "urltest"
  const isIcmptest = outboundType === "icmptest"
  const isProbeTest = isUrltest || isIcmptest
  const tagId = useId()
  const interfaceId = useId()
  const gatewayId = useId()
  const gateway6Id = useId()
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
  const countId = useId()
  const maxFailedId = useId()
  const packetIntervalId = useId()
  const probeTimeoutId = useId()
  const maxRttId = useId()

  return (
    <form
      className="space-y-6"
      onSubmit={(event) => {
        event.preventDefault()
        void form.handleSubmit()
      }}
    >
      {apiErrorMessage ? (
        <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription className="whitespace-pre-wrap">
            {apiErrorMessage}
          </AlertDescription>
        </Alert>
      ) : null}

      <FieldGroup>
        <form.Field
          name={OUTBOUND_FIELD_NAMES.tag}
          validators={{
            onChange: ({ value }) =>
              getOutboundTagError(
                value,
                existingOutbounds,
                mode === "edit" ? outboundId : undefined,
                t
              ) ?? undefined,
          }}
        >
          {(field) => {
            const error = getFirstFieldError(field.state.meta.errors)
            return (
              <Field invalid={Boolean(error)}>
                <FieldLabel htmlFor={tagId}>
                  {t("pages.outboundUpsert.fields.tag")}
                </FieldLabel>
                <FieldContent>
                  <Input
                    aria-invalid={Boolean(error)}
                    id={tagId}
                    onBlur={field.handleBlur}
                    onChange={(event) => field.handleChange(event.target.value)}
                    readOnly={mode === "edit"}
                    value={field.state.value}
                  />
                  <FieldHint
                    description={t("pages.outboundUpsert.fields.tagHint")}
                    error={error ?? null}
                  />
                </FieldContent>
              </Field>
            )
          }}
        </form.Field>

        <form.Field name={OUTBOUND_FIELD_NAMES.type}>
          {(field) => {
            const error = getFirstFieldError(field.state.meta.errors)
            return (
              <Field invalid={Boolean(error)}>
                <FieldLabel>{t("pages.outboundUpsert.fields.type")}</FieldLabel>
                <FieldContent>
                  <Select
                    items={outboundTypeOptions.map((type) => ({
                      value: type,
                      label: t(
                        `pages.outboundUpsert.fields.typeOptions.${type}`
                      ),
                    }))}
                    onValueChange={(value) => {
                      const nextType = (value as Outbound["type"]) ?? draft.type
                      if (
                        nextType === "icmptest" &&
                        field.state.value !== "icmptest"
                      ) {
                        form.setFieldValue(
                          OUTBOUND_FIELD_NAMES.interval,
                          "60000"
                        )
                        form.setFieldValue(OUTBOUND_FIELD_NAMES.tolerance, "10")
                        form.setFieldValue(
                          OUTBOUND_FIELD_NAMES.circuitBreakerTimeout,
                          "60000"
                        )
                      } else if (
                        nextType === "urltest" &&
                        field.state.value === "icmptest"
                      ) {
                        form.setFieldValue(
                          OUTBOUND_FIELD_NAMES.interval,
                          "180000"
                        )
                        form.setFieldValue(
                          OUTBOUND_FIELD_NAMES.tolerance,
                          "100"
                        )
                        form.setFieldValue(
                          OUTBOUND_FIELD_NAMES.circuitBreakerTimeout,
                          "30000"
                        )
                      }
                      field.handleChange(nextType)
                    }}
                    value={field.state.value}
                  >
                    <SelectTrigger aria-invalid={Boolean(error)}>
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectGroup>
                        <SelectLabel>
                          {t("pages.outboundUpsert.fields.outboundTypes")}
                        </SelectLabel>
                        {outboundTypeOptions.map((option) => (
                          <SelectItem key={option} value={option}>
                            {t(
                              `pages.outboundUpsert.fields.typeOptions.${option}`
                            )}
                          </SelectItem>
                        ))}
                      </SelectGroup>
                    </SelectContent>
                  </Select>
                  <FieldHint error={error ?? null} />
                </FieldContent>
              </Field>
            )
          }}
        </form.Field>
      </FieldGroup>

      {isInterface ? (
        <SectionCard
          description={t("pages.outboundUpsert.interface.description")}
          title={t("pages.outboundUpsert.interface.title")}
        >
          <div className="grid gap-4">
            <form.Field name={OUTBOUND_FIELD_NAMES.interfaceName}>
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)
                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor={interfaceId}>
                      {t("pages.outboundUpsert.interface.interface")}
                    </FieldLabel>
                    <FieldContent>
                      <InterfacePicker
                        allowCustomOption
                        id={interfaceId}
                        interfaces={runtimeInterfaces}
                        invalid={Boolean(error)}
                        onChange={field.handleChange}
                        onSelect={field.handleChange}
                        placeholder={t(
                          "pages.outboundUpsert.interface.interfacePlaceholder"
                        )}
                        renderSelectedInline
                        showDetails={false}
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t(
                          "pages.outboundUpsert.interface.interfaceHint"
                        )}
                        error={error ?? null}
                      />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>

            <form.Field name={OUTBOUND_FIELD_NAMES.gateway}>
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)
                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor={gatewayId}>
                      {t("pages.outboundUpsert.interface.gateway")}
                    </FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id={gatewayId}
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t(
                          "pages.outboundUpsert.interface.gatewayHint"
                        )}
                        error={error ?? null}
                      />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>

            <form.Field name={OUTBOUND_FIELD_NAMES.gateway6}>
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)
                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor={gateway6Id}>
                      {t("pages.outboundUpsert.interface.gateway6")}
                    </FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id={gateway6Id}
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t(
                          "pages.outboundUpsert.interface.gateway6Hint"
                        )}
                        error={error ?? null}
                      />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>
          </div>
        </SectionCard>
      ) : null}

      {isTable ? (
        <SectionCard
          description={t("pages.outboundUpsert.table.description")}
          title={t("pages.outboundUpsert.table.title")}
        >
          <form.Field name={OUTBOUND_FIELD_NAMES.table}>
            {(field) => {
              const error = getFirstFieldError(field.state.meta.errors)
              return (
                <Field invalid={Boolean(error)}>
                  <FieldLabel htmlFor={tableId}>
                    {t("pages.outboundUpsert.table.field")}
                  </FieldLabel>
                  <FieldContent>
                    <Input
                      aria-invalid={Boolean(error)}
                      id={tableId}
                      onBlur={field.handleBlur}
                      onChange={(event) =>
                        field.handleChange(event.target.value)
                      }
                      value={field.state.value}
                    />
                    <FieldHint
                      description={t("pages.outboundUpsert.table.hint")}
                      error={error ?? null}
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>
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

      {isProbeTest ? (
        <form.Field name={OUTBOUND_FIELD_NAMES.outboundGroups}>
          {(field) => {
            const groups = getOutboundGroupTags(field.state.value, isIcmptest)
            const handleGroupsChange = (nextGroups: string[][]) => {
              const normalizedGroups = normalizeOutboundGroups(nextGroups)
              field.handleChange(
                synchronizeOutboundGroups(
                  field.state.value,
                  normalizedGroups,
                  isIcmptest
                )
              )
            }
            return (
              <SectionCard
                description={t(
                  "pages.outboundUpsert.urltest.groupsDescription"
                )}
                title={t("pages.outboundUpsert.urltest.groupsTitle")}
              >
                <div className="space-y-4">
                  {groups.map((group, index) => (
                    <form.Field
                      key={`${index}-${group.join(",")}`}
                      name={`outboundGroups[${index}]`}
                    >
                      {(groupField) => {
                        const groupError = getFirstFieldError(
                          groupField.state.meta.errors
                        )
                        return (
                          <OrderedGroupCard
                            canMoveDown={index !== groups.length - 1}
                            canMoveUp={index !== 0}
                            canRemove={groups.length !== 1}
                            description={t(
                              "pages.outboundUpsert.urltest.groupDescription",
                              { index: index + 1 }
                            )}
                            onMoveDown={() =>
                              handleGroupsChange(
                                moveGroup(groups, index, index + 1)
                              )
                            }
                            onMoveUp={() =>
                              handleGroupsChange(
                                moveGroup(groups, index, index - 1)
                              )
                            }
                            onRemove={() =>
                              handleGroupsChange(
                                groups.length === 1
                                  ? groups
                                  : normalizeOutboundGroups(
                                      groups.filter(
                                        (_, currentIndex) =>
                                          currentIndex !== index
                                      )
                                    )
                              )
                            }
                            title={t(
                              "pages.outboundUpsert.urltest.groupTitle",
                              {
                                index: index + 1,
                              }
                            )}
                          >
                            <Field invalid={Boolean(groupError)}>
                              <FieldLabel>
                                {t(
                                  "pages.outboundUpsert.urltest.interfaceOutbounds"
                                )}
                              </FieldLabel>
                              <FieldContent>
                                {candidateOutboundOptions.length ? (
                                  <MultiSelectList
                                    error={groupError}
                                    name={OUTBOUND_FIELD_NAMES.outboundGroups}
                                    addLabel={t(
                                      "pages.outboundUpsert.urltest.addOutbound"
                                    )}
                                    emptyMessage={t(
                                      "pages.outboundUpsert.urltest.noInterfaceOutbounds"
                                    )}
                                    groupLabel={t(
                                      "pages.outboundUpsert.urltest.interfaceOutbounds"
                                    )}
                                    onChange={(nextOutbounds) =>
                                      handleGroupsChange(
                                        groups.map((item, itemIndex) =>
                                          itemIndex === index
                                            ? nextOutbounds
                                            : item
                                        )
                                      )
                                    }
                                    options={candidateOutboundOptions}
                                    getSearchText={(tag) =>
                                      getInterfaceOutboundSearchText(
                                        tag,
                                        candidateOutboundByTag.get(tag)
                                          ?.interface,
                                        runtimeInterfaceByName
                                      )
                                    }
                                    renderItem={(tag) => (
                                      <OutboundInterfaceLabel
                                        interfaceName={
                                          candidateOutboundByTag.get(tag)
                                            ?.interface
                                        }
                                        runtimeInterface={runtimeInterfaceByName.get(
                                          candidateOutboundByTag.get(tag)
                                            ?.interface ?? ""
                                        )}
                                        t={t}
                                        tag={tag}
                                      />
                                    )}
                                    unavailable={getUnavailableOutbounds(
                                      groups,
                                      index
                                    )}
                                    value={group}
                                  />
                                ) : (
                                  <div className="rounded-lg border border-border p-3 text-sm text-muted-foreground md:text-xs">
                                    {t(
                                      "pages.outboundUpsert.urltest.addInterfaceOutboundsFirst"
                                    )}
                                  </div>
                                )}
                              </FieldContent>
                            </Field>
                          </OrderedGroupCard>
                        )
                      }}
                    </form.Field>
                  ))}
                  <div className="flex justify-start">
                    <Button
                      onClick={() =>
                        handleGroupsChange([
                          ...groups,
                          getNextAvailableOutbounds(
                            candidateOutboundOptions,
                            groups
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
            )
          }}
        </form.Field>
      ) : null}

      {isUrltest ? (
        <SectionCard
          description={t("pages.outboundUpsert.urltest.probingDescription")}
          title={t("pages.outboundUpsert.urltest.probingTitle")}
        >
          <div className="grid gap-4 md:grid-cols-2">
            <form.Field name={OUTBOUND_FIELD_NAMES.probeUrl}>
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)
                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor={probeUrlId}>
                      {t("pages.outboundUpsert.urltest.probeUrl")}
                    </FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id={probeUrlId}
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t(
                          "pages.outboundUpsert.urltest.probeUrlHint"
                        )}
                        error={error ?? null}
                      />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>

            <form.Field name={OUTBOUND_FIELD_NAMES.interval}>
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)
                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor={intervalId}>
                      {t("pages.outboundUpsert.urltest.interval")}
                    </FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id={intervalId}
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t(
                          "pages.outboundUpsert.urltest.intervalHint"
                        )}
                        error={error ?? null}
                      />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>

            <form.Field name={OUTBOUND_FIELD_NAMES.tolerance}>
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)
                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor={toleranceId}>
                      {t("pages.outboundUpsert.urltest.tolerance")}
                    </FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id={toleranceId}
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t(
                          "pages.outboundUpsert.urltest.toleranceHint"
                        )}
                        error={error ?? null}
                      />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>

            <form.Field name={OUTBOUND_FIELD_NAMES.retryAttempts}>
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)
                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor={retryAttemptsId}>
                      {t("pages.outboundUpsert.urltest.retryAttempts")}
                    </FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id={retryAttemptsId}
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t(
                          "pages.outboundUpsert.urltest.retryAttemptsHint"
                        )}
                        error={error ?? null}
                      />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>

            <form.Field name={OUTBOUND_FIELD_NAMES.retryInterval}>
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)
                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor={retryIntervalId}>
                      {t("pages.outboundUpsert.urltest.retryInterval")}
                    </FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id={retryIntervalId}
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t(
                          "pages.outboundUpsert.urltest.retryIntervalHint"
                        )}
                        error={error ?? null}
                      />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>
          </div>
        </SectionCard>
      ) : null}

      {isIcmptest ? (
        <SectionCard
          description={t("pages.outboundUpsert.icmptest.description")}
          title={t("pages.outboundUpsert.icmptest.title")}
        >
          <div className="space-y-5">
            <div className="grid gap-4">
              {selectedGroups.some((group) => group.candidates.length) ? (
                selectedGroups.map((group, groupIndex) =>
                  group.candidates.map((candidate, candidateIndex) => (
                    <form.Field
                      key={`${groupIndex}-${candidateIndex}-${candidate.outbound}`}
                      name={`outboundGroups[${groupIndex}].candidates[${candidateIndex}].target`}
                    >
                      {(field) => {
                        const error = getFirstFieldError(
                          field.state.meta.errors
                        )
                        return (
                          <Field invalid={Boolean(error)}>
                            <FieldLabel>
                              {t("pages.outboundUpsert.icmptest.targetLabel", {
                                outbound: candidate.outbound,
                              })}
                            </FieldLabel>
                            <FieldContent>
                              <Input
                                aria-invalid={Boolean(error)}
                                onBlur={field.handleBlur}
                                onChange={(event) =>
                                  field.handleChange(event.target.value)
                                }
                                placeholder="1.1.1.1"
                                value={field.state.value ?? ""}
                              />
                              <FieldHint
                                description={t(
                                  "pages.outboundUpsert.icmptest.targetHint"
                                )}
                                error={error ?? null}
                              />
                            </FieldContent>
                          </Field>
                        )
                      }}
                    </form.Field>
                  ))
                )
              ) : (
                <p className="text-sm text-muted-foreground md:text-xs">
                  {t("pages.outboundUpsert.icmptest.targetsEmpty")}
                </p>
              )}
            </div>

            <div className="grid gap-4 md:grid-cols-2">
              <form.Field name={OUTBOUND_FIELD_NAMES.count}>
                {(field) => (
                  <IcmpNumberField
                    field={field}
                    id={countId}
                    label={t("pages.outboundUpsert.icmptest.count")}
                    hint={t("pages.outboundUpsert.icmptest.countHint")}
                  />
                )}
              </form.Field>
              <form.Field name={OUTBOUND_FIELD_NAMES.maxFailed}>
                {(field) => (
                  <IcmpNumberField
                    field={field}
                    id={maxFailedId}
                    label={t("pages.outboundUpsert.icmptest.maxFailed")}
                    hint={t("pages.outboundUpsert.icmptest.maxFailedHint")}
                  />
                )}
              </form.Field>
              <form.Field name={OUTBOUND_FIELD_NAMES.packetInterval}>
                {(field) => (
                  <IcmpNumberField
                    field={field}
                    id={packetIntervalId}
                    label={t("pages.outboundUpsert.icmptest.packetInterval")}
                    hint={t("pages.outboundUpsert.icmptest.packetIntervalHint")}
                  />
                )}
              </form.Field>
              <form.Field name={OUTBOUND_FIELD_NAMES.probeTimeout}>
                {(field) => (
                  <IcmpNumberField
                    field={field}
                    id={probeTimeoutId}
                    label={t("pages.outboundUpsert.icmptest.probeTimeout")}
                    hint={t("pages.outboundUpsert.icmptest.probeTimeoutHint")}
                  />
                )}
              </form.Field>
              <form.Field name={OUTBOUND_FIELD_NAMES.maxRtt}>
                {(field) => (
                  <IcmpNumberField
                    field={field}
                    id={maxRttId}
                    label={t("pages.outboundUpsert.icmptest.maxRtt")}
                    hint={t("pages.outboundUpsert.icmptest.maxRttHint")}
                  />
                )}
              </form.Field>
              <form.Field name={OUTBOUND_FIELD_NAMES.interval}>
                {(field) => (
                  <IcmpNumberField
                    field={field}
                    id={intervalId}
                    label={t("pages.outboundUpsert.icmptest.interval")}
                    hint={t("pages.outboundUpsert.icmptest.intervalHint")}
                  />
                )}
              </form.Field>
              <form.Field name={OUTBOUND_FIELD_NAMES.tolerance}>
                {(field) => (
                  <IcmpNumberField
                    field={field}
                    id={toleranceId}
                    label={t("pages.outboundUpsert.icmptest.tolerance")}
                    hint={t("pages.outboundUpsert.icmptest.toleranceHint")}
                  />
                )}
              </form.Field>
            </div>
          </div>
        </SectionCard>
      ) : null}

      {isProbeTest ? (
        <SectionCard
          description={t("pages.outboundUpsert.circuitBreaker.description")}
          title={t("pages.outboundUpsert.circuitBreaker.title")}
        >
          <div className="grid gap-4 md:grid-cols-2">
            <form.Field name={OUTBOUND_FIELD_NAMES.circuitBreakerFailures}>
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)
                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor={circuitBreakerFailuresId}>
                      {t("pages.outboundUpsert.circuitBreaker.failures")}
                    </FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id={circuitBreakerFailuresId}
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t(
                          "pages.outboundUpsert.circuitBreaker.failuresHint"
                        )}
                        error={error ?? null}
                      />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>

            <form.Field name={OUTBOUND_FIELD_NAMES.circuitBreakerSuccesses}>
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)
                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor={circuitBreakerSuccessesId}>
                      {t("pages.outboundUpsert.circuitBreaker.successes")}
                    </FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id={circuitBreakerSuccessesId}
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t(
                          "pages.outboundUpsert.circuitBreaker.successesHint"
                        )}
                        error={error ?? null}
                      />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>

            <form.Field name={OUTBOUND_FIELD_NAMES.circuitBreakerTimeout}>
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)
                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor={circuitBreakerTimeoutId}>
                      {t("pages.outboundUpsert.circuitBreaker.timeout")}
                    </FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id={circuitBreakerTimeoutId}
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t(
                          "pages.outboundUpsert.circuitBreaker.timeoutHint"
                        )}
                        error={error ?? null}
                      />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>

            <form.Field name={OUTBOUND_FIELD_NAMES.circuitBreakerHalfOpen}>
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)
                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor={circuitBreakerHalfOpenId}>
                      {t("pages.outboundUpsert.circuitBreaker.halfOpen")}
                    </FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id={circuitBreakerHalfOpenId}
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t(
                          "pages.outboundUpsert.circuitBreaker.halfOpenHint"
                        )}
                        error={error ?? null}
                      />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>
          </div>
        </SectionCard>
      ) : null}

      {isInterface ? (
        <form.Field name={OUTBOUND_FIELD_NAMES.strictEnforcement}>
          {(field) => {
            const error = getFirstFieldError(field.state.meta.errors)
            return (
              <Field invalid={Boolean(error)}>
                <FieldLabel>
                  {t("pages.outboundUpsert.strictEnforcement.label")}
                </FieldLabel>
                <FieldContent>
                  <Select
                    items={strictSelectItems}
                    onValueChange={(value) =>
                      field.handleChange(value ?? draft.strictEnforcement)
                    }
                    value={field.state.value}
                  >
                    <SelectTrigger aria-invalid={Boolean(error)}>
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectGroup>
                        <SelectLabel>
                          {t("pages.outboundUpsert.strictEnforcement.label")}
                        </SelectLabel>
                        {strictOptions.map((option) => (
                          <SelectItem key={option} value={option}>
                            {getStrictOptionLabel(option, t)}
                          </SelectItem>
                        ))}
                      </SelectGroup>
                    </SelectContent>
                  </Select>
                  <FieldHint
                    description={t(
                      "pages.outboundUpsert.strictEnforcement.hint"
                    )}
                    error={error ?? null}
                  />
                </FieldContent>
              </Field>
            )
          }}
        </form.Field>
      ) : null}

      <ServerValidationAlert errors={unmappedServerErrors} />

      <div className="flex justify-end gap-3">
        <Button onClick={onCancel} size="xl" type="button" variant="outline">
          {t("common.cancel")}
        </Button>
        <form.Subscribe
          selector={(state) => ({
            canSubmit: state.canSubmit,
            isPristine: state.isPristine,
          })}
        >
          {({ canSubmit, isPristine }) => (
            <Button
              disabled={
                postConfigMutation.isPending || isPristine || !canSubmit
              }
              size="xl"
              type="submit"
            >
              {mode === "create"
                ? t("pages.outboundUpsert.actions.create")
                : t("pages.outboundUpsert.actions.save")}
            </Button>
          )}
        </form.Subscribe>
      </div>
    </form>
  )
}

function getFirstFieldError(errors: unknown[]) {
  const firstError = errors[0]
  return typeof firstError === "string" ? firstError : null
}

function IcmpNumberField({
  field,
  id,
  label,
  hint,
}: {
  field: {
    state: { value: string; meta: { errors: unknown[] } }
    handleBlur: () => void
    handleChange: (value: string) => void
  }
  id: string
  label: string
  hint: string
}) {
  const error = getFirstFieldError(field.state.meta.errors)
  return (
    <Field invalid={Boolean(error)}>
      <FieldLabel htmlFor={id}>{label}</FieldLabel>
      <FieldContent>
        <Input
          aria-invalid={Boolean(error)}
          id={id}
          inputMode="numeric"
          onBlur={field.handleBlur}
          onChange={(event) => field.handleChange(event.target.value)}
          value={field.state.value}
        />
        <FieldHint description={hint} error={error ?? null} />
      </FieldContent>
    </Field>
  )
}

function getOutboundTagError(
  value: string,
  outbounds: Outbound[],
  existingTag: string | undefined,
  t: (key: string, options?: Record<string, unknown>) => string
) {
  return getTagNameValidationError(value, {
    requiredError: t("pages.outboundUpsert.validation.tagRequired"),
    invalidError: t("common.validation.tagNamePattern"),
    duplicateError: validateTagUniqueness(
      outbounds,
      value.trim(),
      existingTag,
      t
    ),
  })
}

function mapOutboundToDraft(outbound: Outbound): OutboundDraft {
  const isIcmp = outbound.type === "icmptest"
  const defaultBreakerTimeout = isIcmp
    ? Math.max(60000, outbound.interval_ms ?? 60000).toString()
    : sampleNewOutbound.circuitBreakerTimeout
  return {
    tag: outbound.tag,
    type: outbound.type,
    interfaceName: outbound.interface ?? "",
    gateway: outbound.gateway ?? "",
    gateway6: outbound.gateway6 ?? "",
    table: outbound.table?.toString() ?? "",
    outboundGroups:
      outbound.outbound_groups?.map((group) =>
        isIcmp
          ? {
              outbounds: [],
              candidates: (group.candidates ?? []).map((candidate) => ({
                outbound: candidate.outbound,
                target: candidate.target,
              })),
            }
          : { outbounds: [...(group.outbounds ?? [])], candidates: [] }
      ) ?? sampleNewOutbound.outboundGroups,
    probeUrl: outbound.url ?? sampleNewOutbound.probeUrl,
    interval:
      outbound.interval_ms?.toString() ??
      (isIcmp ? "60000" : sampleNewOutbound.interval),
    tolerance:
      outbound.tolerance_ms?.toString() ??
      (isIcmp ? "10" : sampleNewOutbound.tolerance),
    count: outbound.count?.toString() ?? sampleNewOutbound.count,
    maxFailed: outbound.max_failed?.toString() ?? sampleNewOutbound.maxFailed,
    packetInterval:
      outbound.packet_interval_ms?.toString() ??
      sampleNewOutbound.packetInterval,
    probeTimeout:
      outbound.probe_timeout_ms?.toString() ?? sampleNewOutbound.probeTimeout,
    maxRtt: outbound.max_rtt_ms?.toString() ?? sampleNewOutbound.maxRtt,
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
      outbound.circuit_breaker?.timeout_ms?.toString() ?? defaultBreakerTimeout,
    circuitBreakerHalfOpen:
      outbound.circuit_breaker?.half_open_max_requests?.toString() ??
      sampleNewOutbound.circuitBreakerHalfOpen,
    strictEnforcement: mapStrictEnforcementToOption(
      outbound.strict_enforcement
    ),
  }
}

function buildOutboundPayload(draft: OutboundDraft): Outbound {
  const tag = draft.tag.trim()

  if (draft.type === "interface") {
    return {
      type: "interface",
      tag,
      interface: draft.interfaceName.trim() || undefined,
      gateway: draft.gateway.trim() || undefined,
      gateway6: draft.gateway6.trim() || undefined,
      strict_enforcement: mapStrictEnforcementToBoolean(
        draft.strictEnforcement
      ),
    }
  }

  if (draft.type === "table") {
    return {
      type: "table",
      tag,
      table: parseNumber(draft.table),
    }
  }

  if (draft.type === "urltest") {
    return {
      type: "urltest",
      tag,
      url: draft.probeUrl.trim() || undefined,
      interval_ms: parseNumber(draft.interval),
      tolerance_ms: parseNumber(draft.tolerance),
      outbound_groups: getOutboundGroupTags(draft.outboundGroups, false).map(
        (group) => ({
          outbounds: group,
        })
      ),
      retry: {
        attempts: parseNumber(draft.retryAttempts),
        interval_ms: parseNumber(draft.retryInterval),
      },
      circuit_breaker: {
        failure_threshold: parseNumber(draft.circuitBreakerFailures),
        success_threshold: parseNumber(draft.circuitBreakerSuccesses),
        timeout_ms: parseNumber(draft.circuitBreakerTimeout),
        half_open_max_requests: parseNumber(draft.circuitBreakerHalfOpen),
      },
    }
  }

  if (draft.type === "icmptest") {
    return {
      type: "icmptest",
      tag,
      count: parseNumber(draft.count),
      max_failed: parseNumber(draft.maxFailed),
      packet_interval_ms: parseNumber(draft.packetInterval),
      probe_timeout_ms: parseNumber(draft.probeTimeout),
      max_rtt_ms: parseNumber(draft.maxRtt),
      interval_ms: parseNumber(draft.interval),
      tolerance_ms: parseNumber(draft.tolerance),
      outbound_groups: draft.outboundGroups.map((group) => ({
        candidates: group.candidates.map((candidate) => ({
          outbound: candidate.outbound,
          target: candidate.target.trim(),
        })),
      })),
      circuit_breaker: {
        failure_threshold: parseNumber(draft.circuitBreakerFailures),
        success_threshold: parseNumber(draft.circuitBreakerSuccesses),
        timeout_ms: parseNumber(draft.circuitBreakerTimeout),
        half_open_max_requests: parseNumber(draft.circuitBreakerHalfOpen),
      },
    }
  }

  return {
    type: draft.type,
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

function normalizeOutboundGroups(groups: string[][]) {
  if (!groups.length) {
    return [[]]
  }

  return groups.map((group) =>
    group.map((value) => value.trim()).filter(Boolean)
  )
}

function getOutboundGroupTags(
  groups: OutboundGroupDraft[],
  isIcmptest: boolean
): string[][] {
  return normalizeOutboundGroups(
    groups.map((group) =>
      isIcmptest
        ? group.candidates.map((candidate) => candidate.outbound)
        : group.outbounds
    )
  )
}

function synchronizeOutboundGroups(
  currentGroups: OutboundGroupDraft[],
  nextGroups: string[][],
  isIcmptest: boolean
): OutboundGroupDraft[] {
  const targetsByOutbound = new Map<string, string>()

  for (const group of currentGroups) {
    for (const candidate of group.candidates) {
      targetsByOutbound.set(candidate.outbound, candidate.target)
    }
  }

  return nextGroups.map((outbounds) =>
    isIcmptest
      ? {
          outbounds: [],
          candidates: outbounds.map((outbound) => ({
            outbound,
            target: targetsByOutbound.get(outbound) ?? "",
          })),
        }
      : { outbounds, candidates: [] }
  )
}

function moveGroup(groups: string[][], fromIndex: number, toIndex: number) {
  const next = [...groups]
  const [moved] = next.splice(fromIndex, 1)
  next.splice(toIndex, 0, moved)
  return next
}

function getUnavailableOutbounds(groups: string[][], currentIndex: number) {
  return groups
    .filter((_, index) => index !== currentIndex)
    .flatMap((group) => group)
}

function getNextAvailableOutbounds(options: string[], groups: string[][]) {
  const used = new Set(groups.flatMap((group) => group))
  const next = options.find((option) => !used.has(option))
  return next ? [next] : []
}

function getInterfaceOutboundSearchText(
  tag: string,
  interfaceName: string | undefined,
  runtimeInterfaceByName: Map<string, RuntimeInterfaceInventoryEntry>
) {
  const runtimeInterface = interfaceName
    ? runtimeInterfaceByName.get(interfaceName)
    : undefined

  return [tag, interfaceName, getInterfaceSearchText(runtimeInterface)]
    .filter(Boolean)
    .join(" ")
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
  if (value === "default") {
    return t("pages.outboundUpsert.strictEnforcement.default")
  }

  if (value === "enabled") {
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
    if (outbound.type !== "urltest" && outbound.type !== "icmptest") {
      continue
    }

    for (const group of outbound.outbound_groups ?? []) {
      const referencedTags =
        group.outbounds ??
        (group.candidates ?? []).map((candidate) => candidate.outbound)
      for (const referencedTag of referencedTags) {
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

function resolveOutboundFieldPath(
  path: string,
  tag: string
): string | undefined {
  const normalizedTag = tag.trim()
  if (path === "outbounds") {
    return OUTBOUND_FIELD_NAMES.tag
  }

  if (!normalizedTag) {
    return undefined
  }

  const prefix = `outbounds.${normalizedTag}`
  if (path === prefix || path === `${prefix}.tag`) {
    return OUTBOUND_FIELD_NAMES.tag
  }

  if (path === `${prefix}.type`) {
    return OUTBOUND_FIELD_NAMES.type
  }

  if (path === `${prefix}.interface`) {
    return OUTBOUND_FIELD_NAMES.interfaceName
  }

  if (path === `${prefix}.gateway`) {
    return OUTBOUND_FIELD_NAMES.gateway
  }

  if (path === `${prefix}.table`) {
    return OUTBOUND_FIELD_NAMES.table
  }

  if (path === `${prefix}.gateway6`) {
    return OUTBOUND_FIELD_NAMES.gateway6
  }

  if (
    path === `${prefix}.outbound_groups` ||
    new RegExp(
      `^${prefix.replaceAll(".", "\\.")}\\.outbound_groups(?:\\[\\d+\\])?(?:\\.outbounds)?$`
    ).test(path)
  ) {
    return OUTBOUND_FIELD_NAMES.outboundGroups
  }

  if (path === `${prefix}.url`) {
    return OUTBOUND_FIELD_NAMES.probeUrl
  }

  if (path === `${prefix}.interval_ms`) {
    return OUTBOUND_FIELD_NAMES.interval
  }

  if (path === `${prefix}.tolerance_ms`) {
    return OUTBOUND_FIELD_NAMES.tolerance
  }

  if (path === `${prefix}.count`) return OUTBOUND_FIELD_NAMES.count
  if (path === `${prefix}.max_failed`) return OUTBOUND_FIELD_NAMES.maxFailed
  if (path === `${prefix}.packet_interval_ms`)
    return OUTBOUND_FIELD_NAMES.packetInterval
  if (path === `${prefix}.probe_timeout_ms`)
    return OUTBOUND_FIELD_NAMES.probeTimeout
  if (path === `${prefix}.max_rtt_ms`) return OUTBOUND_FIELD_NAMES.maxRtt

  const targetsPrefix = `${prefix}.outbound_groups`
  if (path.startsWith(targetsPrefix)) {
    const fieldPath = path.replace(
      targetsPrefix,
      OUTBOUND_FIELD_NAMES.outboundGroups
    )
    if (fieldPath.endsWith(".target")) {
      return fieldPath
    }

    const candidatesPathIndex = fieldPath.indexOf(".candidates")
    return candidatesPathIndex === -1
      ? fieldPath
      : fieldPath.slice(0, candidatesPathIndex)
  }

  if (path === `${prefix}.retry.attempts`) {
    return OUTBOUND_FIELD_NAMES.retryAttempts
  }

  if (path === `${prefix}.retry.interval_ms`) {
    return OUTBOUND_FIELD_NAMES.retryInterval
  }

  if (path === `${prefix}.circuit_breaker.failure_threshold`) {
    return OUTBOUND_FIELD_NAMES.circuitBreakerFailures
  }

  if (path === `${prefix}.circuit_breaker.success_threshold`) {
    return OUTBOUND_FIELD_NAMES.circuitBreakerSuccesses
  }

  if (path === `${prefix}.circuit_breaker.timeout_ms`) {
    return OUTBOUND_FIELD_NAMES.circuitBreakerTimeout
  }

  if (path === `${prefix}.circuit_breaker.half_open_max_requests`) {
    return OUTBOUND_FIELD_NAMES.circuitBreakerHalfOpen
  }

  if (path === `${prefix}.strict_enforcement`) {
    return OUTBOUND_FIELD_NAMES.strictEnforcement
  }

  return undefined
}
