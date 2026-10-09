import { ChoiceButton } from "@/components/ui/choice-button"
import { type ReactNode, useId } from "react"
import { Trans, useTranslation } from "react-i18next"
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
  getIn,
  setIn,
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
  "interval_ms",
  "tolerance_ms",
  "probe_timeout_ms",
  "circuit_breaker.timeout_ms",
] as const

// [draft path, i18n key suffix] of the fields in each advanced section.
const URLTEST_PROBE_FIELDS = [
  ["interval_ms", "urltest.interval"],
  ["probe_timeout_ms", "urltest.probeTimeout"],
  ["tolerance_ms", "urltest.tolerance"],
  ["retry.attempts", "urltest.retryAttempts"],
  ["retry.interval_ms", "urltest.retryInterval"],
] as const
const ICMPTEST_FIELDS = [
  ["count", "count"],
  ["max_failed", "maxFailed"],
  ["packet_interval_ms", "packetInterval"],
  ["probe_timeout_ms", "probeTimeout"],
  ["max_rtt_ms", "maxRtt"],
  ["interval_ms", "interval"],
  ["tolerance_ms", "tolerance"],
] as const
const CIRCUIT_BREAKER_FIELDS = [
  ["circuit_breaker.failure_threshold", "failures"],
  ["circuit_breaker.success_threshold", "successes"],
  ["circuit_breaker.timeout_ms", "timeout"],
  ["circuit_breaker.half_open_max_requests", "halfOpen"],
] as const

