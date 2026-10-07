import { useTranslation } from "react-i18next"
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
  emptyRouteRuleDraft,
  normalizeRouteRuleDraft,
  protoOptions,
  type RouteRuleMode,
  type RouteRuleDraft,
  toRouteRuleDraft,
} from "@/pages/routing-rules-utils"

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
    validate: (value) => {
      const errors: Record<string, string> = {}

      const outboundError =
        value.outbound.trim().length === 0
          ? t("pages.routingRuleUpsert.validation.outboundRequired")
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
      form.setApiError(error as ApiError, resolveRoutingRuleFieldPath)
    }
  }

  const isNormalRule = values.mode === "normal"

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
      <form className="space-y-6" onSubmit={form.onSubmit(save)}>
        <FieldGroup>
          <Field>
            <FieldLabel>{t("pages.routingRuleUpsert.fields.mode")}</FieldLabel>
            <FieldContent>
              <Select
                onValueChange={(value) => {
                  const nextMode = (value as RouteRuleMode) ?? "normal"
                  form.setValues((prev) => ({
                    ...prev,
                    mode: nextMode,
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
                value={values.mode}
              >
                <SelectTrigger>
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
              <FieldHint
                description={t("pages.routingRuleUpsert.fields.modeHint")}
              />
            </FieldContent>
          </Field>

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
                  className="cursor-pointer flex-col items-start gap-0"
                  htmlFor="routing-rule-enabled"
                >
                  {t("common.enabled")}
                </FieldLabel>
              </div>
            </FieldContent>
          </Field>

          <Field
            hidden={!isNormalRule}
            invalid={Boolean(form.errorFor("list"))}
          >
            <FieldLabel>{t("pages.routingRuleUpsert.fields.lists")}</FieldLabel>
            <FieldContent>
              <MultiSelectList
                error={form.errorFor("list")}
                name="list"
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
              <FieldHint
                description={t("pages.routingRuleUpsert.fields.listsHint")}
              />
            </FieldContent>
          </Field>

          <Field hidden={!isNormalRule}>
            <FieldLabel>{t("pages.routingRuleUpsert.fields.proto")}</FieldLabel>
            <FieldContent>
              <Select
                items={protoSelectItems}
                onValueChange={(value) => form.setValue("proto", value ?? "")}
                value={values.proto}
              >
                <SelectTrigger>
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
              <FieldHint
                description={t("pages.routingRuleUpsert.fields.protoHint")}
              />
            </FieldContent>
          </Field>

          <Field
            hidden={!isNormalRule}
            invalid={Boolean(form.errorFor("dscp"))}
          >
            <FieldLabel htmlFor="routing-dscp">
              {t("pages.routingRuleUpsert.fields.dscp")}
            </FieldLabel>
            <FieldContent>
              <Input
                aria-invalid={Boolean(form.errorFor("dscp"))}
                id="routing-dscp"
                inputMode="numeric"
                max={63}
                min={1}
                onChange={(event) => form.setValue("dscp", event.target.value)}
                placeholder={t("pages.routingRuleUpsert.placeholders.dscp")}
                type="number"
                value={values.dscp}
              />
              <FieldHint
                description={t("pages.routingRuleUpsert.fields.dscpHint")}
                error={form.errorFor("dscp")}
              />
            </FieldContent>
          </Field>

          <Field
            hidden={!isNormalRule}
            invalid={Boolean(form.errorFor("src_port"))}
          >
            <FieldLabel htmlFor="routing-src-port">
              {t("pages.routingRuleUpsert.fields.sourcePort")}
            </FieldLabel>
            <FieldContent>
              <Input
                aria-invalid={Boolean(form.errorFor("src_port"))}
                id="routing-src-port"
                onChange={(event) =>
                  form.setValue("src_port", event.target.value)
                }
                placeholder={t(
                  "pages.routingRuleUpsert.placeholders.sourcePort"
                )}
                value={values.src_port}
              />
              <FieldHint
                description={t("pages.routingRuleUpsert.fields.sourcePortHint")}
                error={form.errorFor("src_port")}
              />
            </FieldContent>
          </Field>

          <Field
            hidden={!isNormalRule}
            invalid={Boolean(form.errorFor("dest_port"))}
          >
            <FieldLabel htmlFor="routing-dest-port">
              {t("pages.routingRuleUpsert.fields.destinationPort")}
            </FieldLabel>
            <FieldContent>
              <Input
                aria-invalid={Boolean(form.errorFor("dest_port"))}
                id="routing-dest-port"
                onChange={(event) =>
                  form.setValue("dest_port", event.target.value)
                }
                placeholder={t(
                  "pages.routingRuleUpsert.placeholders.destinationPort"
                )}
                value={values.dest_port}
              />
              <FieldHint
                description={t(
                  "pages.routingRuleUpsert.fields.destinationPortHint"
                )}
                error={form.errorFor("dest_port")}
              />
            </FieldContent>
          </Field>

          <Field
            hidden={!isNormalRule}
            invalid={Boolean(form.errorFor("src_addr"))}
          >
            <FieldLabel htmlFor="routing-src-addr">
              {t("pages.routingRuleUpsert.fields.sourceAddresses")}
            </FieldLabel>
            <FieldContent>
              <Input
                aria-invalid={Boolean(form.errorFor("src_addr"))}
                id="routing-src-addr"
                onChange={(event) =>
                  form.setValue("src_addr", event.target.value)
                }
                placeholder={t(
                  "pages.routingRuleUpsert.placeholders.sourceAddresses"
                )}
                value={values.src_addr}
              />
              <FieldHint
                description={t(
                  "pages.routingRuleUpsert.fields.sourceAddressHint"
                )}
                error={form.errorFor("src_addr")}
              />
            </FieldContent>
          </Field>

          <Field
            hidden={!isNormalRule}
            invalid={Boolean(form.errorFor("dest_addr"))}
          >
            <FieldLabel htmlFor="routing-dest-addr">
              {t("pages.routingRuleUpsert.fields.destinationAddresses")}
            </FieldLabel>
            <FieldContent>
              <Input
                aria-invalid={Boolean(form.errorFor("dest_addr"))}
                id="routing-dest-addr"
                onChange={(event) =>
                  form.setValue("dest_addr", event.target.value)
                }
                placeholder={t(
                  "pages.routingRuleUpsert.placeholders.destinationAddresses"
                )}
                value={values.dest_addr}
              />
              <FieldHint
                description={t(
                  "pages.routingRuleUpsert.fields.destinationAddressHint"
                )}
                error={form.errorFor("dest_addr")}
              />
            </FieldContent>
          </Field>

          <Field invalid={Boolean(form.errorFor("outbound"))}>
            <FieldLabel>
              {t("pages.routingRuleUpsert.fields.outbound")}
            </FieldLabel>
            <FieldContent>
              <OutboundSelect
                ariaInvalid={Boolean(form.errorFor("outbound"))}
                onValueChange={(value) => form.setValue("outbound", value)}
                outbounds={outbounds}
                value={values.outbound}
              />
              <FieldHint
                description={t("pages.routingRuleUpsert.fields.outboundHint")}
                error={form.errorFor("outbound")}
              />
            </FieldContent>
          </Field>
        </FieldGroup>
        <ServerValidationAlert
          errors={form.errors.unmapped}
          message={form.errors.form}
        />

        <div className="flex justify-end gap-3">
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
              postConfigMutation.isPending || !form.isDirty || form.isSubmitting
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

function resolveRoutingRuleFieldPath(path: string): string | undefined {
  if (path === "route.rules") {
    return "outbound"
  }

  if (/^route\.rules(?:\[\d+\]|\.\d+)?$/.test(path)) {
    return "outbound"
  }

  if (/^route\.rules(?:\[\d+\]|\.\d+)?\.default_gateway$/.test(path)) {
    return "mode"
  }

  if (/^route\.rules(?:\[\d+\]|\.\d+)?\.(list|lists)$/.test(path)) {
    return "list"
  }

  if (/^route\.rules(?:\[\d+\]|\.\d+)?\.outbound$/.test(path)) {
    return "outbound"
  }

  if (/^route\.rules(?:\[\d+\]|\.\d+)?\.proto$/.test(path)) {
    return "proto"
  }

  if (/^route\.rules(?:\[\d+\]|\.\d+)?\.dscp$/.test(path)) {
    return "dscp"
  }

  if (/^route\.rules(?:\[\d+\]|\.\d+)?\.src_port$/.test(path)) {
    return "src_port"
  }

  if (/^route\.rules(?:\[\d+\]|\.\d+)?\.dest_port$/.test(path)) {
    return "dest_port"
  }

  if (/^route\.rules(?:\[\d+\]|\.\d+)?\.src_addr$/.test(path)) {
    return "src_addr"
  }

  if (/^route\.rules(?:\[\d+\]|\.\d+)?\.dest_addr$/.test(path)) {
    return "dest_addr"
  }

  return undefined
}

function validateDscp(value: string, t: (key: string) => string) {
  const trimmed = value.trim()
  if (trimmed.length === 0) {
    return undefined
  }

  if (!/^\d+$/.test(trimmed)) {
    return t("pages.routingRuleUpsert.validation.dscpRange")
  }

  const parsed = Number(trimmed)
  return parsed >= 1 && parsed <= 63
    ? undefined
    : t("pages.routingRuleUpsert.validation.dscpRange")
}
