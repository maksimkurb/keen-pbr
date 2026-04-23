import { useTranslation } from "react-i18next"
import { useLocation } from "wouter"

import { revalidateLogic, useForm } from "@tanstack/react-form"
import { useStore } from "@tanstack/react-store"

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
import { ServerValidationAlert } from "@/components/shared/server-validation-alert"
import { UpsertPage } from "@/components/shared/upsert-page"
import { toast } from "sonner"
import { Button } from "@/components/ui/button"
import { Checkbox } from "@/components/ui/checkbox"
import { Input } from "@/components/ui/input"
import {
  clearFormServerErrors,
  setFormServerErrors,
  splitFormApiErrors,
} from "@/lib/form-api-errors"
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
  getFirstFieldError,
  normalizeRouteRuleDraft,
  protoOptions,
  toRouteRuleDraft,
} from "@/pages/routing-rules-utils"

const ROUTING_RULE_FIELD_NAMES = {
  enabled: "enabled",
  list: "list",
  proto: "proto",
  srcPort: "src_port",
  destPort: "dest_port",
  srcAddr: "src_addr",
  destAddr: "dest_addr",
  outbound: "outbound",
} as const

type RoutingRuleFieldName =
  (typeof ROUTING_RULE_FIELD_NAMES)[keyof typeof ROUTING_RULE_FIELD_NAMES]

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
  const listOptions = Object.keys(loadedConfig.lists ?? {}).sort((left, right) =>
    left.localeCompare(right)
  )
  const outboundOptions = (loadedConfig.outbounds ?? [])
    .map((outbound: Outbound) => outbound.tag)
    .filter((tag): tag is string => Boolean(tag))
    .sort((left: string, right: string) => left.localeCompare(right))
  const protoSelectItems = protoOptions.map((option) => ({
    value: option,
    label: option || t("pages.routingRuleUpsert.fields.anyLower"),
  }))
  const outboundSelectItems = outboundOptions.map((option) => ({
    value: option,
    label: option,
  }))

  const postConfigMutation = usePostConfigMutation()

  const form = useForm({
    defaultValues:
      mode === "edit" && existingRule
        ? toRouteRuleDraft(existingRule)
        : emptyRouteRuleDraft,
    validationLogic: revalidateLogic({
      mode: "submit",
      modeAfterSubmission: "change",
    }),
    validators: {
      onSubmitAsync: async ({ value }) => {
        const nextRule = normalizeRouteRuleDraft(value)
        const hasRuleCondition =
          (nextRule.list ?? []).length > 0 ||
          Boolean(nextRule.src_port) ||
          Boolean(nextRule.dest_port) ||
          Boolean(nextRule.src_addr) ||
          Boolean(nextRule.dest_addr)

        clearFormServerErrors(form)

        if (!hasRuleCondition) {
          return {
            form: t("pages.routingRuleUpsert.validation.atLeastOneCondition"),
            fields: {},
          }
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
          clearFormServerErrors(form)
          navigate("/routing-rules")
          return undefined
        } catch (error) {
          const result = splitFormApiErrors({
            error: error as ApiError,
            fieldNames: Object.values(ROUTING_RULE_FIELD_NAMES),
            resolvePath: resolveRoutingRuleFieldPath,
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
  const submitErrorMessage = useStore(form.store, (state) => {
    const onSubmitError = state.errorMap.onSubmit
    if (typeof onSubmitError === "string") {
      return onSubmitError
    }

    const firstError = state.errors[0]
    return typeof firstError === "string" ? firstError : null
  })
  const unmappedServerErrors = useStore(
    form.store,
    (state) =>
      ((state.errorMap.onServer as {
        unmapped?: { path: string; message: string }[]
      } | undefined)?.unmapped ?? [])
  )

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
      <form
        className="space-y-6"
        onSubmit={(event) => {
          event.preventDefault()
          void form.handleSubmit()
        }}
      >
        <FieldGroup>
          <form.Field name={ROUTING_RULE_FIELD_NAMES.enabled}>
            {(field) => (
              <Field>
                <FieldContent>
                  <div className="flex items-center space-x-3">
                    <Checkbox
                      checked={field.state.value}
                      id="routing-rule-enabled"
                      onCheckedChange={(checked) =>
                        field.handleChange(checked === true)
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
            )}
          </form.Field>

          <form.Field name={ROUTING_RULE_FIELD_NAMES.list}>
            {(field) => {
              const error = getFirstFieldError(field.state.meta.errors)

              return (
                <Field invalid={Boolean(error)}>
                  <FieldLabel>
                    {t("pages.routingRuleUpsert.fields.lists")}
                  </FieldLabel>
                  <FieldContent>
                    <MultiSelectList
                      error={error}
                      name={ROUTING_RULE_FIELD_NAMES.list}
                      onChange={field.handleChange}
                      options={listOptions}
                      placeholderDescription={t(
                        "pages.routingRuleUpsert.fields.listsPlaceholderDescription"
                      )}
                      placeholderTitle={t(
                        "pages.routingRuleUpsert.fields.noListsSelected"
                      )}
                      value={field.state.value}
                    />
                    <FieldHint
                      description={t(
                        "pages.routingRuleUpsert.fields.listsHint"
                      )}
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>

          <form.Field name={ROUTING_RULE_FIELD_NAMES.proto}>
            {(field) => (
              <Field>
                <FieldLabel>
                  {t("pages.routingRuleUpsert.fields.proto")}
                </FieldLabel>
                <FieldContent>
                  <Select
                    items={protoSelectItems}
                    onValueChange={(value) => field.handleChange(value ?? "")}
                    value={field.state.value}
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
                            {option ||
                              t("pages.routingRuleUpsert.fields.anyLower")}
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
            )}
          </form.Field>

          <form.Field name={ROUTING_RULE_FIELD_NAMES.srcPort}>
            {(field) => {
              const error = getFirstFieldError(field.state.meta.errors)

              return (
                <Field invalid={Boolean(error)}>
                  <FieldLabel htmlFor="routing-src-port">
                    {t("pages.routingRuleUpsert.fields.sourcePort")}
                  </FieldLabel>
                  <FieldContent>
                    <Input
                      aria-invalid={Boolean(error)}
                      id="routing-src-port"
                      onBlur={field.handleBlur}
                      onChange={(event) =>
                        field.handleChange(event.target.value)
                      }
                      placeholder={t(
                        "pages.routingRuleUpsert.placeholders.sourcePort"
                      )}
                      value={field.state.value}
                    />
                    <FieldHint
                      description={t(
                        "pages.routingRuleUpsert.fields.sourcePortHint"
                      )}
                      error={error}
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>

          <form.Field name={ROUTING_RULE_FIELD_NAMES.destPort}>
            {(field) => {
              const error = getFirstFieldError(field.state.meta.errors)

              return (
                <Field invalid={Boolean(error)}>
                  <FieldLabel htmlFor="routing-dest-port">
                    {t("pages.routingRuleUpsert.fields.destinationPort")}
                  </FieldLabel>
                  <FieldContent>
                    <Input
                      aria-invalid={Boolean(error)}
                      id="routing-dest-port"
                      onBlur={field.handleBlur}
                      onChange={(event) =>
                        field.handleChange(event.target.value)
                      }
                      placeholder={t(
                        "pages.routingRuleUpsert.placeholders.destinationPort"
                      )}
                      value={field.state.value}
                    />
                    <FieldHint
                      description={t(
                        "pages.routingRuleUpsert.fields.destinationPortHint"
                      )}
                      error={error}
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>

          <form.Field name={ROUTING_RULE_FIELD_NAMES.srcAddr}>
            {(field) => {
              const error = getFirstFieldError(field.state.meta.errors)

              return (
                <Field invalid={Boolean(error)}>
                  <FieldLabel htmlFor="routing-src-addr">
                    {t("pages.routingRuleUpsert.fields.sourceAddresses")}
                  </FieldLabel>
                  <FieldContent>
                    <Input
                      aria-invalid={Boolean(error)}
                      id="routing-src-addr"
                      onBlur={field.handleBlur}
                      onChange={(event) =>
                        field.handleChange(event.target.value)
                      }
                      placeholder={t(
                        "pages.routingRuleUpsert.placeholders.sourceAddresses"
                      )}
                      value={field.state.value}
                    />
                    <FieldHint
                      description={t(
                        "pages.routingRuleUpsert.fields.sourceAddressHint"
                      )}
                      error={error}
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>

          <form.Field name={ROUTING_RULE_FIELD_NAMES.destAddr}>
            {(field) => {
              const error = getFirstFieldError(field.state.meta.errors)

              return (
                <Field invalid={Boolean(error)}>
                  <FieldLabel htmlFor="routing-dest-addr">
                    {t("pages.routingRuleUpsert.fields.destinationAddresses")}
                  </FieldLabel>
                  <FieldContent>
                    <Input
                      aria-invalid={Boolean(error)}
                      id="routing-dest-addr"
                      onBlur={field.handleBlur}
                      onChange={(event) =>
                        field.handleChange(event.target.value)
                      }
                      placeholder={t(
                        "pages.routingRuleUpsert.placeholders.destinationAddresses"
                      )}
                      value={field.state.value}
                    />
                    <FieldHint
                      description={t(
                        "pages.routingRuleUpsert.fields.destinationAddressHint"
                      )}
                      error={error}
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>

          <form.Field
            name={ROUTING_RULE_FIELD_NAMES.outbound}
            validators={{
              onChange: ({ value }) =>
                value.trim().length > 0
                  ? undefined
                  : t("pages.routingRuleUpsert.validation.outboundRequired"),
            }}
          >
            {(field) => {
              const error = getFirstFieldError(field.state.meta.errors)

              return (
                <Field invalid={Boolean(error)}>
                  <FieldLabel>
                    {t("pages.routingRuleUpsert.fields.outbound")}
                  </FieldLabel>
                  <FieldContent>
                    <Select
                      items={outboundSelectItems}
                      onValueChange={(value) => field.handleChange(value ?? "")}
                      value={field.state.value}
                    >
                      <SelectTrigger aria-invalid={Boolean(error)}>
                        <SelectValue
                          placeholder={t(
                            "pages.routingRuleUpsert.fields.selectOutbound"
                          )}
                        />
                      </SelectTrigger>
                      <SelectContent>
                        <SelectGroup>
                          <SelectLabel>
                            {t(
                              "pages.routingRuleUpsert.fields.configuredOutbounds"
                            )}
                          </SelectLabel>
                          {outboundOptions.map((option: string) => (
                            <SelectItem key={option} value={option}>
                              {option}
                            </SelectItem>
                          ))}
                        </SelectGroup>
                      </SelectContent>
                    </Select>
                    <FieldHint
                      description={t(
                        "pages.routingRuleUpsert.fields.outboundHint"
                      )}
                      error={error}
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>
        </FieldGroup>
        <ServerValidationAlert
          errors={unmappedServerErrors}
          message={submitErrorMessage}
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
                  ? t("pages.routingRuleUpsert.actions.create")
                  : t("pages.routingRuleUpsert.actions.save")}
              </Button>
            )}
          </form.Subscribe>
        </div>
      </form>
    </UpsertPage>
  )
}

function resolveRoutingRuleFieldPath(
  path: string
): RoutingRuleFieldName | undefined {
  if (path === "route.rules") {
    return ROUTING_RULE_FIELD_NAMES.outbound
  }

  if (/^route\.rules(?:\[\d+\]|\.\d+)?$/.test(path)) {
    return ROUTING_RULE_FIELD_NAMES.outbound
  }

  if (/^route\.rules(?:\[\d+\]|\.\d+)?\.(list|lists)$/.test(path)) {
    return ROUTING_RULE_FIELD_NAMES.list
  }

  if (/^route\.rules(?:\[\d+\]|\.\d+)?\.outbound$/.test(path)) {
    return ROUTING_RULE_FIELD_NAMES.outbound
  }

  if (/^route\.rules(?:\[\d+\]|\.\d+)?\.proto$/.test(path)) {
    return ROUTING_RULE_FIELD_NAMES.proto
  }

  if (/^route\.rules(?:\[\d+\]|\.\d+)?\.src_port$/.test(path)) {
    return ROUTING_RULE_FIELD_NAMES.srcPort
  }

  if (/^route\.rules(?:\[\d+\]|\.\d+)?\.dest_port$/.test(path)) {
    return ROUTING_RULE_FIELD_NAMES.destPort
  }

  if (/^route\.rules(?:\[\d+\]|\.\d+)?\.src_addr$/.test(path)) {
    return ROUTING_RULE_FIELD_NAMES.srcAddr
  }

  if (/^route\.rules(?:\[\d+\]|\.\d+)?\.dest_addr$/.test(path)) {
    return ROUTING_RULE_FIELD_NAMES.destAddr
  }

  return undefined
}
