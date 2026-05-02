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
  outbounds: string[][]
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

const OUTBOUND_FIELD_NAMES = {
  tag: "tag",
  type: "type",
  interfaceName: "interfaceName",
  gateway: "gateway",
  gateway6: "gateway6",
  table: "table",
  outbounds: "outbounds",
  probeUrl: "probeUrl",
  interval: "interval",
  tolerance: "tolerance",
  retryAttempts: "retryAttempts",
  retryInterval: "retryInterval",
  circuitBreakerFailures: "circuitBreakerFailures",
  circuitBreakerSuccesses: "circuitBreakerSuccesses",
  circuitBreakerTimeout: "circuitBreakerTimeout",
  circuitBreakerHalfOpen: "circuitBreakerHalfOpen",
  strictEnforcement: "strictEnforcement",
} as const

type OutboundFieldName =
  (typeof OUTBOUND_FIELD_NAMES)[keyof typeof OUTBOUND_FIELD_NAMES]

const sampleNewOutbound: OutboundDraft = {
  tag: "",
  type: "interface",
  interfaceName: "",
  gateway: "",
  gateway6: "",
  table: "",
  outbounds: [[]],
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
  const interfaceOutboundOptions = existingOutbounds
    .filter((item) => item.type === "interface" && item.tag !== draft.tag)
    .map((item) => item.tag)

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
          return {
            fields: {
              [OUTBOUND_FIELD_NAMES.tag]: duplicateTagError,
            },
          }
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
          return {
            form: urltestReferencesError,
            fields: {},
          }
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
          const result = splitFormApiErrors({
            error: error as ApiError,
            fieldNames: Object.values(OUTBOUND_FIELD_NAMES),
            resolvePath: (path) =>
              resolveOutboundFieldPath(path, payload.tag || draft.tag),
          })

          setFormServerErrors(form, {
            form: result.formError ?? undefined,
            fields: result.fieldErrors,
            unmapped: result.unmappedErrors,
          })

          return {
            form: result.formError ?? undefined,
            fields: result.fieldErrors,
          }
        }
      },
    },
  })

  const postConfigMutation = usePostConfigMutation()

  const outboundType = useStore(form.store, (state) => state.values.type)
  const apiErrorMessage = useStore(
    form.store,
    (state) =>
      ((state.errorMap.onServer as { form?: string } | undefined)?.form ?? null)
  )
  const unmappedServerErrors = useStore(
    form.store,
    (state) =>
      ((state.errorMap.onServer as { unmapped?: { path: string; message: string }[] } | undefined)
        ?.unmapped ?? [])
  )

  const isInterface = outboundType === "interface"
  const isTable = outboundType === "table"
  const isBlackhole = outboundType === "blackhole"
  const isIgnore = outboundType === "ignore"
  const isUrltest = outboundType === "urltest"
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
                      label: t(`pages.outboundUpsert.fields.typeOptions.${type}`),
                    }))}
                    onValueChange={(value) =>
                      field.handleChange((value as Outbound["type"]) ?? draft.type)
                    }
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
                            {t(`pages.outboundUpsert.fields.typeOptions.${option}`)}
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
          <div className="grid gap-4 md:grid-cols-2">
            <form.Field name={OUTBOUND_FIELD_NAMES.interfaceName}>
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)
                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor={interfaceId}>
                      {t("pages.outboundUpsert.interface.interface")}
                    </FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id={interfaceId}
                        onBlur={field.handleBlur}
                        onChange={(event) => field.handleChange(event.target.value)}
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t("pages.outboundUpsert.interface.interfaceHint")}
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
                        onChange={(event) => field.handleChange(event.target.value)}
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t("pages.outboundUpsert.interface.gatewayHint")}
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
                        onChange={(event) => field.handleChange(event.target.value)}
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t("pages.outboundUpsert.interface.gateway6Hint")}
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
                      onChange={(event) => field.handleChange(event.target.value)}
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

      {isUrltest ? (
        <form.Field name={OUTBOUND_FIELD_NAMES.outbounds}>
          {(field) => {
            const error = getFirstFieldError(field.state.meta.errors)
            const groups = normalizeOutboundGroups(field.state.value)
            return (
              <SectionCard
                description={t("pages.outboundUpsert.urltest.groupsDescription")}
                title={t("pages.outboundUpsert.urltest.groupsTitle")}
              >
                <div className="space-y-4">
                  {groups.map((group, index) => (
                    <OrderedGroupCard
                      canMoveDown={index !== groups.length - 1}
                      canMoveUp={index !== 0}
                      canRemove={groups.length !== 1}
                      description={t(
                        "pages.outboundUpsert.urltest.groupDescription",
                        { index: index + 1 }
                      )}
                      key={`${index}-${group.join(",")}`}
                      onMoveDown={() =>
                        field.handleChange(moveGroup(groups, index, index + 1))
                      }
                      onMoveUp={() =>
                        field.handleChange(moveGroup(groups, index, index - 1))
                      }
                      onRemove={() =>
                        field.handleChange(
                          groups.length === 1
                            ? groups
                            : normalizeOutboundGroups(
                                groups.filter((_, currentIndex) => currentIndex !== index)
                              )
                        )
                      }
                      title={t("pages.outboundUpsert.urltest.groupTitle", {
                        index: index + 1,
                      })}
                    >
                      <Field invalid={Boolean(error)}>
                        <FieldLabel>
                          {t("pages.outboundUpsert.urltest.interfaceOutbounds")}
                        </FieldLabel>
                        <FieldContent>
                          {interfaceOutboundOptions.length ? (
                            <MultiSelectList
                              error={error}
                              name={OUTBOUND_FIELD_NAMES.outbounds}
                              addLabel={t("pages.outboundUpsert.urltest.addOutbound")}
                              emptyMessage={t(
                                "pages.outboundUpsert.urltest.noInterfaceOutbounds"
                              )}
                              groupLabel={t(
                                "pages.outboundUpsert.urltest.interfaceOutbounds"
                              )}
                              onChange={(nextOutbounds) =>
                                field.handleChange(
                                  groups.map((item, itemIndex) =>
                                    itemIndex === index ? nextOutbounds : item
                                  )
                                )
                              }
                              options={interfaceOutboundOptions}
                              unavailable={getUnavailableOutbounds(groups, index)}
                              value={group}
                            />
                          ) : (
                            <div className="rounded-lg border border-border p-3 text-sm text-muted-foreground md:text-xs">
                              {t(
                                "pages.outboundUpsert.urltest.addInterfaceOutboundsFirst"
                              )}
                            </div>
                          )}
                          {interfaceOutboundOptions.length ? (
                            <FieldHint error={error ?? null} />
                          ) : null}
                        </FieldContent>
                      </Field>
                    </OrderedGroupCard>
                  ))}
                  <div className="flex justify-start">
                    <Button
                      onClick={() =>
                        field.handleChange([
                          ...groups,
                          getNextAvailableOutbounds(interfaceOutboundOptions, groups),
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
                        onChange={(event) => field.handleChange(event.target.value)}
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t("pages.outboundUpsert.urltest.probeUrlHint")}
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
                        onChange={(event) => field.handleChange(event.target.value)}
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t("pages.outboundUpsert.urltest.intervalHint")}
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
                        onChange={(event) => field.handleChange(event.target.value)}
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t("pages.outboundUpsert.urltest.toleranceHint")}
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
                        onChange={(event) => field.handleChange(event.target.value)}
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t("pages.outboundUpsert.urltest.retryAttemptsHint")}
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
                        onChange={(event) => field.handleChange(event.target.value)}
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

      {isUrltest ? (
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
                        onChange={(event) => field.handleChange(event.target.value)}
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
                        onChange={(event) => field.handleChange(event.target.value)}
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
                        onChange={(event) => field.handleChange(event.target.value)}
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
                        onChange={(event) => field.handleChange(event.target.value)}
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
                    description={t("pages.outboundUpsert.strictEnforcement.hint")}
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

function getOutboundTagError(
  value: string,
  outbounds: Outbound[],
  existingTag: string | undefined,
  t: (key: string, options?: Record<string, unknown>) => string
) {
  return getTagNameValidationError(value, {
    requiredError: t("pages.outboundUpsert.validation.tagRequired"),
    invalidError: t("common.validation.tagNamePattern"),
    duplicateError: validateTagUniqueness(outbounds, value.trim(), existingTag, t),
  })
}

function mapOutboundToDraft(outbound: Outbound): OutboundDraft {
  return {
    tag: outbound.tag,
    type: outbound.type,
    interfaceName: outbound.interface ?? "",
    gateway: outbound.gateway ?? "",
    gateway6: outbound.gateway6 ?? "",
    table: outbound.table?.toString() ?? "",
    outbounds:
      outbound.outbound_groups?.map((group) => [...group.outbounds]) ??
      sampleNewOutbound.outbounds,
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

function buildOutboundPayload(draft: OutboundDraft): Outbound {
  const tag = draft.tag.trim()

  if (draft.type === "interface") {
    return {
      type: "interface",
      tag,
      interface: draft.interfaceName.trim() || undefined,
      gateway: draft.gateway.trim() || undefined,
      gateway6: draft.gateway6.trim() || undefined,
      strict_enforcement: mapStrictEnforcementToBoolean(draft.strictEnforcement),
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
      outbound_groups: normalizeOutboundGroups(draft.outbounds).map((group) => ({
        outbounds: group,
      })),
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

  return groups.map((group) => group.map((value) => value.trim()).filter(Boolean))
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

function resolveOutboundFieldPath(
  path: string,
  tag: string
): OutboundFieldName | undefined {
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
    return OUTBOUND_FIELD_NAMES.outbounds
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
