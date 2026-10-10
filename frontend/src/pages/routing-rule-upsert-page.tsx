import { useState, type ReactNode } from "react"
import { RoutingRuleConditionBuilder } from "@/components/routing-rules/routing-rule-condition-builder"
import { Trans, useTranslation } from "react-i18next"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { Outbound } from "@/api/generated/model/outbound"
import type { RouteRule } from "@/api/generated/model/routeRule"
import { usePostConfigMutation } from "@/api/mutations"
import { useGetConfig } from "@/api/queries"
import { selectConfig } from "@/api/selectors"
import {
  Field,
  FieldContent,
  FieldDescription,
  FieldGroup,
  FieldHint,
  FieldLabel,
} from "@/components/shared/field"
import { MultiSelectList } from "@/components/shared/multi-select-list"
import { OutboundSelect } from "@/components/shared/outbound-select"
import { ServerValidationAlert } from "@/components/shared/server-validation-alert"
import { UpsertPage } from "@/components/shared/upsert-page"
import { toast } from "sonner"
import { Button } from "@/components/ui/button"
import { Checkbox } from "@/components/ui/checkbox"
import { Input } from "@/components/ui/input"
import { useListUsageSubtitle } from "@/hooks/use-list-usage-subtitle"
import { useDraftForm } from "@/lib/draft-form"
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectLabel,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import {
  getActiveRouteConditions,
  validateDscp,
  type RouteConditionKey,
  emptyRouteRuleDraft,
  normalizeRouteRuleDraft,
  protoOptions,
  type RouteRuleMode,
  type RouteRuleDraft,
  toRouteRuleDraft,
} from "@/pages/routing-rules-utils"

import {
  PLATFORM_GENERIC,
  PLATFORM_OPENWRT,
  PLATFORM_DEVELOPMENT,
} from "@/lib/platform"

// Direct build-time comparisons let the bundler remove the selector on Keenetic.
const DEFAULT_GATEWAY_RULES_SUPPORTED =
  import.meta.env.VITE_KEEN_PBR_PLATFORM === PLATFORM_GENERIC ||
  import.meta.env.VITE_KEEN_PBR_PLATFORM === PLATFORM_OPENWRT ||
  import.meta.env.VITE_KEEN_PBR_PLATFORM === PLATFORM_DEVELOPMENT

const routeRuleModeOptions = ["normal", "ipv4", "ipv6"] as const

