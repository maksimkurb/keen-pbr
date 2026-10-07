import { type ReactNode, useId } from "react"
import { useTranslation } from "react-i18next"
import {
  Activity,
  Ban,
  EthernetPort,
  Info,
  RouteOff,
  Table2,
  Zap,
  type LucideIcon,
} from "lucide-react"

import { useQueryClient } from "@tanstack/react-query"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { Outbound } from "@/api/generated/model/outbound"
import { usePostConfigMutation } from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
import {
  useGetConfig,
  useGetRuntimeInterfaces,
  useGetRuntimeOutbounds,
} from "@/api/queries"
import {
  findOutboundByTag,
  selectConfig,
  selectOutbounds,
} from "@/api/selectors"
import {
  AdvancedSection,
  ChoiceCard,
  FormSection,
  InfoHint,
  SegmentedControl,
} from "@/components/outbound-form/form-parts"
import {
  GroupLadder,
  type LadderCandidate,
} from "@/components/outbound-form/group-ladder"
import {
  Field,
  FieldContent,
  FieldDescription,
  FieldGroup,
  FieldHint,
  FieldLabel,
} from "@/components/shared/field"
import { InterfacePicker } from "@/components/shared/interface-picker"
import { ServerValidationAlert } from "@/components/shared/server-validation-alert"
import { UpsertPage } from "@/components/shared/upsert-page"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { RadioGroup } from "@/components/ui/radio-group"
import {
  bindInput,
  type FieldBinding,
  type Path,
  useDraftForm,
} from "@/lib/draft-form"
import { getTagNameValidationError } from "@/lib/tag-name-validation"
import {
  PLATFORM_DEVELOPMENT,
  PLATFORM_GENERIC,
  PLATFORM_OPENWRT,
} from "@/lib/platform"
import {
  buildOutboundPayload,
  getKillSwitchChoice,
  getKillSwitchFields,
  getMemberSharePercent,
  getOutboundGroupTags,
  type KillSwitchChoice,
  mapOutboundToDraft,
  normalizeOutboundGroups,
  type OutboundDraft,
  resolveOutboundFieldPath,
  sampleNewOutbound,
  synchronizeOutboundGroups,
  TEST_GROUP_DEFAULTS,
} from "@/pages/outbound-upsert-utils"
import { getOutboundGroupMembers } from "@/pages/outbounds-utils"

// Load balancing exists only where the daemon compiles it (not on Keenetic).
const BALANCE_SUPPORTED =
  import.meta.env.VITE_KEEN_PBR_PLATFORM === PLATFORM_GENERIC ||
  import.meta.env.VITE_KEEN_PBR_PLATFORM === PLATFORM_OPENWRT ||
  import.meta.env.VITE_KEEN_PBR_PLATFORM === PLATFORM_DEVELOPMENT

type TranslateFn = (key: string, options?: Record<string, unknown>) => string

const outboundTypes: Array<{ type: Outbound["type"]; icon: LucideIcon }> = [
  { type: "interface", icon: EthernetPort },
  { type: "table", icon: Table2 },
  { type: "urltest", icon: Zap },
  { type: "icmptest", icon: Activity },
  { type: "blackhole", icon: Ban },
  { type: "ignore", icon: RouteOff },
]