const sectionPaths = (
  fields: ReadonlyArray<readonly [Path<OutboundDraft>, string]>
) => fields.map(([path]) => path)

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
      title={
        mode === "edit" ? (
          <Trans
            i18nKey="pages.outboundUpsert.editCardTitle"
            values={{ tag: draft.tag }}
            components={{ entity: <span className="text-primary" /> }}
          />
        ) : (
          title
        )
      }
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
  // Index the outbound has in the array that is sent (new ones are appended).
  const outboundIndex =
    mode === "edit"
      ? existingOutbounds.findIndex((item) => item.tag === outboundId)
      : existingOutbounds.length
  const form = useDraftForm<OutboundDraft>(draft, {
    apiPrefix: `outbounds[${outboundIndex}]`,
    // Check the name and explicit type selection; the daemon validates type-specific settings.
    validate: (value) => {
      const tagError = getOutboundTagError(
        value.tag,
        existingOutbounds,
        existingTag,
        t
      )
      const errors: Record<string, string> = {}
      if (tagError) errors.tag = tagError
      if (!value.type) errors.type = t("common.validation.required")
      return errors
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
      form.setApiError(error as ApiError)
      return
    }
    await Promise.all([
      queryClient.invalidateQueries({ queryKey: queryKeys.config() }),
      queryClient.invalidateQueries({ queryKey: queryKeys.healthService() }),
      queryClient.invalidateQueries({ queryKey: queryKeys.healthRouting() }),
    ])
    navigate("/outbounds")
  }

  const steps = getOutboundGroupTags(values.outbound_groups)
  const apiErrorMessage = form.formError
  // Members are validated by the daemon only; its errors reach the inputs by
  // path (a weight error keeps the weight visible in priority mode).
  const sectionErrorLabel = (names: ReadonlyArray<Path<OutboundDraft>>) =>
    names.some((name) => form.errorFor(name))
      ? t("pages.outboundUpsert.advanced.hasError")
      : null
  const hasWeightServerError = form
    .errorPaths()
    .some((name) =>
      /^outbound_groups\[\d+\]\.members\[\d+\]\.weight$/.test(name)
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
    values.strict_enforcement,
    values.strict_enforcement_action,
    globalAction
  )
  const killSwitchError = form.errorFor("strict_enforcement", {
    // One kill-switch control owns both config fields.
    alsoClaims: ["strict_enforcement_action"],
  })
  const urltestPaths: Path<OutboundDraft>[] = [
    "url",
    ...sectionPaths(URLTEST_PROBE_FIELDS),
  ]
  const icmptestPaths = sectionPaths(ICMPTEST_FIELDS)
  const breakerPaths = sectionPaths(CIRCUIT_BREAKER_FIELDS)
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
      let next: OutboundDraft = { ...previous, type: nextType }
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
          if (!wasGroup || getIn(previous, name) === previousDefaults[name]) {
            next = setIn(next, name, nextDefaults[name])
          }
        }
        if (wasGroup) {
          // Keep the chosen members when switching between urltest and ICMP.
          next = {
            ...next,
            outbound_groups: synchronizeOutboundGroups(
              previous.outbound_groups,
              getOutboundGroupTags(previous.outbound_groups)
            ),
          }
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
          className="max-w-sm"
          // The tag control also shows errors addressed to the whole outbound.
          field={form.field("tag", { alsoClaims: [""] })}
          label={t("pages.outboundUpsert.fields.tag")}
          disabled={mode === "edit"}
        />

        <Field invalid={Boolean(form.errorFor("type"))}>
          <FieldLabel>{t("pages.outboundUpsert.fields.type")}</FieldLabel>
          <FieldContent>
            <RadioGroup
              aria-label={t("pages.outboundUpsert.fields.type")}
              value={values.type}
              onValueChange={(value) => changeType(value as Outbound["type"])}
              className="grid w-full max-w-3xl min-w-0 gap-3 sm:grid-cols-2 lg:grid-cols-3"
            >
              {outboundTypes.map(({ type, icon: Icon }) => (
                <ChoiceButton value={type} key={type}>
                  <Icon aria-hidden="true" className="size-5 text-primary" />
                  <span className="text-sm font-medium">
                    {t(`pages.outboundUpsert.fields.typeOptions.${type}`)}
                  </span>
                  <span className="text-xs text-muted-foreground">
                    {t(`pages.outboundUpsert.typeHints.${type}`)}
                  </span>
                </ChoiceButton>
              ))}
            </RadioGroup>
            <FieldHint error={form.errorFor("type")} />
          </FieldContent>
        </Field>
      </FieldGroup>

      {outboundType === "interface" ? (
        <FieldGroup>
          <Field invalid={Boolean(form.errorFor("interface"))}>
            <FieldLabel>
              {t("pages.outboundUpsert.interface.interface")}
            </FieldLabel>
            <FieldContent>
              <InterfacePicker
                allowCustomOption
                interfaces={runtimeInterfaces}
                invalid={Boolean(form.errorFor("interface"))}
                onChange={(value) => form.setValue("interface", value)}
                onSelect={(value) => form.setValue("interface", value)}
                placeholder={t(
                  "pages.outboundUpsert.interface.interfacePlaceholder"
                )}
                renderSelectedInline
                showDetails={false}
                value={values.interface}
              />
              <FieldHint
                description={t("pages.outboundUpsert.interface.interfaceHint")}
                error={form.errorFor("interface")}
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
              className="grid w-full max-w-3xl gap-3 md:grid-cols-2"
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
                <ChoiceButton
                  className="flex flex-col items-start gap-0.5 py-2.5 pr-9 pl-3 text-left"
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
                </ChoiceButton>
              ))}
            </RadioGroup>
            <FieldHint error={killSwitchError} />
          </Field>
        </FieldGroup>
      ) : null}

      {outboundType === "table" ? (
        <TextField
          className="max-w-sm"
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
            {t("pages.outboundUpsert.noAdditionalSettings")}
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
                      <ChoiceButton
                        className="flex flex-col items-start gap-0.5 py-3 pr-9 pl-3.5 text-left"
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
                      </ChoiceButton>
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
                    "outbound_groups",
                    synchronizeOutboundGroups(
                      values.outbound_groups,
                      normalizeOutboundGroups(nextSteps)
                    )
                  )
                }
                renderTarget={
                  isIcmptest
                    ? (stepIndex, memberIndex) => (
                        <PingTargetInput
                          field={form.field(
                            `outbound_groups[${stepIndex}].members[${memberIndex}].target`
                          )}
                        />
                      )
                    : undefined
                }
                renderWeight={(stepIndex, memberIndex) => (
                  <WeightInput
                    field={form.field(
                      `outbound_groups[${stepIndex}].members[${memberIndex}].weight`
                    )}
                    share={getMemberSharePercent(
                      values.outbound_groups[stepIndex],
                      memberIndex
                    )}
                  />
                )}
                showWeights={
                  values.strategy === "balance" || hasWeightServerError
                }
                stepErrors={steps.map((_, index) =>
                  form.errorFor(`outbound_groups[${index}]`)
                )}
                steps={steps}
                strategy={values.strategy}
              />
              {form.errorFor("outbound_groups") ? (
                <p className="text-sm text-destructive">
                  {form.errorFor("outbound_groups")}
                </p>
              ) : null}
            </div>
          </FormSection>

          {values.strategy === "priority" ? (
            <ChoiceField
              error={form.errorFor("conntrack_on_switch")}
              hint={t("pages.outboundUpsert.conntrack.hint")}
              label={t("pages.outboundUpsert.conntrack.label")}
            >
              <SegmentedControl
                aria-label={t("pages.outboundUpsert.conntrack.label")}
                onChange={(value) =>
                  form.setValue("conntrack_on_switch", value)
                }
                options={(["preserve", "delete"] as const).map((value) => ({
                  value,
                  label: t(`pages.outboundUpsert.conntrack.${value}`),
                }))}
                value={values.conntrack_on_switch}
              />
            </ChoiceField>
          ) : null}

          {isUrltest ? (
            <AdvancedSection
              badge={changedLabel(
                isChanged(values, groupDefaults, urltestPaths)
              )}
              changed={isChanged(values, groupDefaults, urltestPaths)}
              errorLabel={sectionErrorLabel(urltestPaths)}
              title={t("pages.outboundUpsert.advanced.probesTitle")}
            >
              <div className="grid gap-4 md:grid-cols-2">
                <TextField
                  className="md:col-span-2"
                  field={form.field("url")}
                  label={t("pages.outboundUpsert.urltest.probeUrl")}
                  tooltip={t("pages.outboundUpsert.urltest.probeUrlHint")}
                />
                {URLTEST_PROBE_FIELDS.map(([name, key]) => (
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
                isChanged(values, groupDefaults, icmptestPaths)
              )}
              changed={isChanged(values, groupDefaults, icmptestPaths)}
              errorLabel={sectionErrorLabel(icmptestPaths)}
              title={t("pages.outboundUpsert.icmptest.title")}
            >
              <div className="grid gap-4 md:grid-cols-2">
                {ICMPTEST_FIELDS.map(([name, key]) => (
                  <TextField
                    field={form.field(name)}
                    inputMode="numeric"
                    key={name}
                    label={t(`pages.outboundUpsert.icmptest.${key}`)}
                    tooltip={t(`pages.outboundUpsert.icmptest.${key}Hint`)}
                  />
                ))}
              </div>
            </AdvancedSection>
          ) : null}

          <AdvancedSection
            badge={changedLabel(isChanged(values, groupDefaults, breakerPaths))}
            changed={isChanged(values, groupDefaults, breakerPaths)}
            errorLabel={sectionErrorLabel(breakerPaths)}
            title={t("pages.outboundUpsert.advanced.circuitBreakerTitle")}
          >
            <FieldDescription className="mb-4">
              {t("pages.outboundUpsert.circuitBreaker.description")}
            </FieldDescription>
            <div className="grid gap-4 md:grid-cols-2">
              {CIRCUIT_BREAKER_FIELDS.map(([name, key]) => (
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

      <ServerValidationAlert errors={form.unmappedErrors()} />

      <div className="flex justify-end gap-3">
        <Button onClick={onCancel} size="xl" type="button" variant="outline">
          {t("common.cancel")}
        </Button>
        <Button
          disabled={form.isSubmitting || (mode === "edit" && !form.isDirty)}
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
  disabled,
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
  disabled?: boolean
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
          disabled={disabled}
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
  groupDefaults: Readonly<Record<string, string>>,
  paths: ReadonlyArray<Path<OutboundDraft>>
) {
  return paths.some(
    (path) =>
      getIn(values, path) !==
      (groupDefaults[path] ?? getIn(sampleNewOutbound, path))
  )
}

function getOutboundTagError(
  value: string,
  outbounds: Outbound[],
  existingTag: string | undefined,
  t: TranslateFn
) {
  return getTagNameValidationError(value, {
    requiredError: t("common.validation.required"),
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