export function RoutingRuleUpsertPage({
  mode,
  ruleIndex,
}: {
  mode: "create" | "edit"
  ruleIndex?: string
}) {
  const { t } = useTranslation()
  const [, navigate] = useLocation()
  const configQuery = useGetConfig()
  const loadedConfig = selectConfig(configQuery.data)
  const rules = loadedConfig?.route?.rules ?? []
  const parsedRuleIndex = Number(ruleIndex)
  const existingRule =
    mode === "edit" && Number.isInteger(parsedRuleIndex) && parsedRuleIndex >= 0
      ? rules[parsedRuleIndex]
      : undefined

  if (mode === "edit" && loadedConfig && !existingRule) {
    return (
      <UpsertPage
        cardDescription={t("pages.routingRuleUpsert.missing.cardDescription")}
        cardTitle={t("pages.routingRuleUpsert.missing.cardTitle")}
        description={t("pages.routingRuleUpsert.missing.description")}
        title={t("pages.routingRuleUpsert.editTitle")}
      >
        <div className="flex justify-end">
          <Button onClick={() => navigate("/routing-rules")} variant="outline">
            {t("pages.routingRuleUpsert.missing.back")}
          </Button>
        </div>
      </UpsertPage>
    )
  }

  if (!loadedConfig) {
    return (
      <UpsertPage
        cardDescription={t("pages.routingRuleUpsert.cardDescription")}
        cardTitle={
          mode === "create"
            ? t("pages.routingRuleUpsert.createTitle")
            : t("pages.routingRuleUpsert.editTitle")
        }
        description={t("pages.routingRuleUpsert.description")}
        title={
          mode === "create"
            ? t("pages.routingRuleUpsert.createTitle")
            : t("pages.routingRuleUpsert.editTitle")
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

  return (
    <RoutingRuleForm
      key={`${mode}:${ruleIndex ?? "new"}`}
      existingRule={existingRule}
      loadedConfig={loadedConfig}
      mode={mode}
      parsedRuleIndex={parsedRuleIndex}
      rules={rules}
    />
  )
}

function RoutingRuleForm({
  existingRule,
  loadedConfig,
  mode,
  parsedRuleIndex,
  rules,
}: {
  existingRule?: RouteRule
  loadedConfig: ConfigObject
  mode: "create" | "edit"
  parsedRuleIndex: number
  rules: RouteRule[]
}) {
  const { t } = useTranslation()
  const [, navigate] = useLocation()
  const listOptions = Object.keys(loadedConfig.lists ?? {}).sort(
    (left, right) => left.localeCompare(right)
  )
  const outbounds = (loadedConfig.outbounds ?? [])
    .filter((outbound: Outbound): outbound is Outbound & { tag: string } =>
      Boolean(outbound.tag)
    )
    .sort((left: Outbound, right: Outbound) =>
      left.tag.localeCompare(right.tag)
    )
  const listUsageSubtitle = useListUsageSubtitle(
    rules,
    "routing",
    mode === "edit" ? parsedRuleIndex : undefined
  )
  const modeSelectItems = DEFAULT_GATEWAY_RULES_SUPPORTED
    ? routeRuleModeOptions.map((value) => ({
        value,
        label: t(`pages.routingRuleUpsert.fields.modeOptions.${value}`),
      }))
    : []
  const protoSelectItems = protoOptions.map((option) => ({
    value: option,
    label: option || t("pages.routingRuleUpsert.fields.anyLower"),
  }))

  const postConfigMutation = usePostConfigMutation()

  const draft =
    mode === "edit" && existingRule
      ? toRouteRuleDraft(existingRule)
      : emptyRouteRuleDraft

  const form = useDraftForm<RouteRuleDraft>(draft, {
    // Index the rule has in the array that is sent (new ones are appended).
    apiPrefix: `route.rules[${mode === "edit" ? parsedRuleIndex : rules.length}]`,
    validate: (value) => {
      const errors: Record<string, string> = {}

      const outboundError =
        value.outbound.trim().length === 0
          ? t("common.validation.required")
          : undefined
      if (outboundError) {
        errors.outbound = outboundError
      }

      const dscpError = validateDscp(value.dscp, t)
      if (dscpError) {
        errors.dscp = dscpError
      }

      return errors
    },
  })
  const { values } = form

  const save = async (value: RouteRuleDraft) => {
    const nextRule = normalizeRouteRuleDraft(value)
    const hasRuleCondition =
      nextRule.default_gateway !== undefined ||
      (nextRule.list ?? []).length > 0 ||
      nextRule.dscp !== undefined ||
      Boolean(nextRule.src_port) ||
      Boolean(nextRule.dest_port) ||
      Boolean(nextRule.src_addr) ||
      Boolean(nextRule.dest_addr)

    if (!hasRuleCondition) {
      form.setServerErrors({
        form: t("pages.routingRuleUpsert.validation.atLeastOneCondition"),
      })
      return
    }

    const nextRules =
      mode === "edit"
        ? rules.map((rule: RouteRule, index: number) =>
            index === parsedRuleIndex ? nextRule : rule
          )
        : [...rules, nextRule]

    try {
      await postConfigMutation.mutateAsync({
        data: {
          ...loadedConfig,
          route: {
            ...loadedConfig.route,
            rules: nextRules,
          },
        },
      })
      toast.success(t("pages.routingRuleUpsert.messages.saved"))
      navigate("/routing-rules")
    } catch (error) {
      form.setApiError(error as ApiError)
    }
  }

  const gatewayError = DEFAULT_GATEWAY_RULES_SUPPORTED
    ? form.errorFor("default_gateway")
    : null
  const isNormalRule = values.default_gateway === "normal"

  // Empty opened editors are UI state only; values and serialization stay in useDraftForm.
  const [openedConditions, setOpenedConditions] = useState<RouteConditionKey[]>(
    () => getActiveRouteConditions(draft, [])
  )
  const activeConditions = getActiveRouteConditions(
    values,
    openedConditions,
    form.errorPaths()
  )
  const showOutbound = !isNormalRule || activeConditions.length > 0
  const outboundError = showOutbound ? form.errorFor("outbound") : null
  const conditionControls: Partial<Record<RouteConditionKey, ReactNode>> = {}
  if (activeConditions.includes("list")) {
    conditionControls.list = (
      <Field className="gap-2" invalid={Boolean(form.errorFor("list"))}>
        <FieldLabel id="routing-list-label">
          {t("pages.routingRuleUpsert.fields.lists")}
        </FieldLabel>
        <FieldContent>
          <FieldDescription>
            {t("pages.routingRuleUpsert.builder.descriptions.list")}
          </FieldDescription>
          <MultiSelectList
            error={form.errorFor("list")}
            name="list"
            compact
            ariaLabelledBy="routing-list-label"
            onChange={(value) => form.setValue("list", value)}
            options={listOptions}
            placeholderDescription={t(
              "pages.routingRuleUpsert.fields.listsPlaceholderDescription"
            )}
            placeholderTitle={t(
              "pages.routingRuleUpsert.fields.noListsSelected"
            )}
            usageSubtitle={listUsageSubtitle}
            value={values.list}
          />
        </FieldContent>
      </Field>
    )
  }
  if (activeConditions.includes("proto")) {
    conditionControls.proto = (
      <Field className="gap-2" invalid={Boolean(form.errorFor("proto"))}>
        <FieldLabel htmlFor="routing-proto">
          {t("pages.routingRuleUpsert.fields.proto")}
        </FieldLabel>
        <FieldContent>
          <FieldDescription>
            {t("pages.routingRuleUpsert.builder.descriptions.proto")}
          </FieldDescription>
          <Select
            items={protoSelectItems}
            onValueChange={(value) => form.setValue("proto", value ?? "")}
            value={values.proto}
          >
            <SelectTrigger
              id="routing-proto"
              aria-invalid={Boolean(form.errorFor("proto"))}
            >
              <SelectValue
                placeholder={t("pages.routingRuleUpsert.fields.any")}
              />
            </SelectTrigger>
            <SelectContent>
              <SelectGroup>
                <SelectLabel>
                  {t("pages.routingRuleUpsert.fields.protocol")}
                </SelectLabel>
                {protoOptions.map((option) => (
                  <SelectItem key={option || "any"} value={option}>
                    {option || t("pages.routingRuleUpsert.fields.anyLower")}
                  </SelectItem>
                ))}
              </SelectGroup>
            </SelectContent>
          </Select>
          <FieldHint error={form.errorFor("proto")} />
        </FieldContent>
      </Field>
    )
  }
  const inputConditions = [
    ["dscp", "dscp", "dscpHint"],
    ["src_port", "sourcePort", "sourcePortHint"],
    ["dest_port", "destinationPort", "destinationPortHint"],
    ["src_addr", "sourceAddresses", "sourceAddressHint"],
    ["dest_addr", "destinationAddresses", "destinationAddressHint"],
  ] as const
  for (const [key, label, hint] of inputConditions) {
    if (!activeConditions.includes(key)) continue
    const error = form.errorFor(key)
    const id = `routing-${key}`
    conditionControls[key] = (
      <Field className="gap-2" invalid={Boolean(error)}>
        <FieldLabel htmlFor={id}>
          {t(`pages.routingRuleUpsert.fields.${label}`)}
        </FieldLabel>
        <FieldContent>
          <FieldDescription id={`${id}-description`}>
            {t(`pages.routingRuleUpsert.fields.${hint}`)}
          </FieldDescription>
          <Input
            aria-invalid={Boolean(error)}
            aria-describedby={`${id}-description ${id}-hint`}
            id={id}
            name={key}
            inputMode={key === "dscp" ? "numeric" : undefined}
            max={key === "dscp" ? 63 : undefined}
            min={key === "dscp" ? 1 : undefined}
            onChange={(event) => form.setValue(key, event.target.value)}
            placeholder={t(`pages.routingRuleUpsert.placeholders.${label}`)}
            type={key === "dscp" ? "number" : "text"}
            value={values[key]}
          />
          <div id={`${id}-hint`}>
            <FieldHint error={error} />
          </div>
        </FieldContent>
      </Field>
    )
  }

  return (
    <UpsertPage
      description={t("pages.routingRuleUpsert.description")}
      title={
        mode === "create" ? (
          t("pages.routingRuleUpsert.createTitle")
        ) : (
          <Trans
            i18nKey="pages.routingRuleUpsert.editNamedTitle"
            values={{ number: parsedRuleIndex + 1 }}
            components={{ entity: <span className="text-primary" /> }}
          />
        )
      }
    >
      <form className="min-w-0 space-y-6" onSubmit={form.onSubmit(save)}>
        <ServerValidationAlert
          errors={form.unmappedErrors()}
          message={form.formError}
        />
        <FieldGroup>
          <Field>
            <FieldContent>
              <div className="flex items-center space-x-3">
                <Checkbox
                  checked={values.enabled}
                  id="routing-rule-enabled"
                  onCheckedChange={(checked) =>
                    form.setValue("enabled", checked === true)
                  }
                />
                <FieldLabel
                  className="cursor-pointer flex-col items-start justify-center gap-0"
                  htmlFor="routing-rule-enabled"
                >
                  {t("pages.routingRuleUpsert.fields.enabled")}
                </FieldLabel>
              </div>
            </FieldContent>
          </Field>

          {DEFAULT_GATEWAY_RULES_SUPPORTED ? (
            <Field invalid={Boolean(gatewayError)}>
              <FieldLabel htmlFor="routing-rule-mode">
                {t("pages.routingRuleUpsert.fields.mode")}
              </FieldLabel>
              <FieldContent>
                <Select
                  items={modeSelectItems}
                  onValueChange={(value) => {
                    const nextMode = (value as RouteRuleMode) ?? "normal"
                    if (nextMode !== "normal") setOpenedConditions([])
                    form.setValues((prev) => ({
                      ...prev,
                      default_gateway: nextMode,
                      // Condition fields only apply to normal rules: back to
                      // their initial values.
                      ...(nextMode !== "normal"
                        ? {
                            list: draft.list,
                            proto: draft.proto,
                            dscp: draft.dscp,
                            src_port: draft.src_port,
                            dest_port: draft.dest_port,
                            src_addr: draft.src_addr,
                            dest_addr: draft.dest_addr,
                          }
                        : {}),
                    }))
                  }}
                  value={values.default_gateway}
                >
                  <SelectTrigger
                    id="routing-rule-mode"
                    aria-invalid={Boolean(gatewayError)}
                  >
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectGroup>
                      <SelectLabel>
                        {t("pages.routingRuleUpsert.fields.ruleType")}
                      </SelectLabel>
                      {routeRuleModeOptions.map((option) => (
                        <SelectItem key={option} value={option}>
                          {t(
                            `pages.routingRuleUpsert.fields.modeOptions.${option}`
                          )}
                        </SelectItem>
                      ))}
                    </SelectGroup>
                  </SelectContent>
                </Select>
                <FieldHint error={gatewayError} />
              </FieldContent>
            </Field>
          ) : null}
        </FieldGroup>
        <RoutingRuleConditionBuilder
          activeConditions={isNormalRule ? activeConditions : []}
          controls={conditionControls}
          isNormalRule={isNormalRule}
          onAdd={(key) =>
            setOpenedConditions((previous) =>
              previous.includes(key) ? previous : [...activeConditions, key]
            )
          }
          onRemove={(key) => {
            if (key === "list") form.setValue("list", [])
            else form.setValue(key, "")
            setOpenedConditions((previous) =>
              previous.filter((item) => item !== key)
            )
          }}
          outbound={
            showOutbound ? (
              <Field invalid={Boolean(outboundError)}>
                <FieldLabel id="routing-outbound-label">
                  {t("pages.routingRuleUpsert.builder.routeThrough")}
                </FieldLabel>
                <FieldContent>
                  <OutboundSelect
                    ariaInvalid={Boolean(outboundError)}
                    ariaLabelledBy="routing-outbound-label"
                    onValueChange={(value) => form.setValue("outbound", value)}
                    outbounds={outbounds}
                    value={values.outbound}
                  />
                  <FieldHint
                    description={t(
                      "pages.routingRuleUpsert.fields.outboundHint"
                    )}
                    error={outboundError}
                  />
                </FieldContent>
              </Field>
            ) : null
          }
        />

        <div className="flex flex-wrap justify-end gap-3">
          <Button
            onClick={() => navigate("/routing-rules")}
            size="xl"
            type="button"
            variant="outline"
          >
            {t("common.cancel")}
          </Button>
          <Button
            disabled={
              postConfigMutation.isPending ||
              (mode === "edit" && !form.isDirty) ||
              form.isSubmitting
            }
            size="xl"
            type="submit"
          >
            {mode === "create"
              ? t("pages.routingRuleUpsert.actions.create")
              : t("pages.routingRuleUpsert.actions.save")}
          </Button>
        </div>
      </form>
    </UpsertPage>
  )
}