const TEST_GROUP_TUNED_FIELDS = [
  "interval",
  "tolerance",
  "probeTimeout",
  "circuitBreakerTimeout",
] as const

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
  const title =
    mode === "create"
      ? t("pages.outboundUpsert.createTitle")
      : t("pages.outboundUpsert.editTitle")

  if (!loadedConfig) {
    return (
      <UpsertPage
        cardDescription={t("pages.outboundUpsert.cardDescription")}
        cardTitle={title}
        description={t("pages.outboundUpsert.description")}
        title={title}
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

  const existing =
    mode === "edit" && outboundId
      ? findOutboundByTag(loadedConfig, outboundId)
      : undefined

  if (mode === "edit" && !existing) {
    return (
      <UpsertPage
        cardDescription={t("pages.outboundUpsert.missing.cardDescription")}
        cardTitle={t("pages.outboundUpsert.missing.cardTitle")}
        description={t("pages.outboundUpsert.missing.description")}
        title={title}
      >
        <div className="flex justify-end">
          <Button onClick={() => navigate("/outbounds")} variant="outline">
            {t("pages.outboundUpsert.missing.back")}
          </Button>
        </div>
      </UpsertPage>
    )
  }

  const draft = existing ? mapOutboundToDraft(existing) : sampleNewOutbound

  return (
    <UpsertPage
      cardDescription={t("pages.outboundUpsert.cardDescription")}
      cardTitle={
        mode === "create"
          ? title
          : t("pages.outboundUpsert.editCardTitle", { tag: draft.tag })
      }
      description={t("pages.outboundUpsert.description")}
      title={title}
    >
      <OutboundForm
        draft={draft}
        key={`${mode}:${outboundId ?? "new"}`}
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
  const postConfigMutation = usePostConfigMutation()

  const runtimeInterfacesQuery = useGetRuntimeInterfaces()
  const runtimeInterfaces =
    runtimeInterfacesQuery.data?.status === 200
      ? runtimeInterfacesQuery.data.data.interfaces
      : []
  const runtimeOutboundsQuery = useGetRuntimeOutbounds()
  const runtimeOutbounds =
    runtimeOutboundsQuery.data?.status === 200
      ? runtimeOutboundsQuery.data.data.outbounds
      : []
  const runtimeByTag = new Map(runtimeOutbounds.map((item) => [item.tag, item]))
  const memberRuntime = new Map(
    (outboundId ? runtimeByTag.get(outboundId)?.interfaces : undefined)?.map(
      (item) => [item.outbound_tag, item]
    ) ?? []
  )

  const candidates: LadderCandidate[] = existingOutbounds
    .filter(
      (item) =>
        (item.type === "interface" || item.type === "table") &&
        item.tag !== draft.tag
    )
    .map((item) => ({
      tag: item.tag,
      detail:
        item.type === "interface"
          ? item.interface
          : t("overview.routing.tableLabel", { value: item.table }),
      runtime: runtimeByTag.get(item.tag),
    }))

  const existingTag = mode === "edit" ? outboundId : undefined
  const form = useDraftForm<OutboundDraft>(draft, {
    // The only client check: the daemon cannot know the name rules of the
    // loaded list. Everything else is validated by the daemon on save.
    validate: (value) => {
      const tagError = getOutboundTagError(
        value.tag,
        existingOutbounds,
        existingTag,
        t
      )
      return tagError ? { tag: tagError } : {}
    },
  })
  const { values } = form

  const save = async (value: OutboundDraft) => {
    const payload = buildOutboundPayload(value)
    const nextOutbounds =
      mode === "create"
        ? [...existingOutbounds, payload]
        : existingOutbounds.map((outbound) =>
            outbound.tag === outboundId ? payload : outbound
          )

    const referencesError = validateUrltestGroupReferences(nextOutbounds, t)
    if (referencesError) {
      form.setServerErrors({ form: referencesError })
      return
    }

    try {
      await postConfigMutation.mutateAsync({
        data: {
          ...loadedConfig,
          outbounds: nextOutbounds,
        } satisfies ConfigObject,
      })
    } catch (error) {
      form.setApiError(error as ApiError, (path) =>
        resolveOutboundFieldPath(path, payload.tag || draft.tag)
      )
      return
    }
    await Promise.all([
      queryClient.invalidateQueries({ queryKey: queryKeys.config() }),
      queryClient.invalidateQueries({ queryKey: queryKeys.healthService() }),
      queryClient.invalidateQueries({ queryKey: queryKeys.healthRouting() }),
    ])
    navigate("/outbounds")
  }

  const steps = getOutboundGroupTags(values.outboundGroups)
  const apiErrorMessage = form.errors.form
  const serverFieldErrors = form.errors.fields
  const unmappedServerErrors = form.errors.unmapped
  // Members are validated by the daemon only; its errors reach the inputs by
  // path (a weight error keeps the weight visible in priority mode).
  const sectionErrorLabel = (names: ReadonlyArray<Path<OutboundDraft>>) =>
    names.some((name) => form.errorFor(name))
      ? t("pages.outboundUpsert.advanced.hasError")
      : null
  const hasWeightServerError = Object.keys(serverFieldErrors).some((name) =>
    /^outboundGroups\[\d+\]\.members\[\d+\]\.weight$/.test(name)
  )

  const outboundType = values.type
  const isUrltest = outboundType === "urltest"
  const isIcmptest = outboundType === "icmptest"
  const isTestGroup = isUrltest || isIcmptest
  const groupDefaults = isIcmptest
    ? TEST_GROUP_DEFAULTS.icmptest
    : TEST_GROUP_DEFAULTS.urltest
  const globalAction =
    loadedConfig.daemon?.strict_enforcement_action ?? "unreachable"
  const globalKillSwitch: KillSwitchChoice = loadedConfig.daemon
    ?.strict_enforcement
    ? globalAction === "blackhole"
      ? "drop"
      : "reject"
    : "off"
  const killSwitch = getKillSwitchChoice(
    values.strictEnforcement,
    values.strictEnforcementAction,
    globalAction
  )
  const killSwitchError =
    form.errorFor("strictEnforcement") ??
    form.errorFor("strictEnforcementAction")
  const changedLabel = (changed: boolean) =>
    changed
      ? t("pages.outboundUpsert.advanced.changed")
      : t("pages.outboundUpsert.advanced.default")

  const changeType = (nextType: Outbound["type"]) =>
    form.setValues((previous) => {
      const previousType = previous.type
      if (nextType === previousType) {
        return previous
      }
      const next = { ...previous, type: nextType }
      const wasGroup = previousType === "urltest" || previousType === "icmptest"
      if (nextType === "urltest" || nextType === "icmptest") {
        const previousDefaults =
          previousType === "icmptest"
            ? TEST_GROUP_DEFAULTS.icmptest
            : TEST_GROUP_DEFAULTS.urltest
        const nextDefaults =
          nextType === "icmptest"
            ? TEST_GROUP_DEFAULTS.icmptest
            : TEST_GROUP_DEFAULTS.urltest
        // Swap per-type defaults, but keep values the user has tuned.
        for (const name of TEST_GROUP_TUNED_FIELDS) {
          if (!wasGroup || previous[name] === previousDefaults[name]) {
            next[name] = nextDefaults[name]
          }
        }
        if (wasGroup) {
          // Keep the chosen members when switching between urltest and ICMP.
          next.outboundGroups = synchronizeOutboundGroups(
            previous.outboundGroups,
            getOutboundGroupTags(previous.outboundGroups)
          )
        }
      }
      return next
    })

  return (
    <form className="space-y-6" onSubmit={form.onSubmit(save)}>
      {apiErrorMessage ? (
        <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription className="whitespace-pre-wrap">
            {apiErrorMessage}
          </AlertDescription>
        </Alert>
      ) : null}

      <FieldGroup>
        <TextField
          className="max-w-md"
          field={form.field("tag")}
          hint={t("pages.outboundUpsert.fields.tagHint")}
          label={t("pages.outboundUpsert.fields.tag")}
          readOnly={mode === "edit"}
        />

        <Field invalid={Boolean(form.errorFor("type"))}>
          <FieldLabel>{t("pages.outboundUpsert.fields.type")}</FieldLabel>
          <FieldContent>
            <RadioGroup
              aria-label={t("pages.outboundUpsert.fields.type")}
              className="grid grid-cols-2 gap-3 sm:grid-cols-3 xl:grid-cols-6"
              onValueChange={(value) => changeType(value as Outbound["type"])}
              value={values.type}
            >
              {outboundTypes.map(({ type, icon: Icon }) => (
                <ChoiceCard
                  className="min-h-28 flex-col items-center justify-center gap-2 px-2 pt-6 pb-3 text-center"
                  key={type}
                  value={type}
                >
                  <Icon className="size-7 text-primary" />
                  <span className="text-sm leading-tight font-medium">
                    {t(`pages.outboundUpsert.fields.typeOptions.${type}`)}
                  </span>
                  <InfoHint
                    className="absolute top-2.5 right-2.5"
                    label={t("pages.outboundUpsert.fields.aboutType")}
                    text={t(`pages.outboundUpsert.typeHints.${type}`)}
                  />
                </ChoiceCard>
              ))}
            </RadioGroup>
            <FieldHint error={form.errorFor("type")} />
          </FieldContent>
        </Field>
      </FieldGroup>

      {outboundType === "interface" ? (
        <FieldGroup>
          <Field invalid={Boolean(form.errorFor("interfaceName"))}>
            <FieldLabel>
              {t("pages.outboundUpsert.interface.interface")}
            </FieldLabel>
            <FieldContent>
              <InterfacePicker
                allowCustomOption
                interfaces={runtimeInterfaces}
                invalid={Boolean(form.errorFor("interfaceName"))}
                onChange={(value) => form.setValue("interfaceName", value)}
                onSelect={(value) => form.setValue("interfaceName", value)}
                placeholder={t(
                  "pages.outboundUpsert.interface.interfacePlaceholder"
                )}
                renderSelectedInline
                showDetails={false}
                value={values.interfaceName}
              />
              <FieldHint
                description={t("pages.outboundUpsert.interface.interfaceHint")}
                error={form.errorFor("interfaceName")}
              />
            </FieldContent>
          </Field>

          <div className="space-y-1.5">
            <div className="grid gap-4 md:grid-cols-2">
              <TextField
                field={form.field("gateway")}
                label={t("pages.outboundUpsert.interface.gateway")}
                placeholder={t(
                  "pages.outboundUpsert.interface.gatewayPlaceholder"
                )}
              />
              <TextField
                field={form.field("gateway6")}
                label={t("pages.outboundUpsert.interface.gateway6")}
                placeholder={t(
                  "pages.outboundUpsert.interface.gateway6Placeholder"
                )}
              />
            </div>
            <FieldDescription>
              {t("pages.outboundUpsert.interface.gatewaysHint")}
            </FieldDescription>
          </div>

          <Field invalid={Boolean(killSwitchError)}>
            <FieldLabel>
              {t("pages.outboundUpsert.killSwitch.title")}
            </FieldLabel>
            <FieldDescription className="-mt-1.5">
              {t("pages.outboundUpsert.killSwitch.description")}
            </FieldDescription>
            <RadioGroup
              aria-label={t("pages.outboundUpsert.killSwitch.title")}
              className="grid gap-3 md:grid-cols-2"
              onValueChange={(value) =>
                // Two config fields change together.
                form.setValues((previous) => ({
                  ...previous,
                  ...getKillSwitchFields(value as KillSwitchChoice),
                }))
              }
              value={killSwitch}
            >
              {(["inherit", "off", "reject", "drop"] as const).map((choice) => (
                <ChoiceCard
                  className="flex-col items-start gap-0.5 py-2.5 pr-3 pl-9 text-left"
                  key={choice}
                  value={choice}
                >
                  <span className="text-sm font-medium">
                    {t(
                      `pages.outboundUpsert.killSwitch.options.${choice}.title`
                    )}
                  </span>
                  <span className="text-xs text-muted-foreground">
                    {choice === "inherit"
                      ? t("pages.outboundUpsert.killSwitch.inheritNow", {
                          value: t(
                            `pages.outboundUpsert.killSwitch.badge.${globalKillSwitch}`
                          ),
                        })
                      : t(
                          `pages.outboundUpsert.killSwitch.options.${choice}.description`
                        )}
                  </span>
                </ChoiceCard>
              ))}
            </RadioGroup>
            <FieldHint error={killSwitchError} />
          </Field>
        </FieldGroup>
      ) : null}

      {outboundType === "table" ? (
        <TextField
          className="max-w-64"
          field={form.field("table")}
          hint={t("pages.outboundUpsert.table.hint")}
          inputMode="numeric"
          label={t("pages.outboundUpsert.table.field")}
          placeholder="100"
        />
      ) : null}

      {outboundType === "blackhole" || outboundType === "ignore" ? (
        <div className="flex items-start gap-2.5 rounded-xl border border-dashed bg-muted/40 p-3.5 text-sm text-muted-foreground">
          <Info className="mt-0.5 size-4 shrink-0" />
          <p className="text-foreground">
            {t(`pages.outboundUpsert.typeHints.${outboundType}`)}
          </p>
        </div>
      ) : null}

      {isTestGroup ? (
        <>
          {
            // Without load balancing there is nothing to choose, unless a
            // balance config has to be switched back.
            !BALANCE_SUPPORTED && values.strategy !== "balance" ? null : (
              <Field>
                <FieldLabel>
                  {t("pages.outboundUpsert.strategy.label")}
                </FieldLabel>
                <FieldContent>
                  <RadioGroup
                    aria-label={t("pages.outboundUpsert.strategy.label")}
                    className="grid max-w-2xl gap-3 sm:grid-cols-2"
                    onValueChange={(value) =>
                      form.setValue(
                        "strategy",
                        value as NonNullable<Outbound["strategy"]>
                      )
                    }
                    value={values.strategy}
                  >
                    {(["priority", "balance"] as const).map((strategy) => (
                      <ChoiceCard
                        className="flex-col items-start gap-0.5 py-3 pr-3.5 pl-9 text-left"
                        disabled={strategy === "balance" && !BALANCE_SUPPORTED}
                        key={strategy}
                        value={strategy}
                      >
                        <span className="text-sm font-medium">
                          {t(
                            `pages.outboundUpsert.strategy.cards.${strategy}.title`
                          )}
                        </span>
                        <span className="text-xs text-muted-foreground">
                          {t(
                            `pages.outboundUpsert.strategy.cards.${strategy}.description`
                          )}
                        </span>
                      </ChoiceCard>
                    ))}
                  </RadioGroup>
                </FieldContent>
              </Field>
            )
          }

          <FormSection>
            <div className="space-y-2">
              {candidates.length === 0 ? (
                <div className="rounded-lg border border-dashed p-3 text-sm text-muted-foreground">
                  {t("pages.outboundUpsert.urltest.addInterfaceOutboundsFirst")}
                </div>
              ) : null}
              <GroupLadder
                candidates={candidates}
                memberRuntime={memberRuntime}
                // Structural edits replace the whole array, which also drops
                // the errors of every row below it (rows shift on reorder).
                onChange={(nextSteps) =>
                  form.setValue(
                    "outboundGroups",
                    synchronizeOutboundGroups(
                      values.outboundGroups,
                      normalizeOutboundGroups(nextSteps)
                    )
                  )
                }
                renderTarget={
                  isIcmptest
                    ? (stepIndex, memberIndex) => (
                        <PingTargetInput
                          field={form.field(
                            `outboundGroups[${stepIndex}].members[${memberIndex}].target`
                          )}
                        />
                      )
                    : undefined
                }
                renderWeight={(stepIndex, memberIndex) => (
                  <WeightInput
                    field={form.field(
                      `outboundGroups[${stepIndex}].members[${memberIndex}].weight`
                    )}
                    share={getMemberSharePercent(
                      values.outboundGroups[stepIndex],
                      memberIndex
                    )}
                  />
                )}
                showWeights={
                  values.strategy === "balance" || hasWeightServerError
                }
                stepErrors={steps.map((_, index) =>
                  form.errorFor(`outboundGroups[${index}]`)
                )}
                steps={steps}
                strategy={values.strategy}
              />
              {form.errorFor("outboundGroups") ? (
                <p className="text-sm text-destructive">
                  {form.errorFor("outboundGroups")}
                </p>
              ) : null}
            </div>
          </FormSection>

          {values.strategy === "priority" ? (
            <ChoiceField
              error={form.errorFor("conntrackOnSwitch")}
              hint={t("pages.outboundUpsert.conntrack.hint")}
              label={t("pages.outboundUpsert.conntrack.label")}
            >
              <SegmentedControl
                aria-label={t("pages.outboundUpsert.conntrack.label")}
                onChange={(value) => form.setValue("conntrackOnSwitch", value)}
                options={(["preserve", "delete"] as const).map((value) => ({
                  value,
                  label: t(`pages.outboundUpsert.conntrack.${value}`),
                }))}
                value={values.conntrackOnSwitch}
              />
            </ChoiceField>
          ) : null}

          {isUrltest ? (
            <AdvancedSection
              badge={changedLabel(
                isChanged(values, groupDefaults, [
                  "probeUrl",
                  "interval",
                  "probeTimeout",
                  "tolerance",
                  "retryAttempts",
                  "retryInterval",
                ])
              )}
              changed={isChanged(values, groupDefaults, [
                "probeUrl",
                "interval",
                "probeTimeout",
                "tolerance",
                "retryAttempts",
                "retryInterval",
              ])}
              errorLabel={sectionErrorLabel([
                "probeUrl",
                "interval",
                "probeTimeout",
                "tolerance",
                "retryAttempts",
                "retryInterval",
              ])}
              title={t("pages.outboundUpsert.advanced.probesTitle")}
            >
              <div className="grid gap-4 md:grid-cols-2">
                <TextField
                  className="md:col-span-2"
                  field={form.field("probeUrl")}
                  label={t("pages.outboundUpsert.urltest.probeUrl")}
                  tooltip={t("pages.outboundUpsert.urltest.probeUrlHint")}
                />
                {(
                  [
                    ["interval", "urltest.interval"],
                    ["probeTimeout", "urltest.probeTimeout"],
                    ["tolerance", "urltest.tolerance"],
                    ["retryAttempts", "urltest.retryAttempts"],
                    ["retryInterval", "urltest.retryInterval"],
                  ] as const
                ).map(([name, key]) => (
                  <TextField
                    field={form.field(name)}
                    inputMode="numeric"
                    key={name}
                    label={t(`pages.outboundUpsert.${key}`)}
                    tooltip={t(`pages.outboundUpsert.${key}Hint`)}
                  />
                ))}
              </div>
            </AdvancedSection>
          ) : null}

          {isIcmptest ? (
            <AdvancedSection
              badge={changedLabel(
                isChanged(values, groupDefaults, [
                  "count",
                  "maxFailed",
                  "packetInterval",
                  "probeTimeout",
                  "maxRtt",
                  "interval",
                  "tolerance",
                ])
              )}
              changed={isChanged(values, groupDefaults, [
                "count",
                "maxFailed",
                "packetInterval",
                "probeTimeout",
                "maxRtt",
                "interval",
                "tolerance",
              ])}
              errorLabel={sectionErrorLabel([
                "count",
                "maxFailed",
                "packetInterval",
                "probeTimeout",
                "maxRtt",
                "interval",
                "tolerance",
              ])}
              title={t("pages.outboundUpsert.icmptest.title")}
            >
              <div className="grid gap-4 md:grid-cols-2">
                {(
                  [
                    "count",
                    "maxFailed",
                    "packetInterval",
                    "probeTimeout",
                    "maxRtt",
                    "interval",
                    "tolerance",
                  ] as const
                ).map((name) => (
                  <TextField
                    field={form.field(name)}
                    inputMode="numeric"
                    key={name}
                    label={t(`pages.outboundUpsert.icmptest.${name}`)}
                    tooltip={t(`pages.outboundUpsert.icmptest.${name}Hint`)}
                  />
                ))}
              </div>
            </AdvancedSection>
          ) : null}

          <AdvancedSection
            badge={changedLabel(
              isChanged(values, groupDefaults, [
                "circuitBreakerFailures",
                "circuitBreakerSuccesses",
                "circuitBreakerTimeout",
                "circuitBreakerHalfOpen",
              ])
            )}
            changed={isChanged(values, groupDefaults, [
              "circuitBreakerFailures",
              "circuitBreakerSuccesses",
              "circuitBreakerTimeout",
              "circuitBreakerHalfOpen",
            ])}
            errorLabel={sectionErrorLabel([
              "circuitBreakerFailures",
              "circuitBreakerSuccesses",
              "circuitBreakerTimeout",
              "circuitBreakerHalfOpen",
            ])}
            title={t("pages.outboundUpsert.advanced.circuitBreakerTitle")}
          >
            <FieldDescription className="mb-4">
              {t("pages.outboundUpsert.circuitBreaker.description")}
            </FieldDescription>
            <div className="grid gap-4 md:grid-cols-2">
              {(
                [
                  ["circuitBreakerFailures", "failures"],
                  ["circuitBreakerSuccesses", "successes"],
                  ["circuitBreakerTimeout", "timeout"],
                  ["circuitBreakerHalfOpen", "halfOpen"],
                ] as const
              ).map(([name, key]) => (
                <TextField
                  field={form.field(name)}
                  inputMode="numeric"
                  key={name}
                  label={t(`pages.outboundUpsert.circuitBreaker.${key}`)}
                  tooltip={t(`pages.outboundUpsert.circuitBreaker.${key}Hint`)}
                />
              ))}
            </div>
          </AdvancedSection>
        </>
      ) : null}

      <ServerValidationAlert errors={unmappedServerErrors} />

      <div className="flex justify-end gap-3">
        <Button onClick={onCancel} size="xl" type="button" variant="outline">
          {t("common.cancel")}
        </Button>
        <Button
          disabled={form.isSubmitting || !form.isDirty}
          size="xl"
          type="submit"
        >
          {mode === "create"
            ? t("pages.outboundUpsert.actions.create")
            : t("pages.outboundUpsert.actions.save")}
        </Button>
      </div>
    </form>
  )
}

function TextField({
  field,
  label,
  hint,
  tooltip,
  placeholder,
  inputMode,
  readOnly,
  className,
}: {
  field: FieldBinding<string>
  label: string
  /** Shown under the input. */
  hint?: string
  /** Shown as an info icon next to the label. */
  tooltip?: string
  placeholder?: string
  inputMode?: "numeric"
  readOnly?: boolean
  className?: string
}) {
  const id = useId()
  return (
    <Field className={className} invalid={Boolean(field.error)}>
      <FieldLabel htmlFor={id}>
        {label}
        {tooltip ? <InfoHint label={label} text={tooltip} /> : null}
      </FieldLabel>
      <FieldContent>
        <Input
          {...bindInput(field)}
          id={id}
          inputMode={inputMode}
          placeholder={placeholder}
          readOnly={readOnly}
        />
        <FieldHint description={hint} error={field.error} />
      </FieldContent>
    </Field>
  )
}

function ChoiceField({
  label,
  hint,
  error,
  children,
}: {
  label: string
  hint: string
  error: string | null
  children: ReactNode
}) {
  return (
    <Field invalid={Boolean(error)}>
      <FieldLabel>
        {label}
        <InfoHint label={label} text={hint} />
      </FieldLabel>
      <FieldContent>
        {children}
        <FieldHint error={error ?? null} />
      </FieldContent>
    </Field>
  )
}

function WeightInput({
  field,
  share,
}: {
  field: FieldBinding<string>
  share: number
}) {
  const { t } = useTranslation()
  const error = field.error
  const errorId = useId()
  const label = t("pages.outboundUpsert.ladder.weight")
  return (
    <InlineFieldError error={error} id={errorId}>
      <div className="flex items-center gap-2 text-xs text-muted-foreground">
        <label className="flex items-center gap-2">
          {label}
          <Input
            aria-describedby={error ? errorId : undefined}
            aria-invalid={Boolean(error)}
            className="h-7 w-14 text-xs tabular-nums md:text-xs"
            inputMode="numeric"
            name={field.name}
            // Digits only, so the request always carries a number; the
            // daemon checks the 1..100 range.
            onChange={(event) =>
              field.onChange(event.target.value.replace(/\D/g, ""))
            }
            placeholder="1"
            value={field.value ?? ""}
          />
        </label>
        <InfoHint
          label={label}
          text={t("pages.outboundUpsert.ladder.weightHint")}
        />
        <span
          className="w-10 text-right tabular-nums"
          title={t("pages.outboundUpsert.ladder.shareTitle")}
        >
          {error ? "—" : `≈${share}%`}
        </span>
      </div>
    </InlineFieldError>
  )
}

function PingTargetInput({ field }: { field: FieldBinding<string> }) {
  const { t } = useTranslation()
  const error = field.error
  const errorId = useId()
  return (
    <InlineFieldError error={error} id={errorId}>
      <label className="flex items-center gap-2 text-xs text-muted-foreground">
        {t("pages.outboundUpsert.ladder.pingTarget")}
        <Input
          aria-describedby={error ? errorId : undefined}
          className="h-7 w-40 font-mono text-xs md:text-xs"
          {...bindInput(field)}
          placeholder="1.1.1.1"
          title={t("pages.outboundUpsert.icmptest.targetHint")}
        />
      </label>
    </InlineFieldError>
  )
}

/** A compact inline control with its error message right below it. */
function InlineFieldError({
  error,
  id,
  children,
}: {
  error: string | null
  id: string
  children: ReactNode
}) {
  return (
    <div className="flex flex-col items-end gap-0.5">
      {children}
      {error ? (
        <p className="text-[11px] text-destructive" id={id} role="alert">
          {error}
        </p>
      ) : null}
    </div>
  )
}

function isChanged(
  values: OutboundDraft,
  groupDefaults: Partial<Record<keyof OutboundDraft, string>>,
  names: ReadonlyArray<keyof OutboundDraft>
) {
  return names.some(
    (name) => values[name] !== (groupDefaults[name] ?? sampleNewOutbound[name])
  )
}

function getOutboundTagError(
  value: string,
  outbounds: Outbound[],
  existingTag: string | undefined,
  t: TranslateFn
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

function validateTagUniqueness(
  outbounds: Outbound[],
  tag: string,
  existingTag: string | undefined,
  t: TranslateFn
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
  t: TranslateFn
): string | null {
  const tags = new Set(outbounds.map((outbound) => outbound.tag))

  for (const outbound of outbounds) {
    if (outbound.type !== "urltest" && outbound.type !== "icmptest") {
      continue
    }

    for (const group of outbound.outbound_groups ?? []) {
      const referencedTags = getOutboundGroupMembers(group).map(
        (member) => member.outbound
      )
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
