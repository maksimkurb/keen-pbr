import { toast } from "sonner"
import { useTranslation } from "react-i18next"
import { useLocation } from "wouter"

import { useForm } from "@tanstack/react-form"
import { useQueryClient } from "@tanstack/react-query"
import { useStore } from "@tanstack/react-store"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { DnsRule } from "@/api/generated/model/dnsRule"
import { usePostConfigMutation } from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
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
import { Button } from "@/components/ui/button"
import { Checkbox } from "@/components/ui/checkbox"
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
  buildUpdatedConfigWithRules,
  getRuleDraft,
  validateRules,
} from "@/pages/dns-rules-utils"

const DNS_RULE_FIELD_NAMES = {
  enabled: "rule.enabled",
  server: "rule.server",
  lists: "rule.lists",
  allowDomainRebinding: "rule.allowDomainRebinding",
} as const

type DnsRuleFieldName =
  (typeof DNS_RULE_FIELD_NAMES)[keyof typeof DNS_RULE_FIELD_NAMES]

export function DnsRuleUpsertPage({
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
  const rules = loadedConfig?.dns?.rules ?? []
  const parsedRuleIndex = Number(ruleIndex)
  const existingRule =
    mode === "edit" && Number.isInteger(parsedRuleIndex) && parsedRuleIndex >= 0
      ? rules[parsedRuleIndex]
      : undefined

  if (mode === "edit" && loadedConfig && !existingRule) {
    return (
      <UpsertPage
        cardDescription={t("pages.dnsRuleUpsert.missing.cardDescription")}
        cardTitle={t("pages.dnsRuleUpsert.missing.cardTitle")}
        description={t("pages.dnsRuleUpsert.missing.description")}
        title={t("pages.dnsRuleUpsert.editTitle")}
      >
        <div className="flex justify-end">
          <Button onClick={() => navigate("/dns-rules")} variant="outline">
            {t("pages.dnsRuleUpsert.missing.back")}
          </Button>
        </div>
      </UpsertPage>
    )
  }

  if (!loadedConfig) {
    return (
      <UpsertPage
        cardDescription={t("pages.dnsRuleUpsert.cardDescription")}
        cardTitle={
          mode === "create"
            ? t("pages.dnsRuleUpsert.createTitle")
            : t("pages.dnsRuleUpsert.editTitle")
        }
        description={t("pages.dnsRuleUpsert.description")}
        title={
          mode === "create"
            ? t("pages.dnsRuleUpsert.createTitle")
            : t("pages.dnsRuleUpsert.editTitle")
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
    <DnsRuleForm
      key={`${mode}:${ruleIndex ?? "new"}`}
      existingRule={existingRule}
      loadedConfig={loadedConfig}
      mode={mode}
      parsedRuleIndex={parsedRuleIndex}
      rules={rules}
    />
  )
}

function DnsRuleForm({
  existingRule,
  loadedConfig,
  mode,
  parsedRuleIndex,
  rules,
}: {
  existingRule?: DnsRule
  loadedConfig: ConfigObject
  mode: "create" | "edit"
  parsedRuleIndex: number
  rules: DnsRule[]
}) {
  const { t } = useTranslation()
  const queryClient = useQueryClient()
  const [, navigate] = useLocation()
  const serverTags = (loadedConfig.dns?.servers ?? [])
    .map((server) => server.tag)
    .filter(Boolean)
  const serverSelectItems = serverTags.map((serverTag) => ({
    value: serverTag,
    label: serverTag,
  }))
  const listOptions = Object.keys(loadedConfig.lists ?? {})
  const postConfigMutation = usePostConfigMutation()
  const form = useForm({
    defaultValues: {
      rule:
        mode === "edit" && existingRule
          ? getRuleDraft(existingRule)
          : {
              enabled: true,
              server: serverTags[0] ?? "",
              lists: [],
              allowDomainRebinding: false,
            },
    },
    validators: {
      onSubmitAsync: async ({ value }) => {
        const nextRules = rules.map((rule) => getRuleDraft(rule))

        if (mode === "edit") {
          if (!existingRule || Number.isNaN(parsedRuleIndex)) {
            toast.error(t("pages.dnsRuleUpsert.validation.notFound"), {
              richColors: true,
            })
            return undefined
          }

          nextRules[parsedRuleIndex] = value.rule
        } else {
          nextRules.push(value.rule)
        }

        clearFormServerErrors(form)

        const validation = validateRules(nextRules, serverTags, listOptions)
        if (Object.keys(validation).length > 0) {
          const currentIndex =
            mode === "edit" ? parsedRuleIndex : nextRules.length - 1
          const currentError = validation[currentIndex]
          if (!currentError) {
            return undefined
          }

          const fieldErrors: Record<string, string> = {}
          if (currentError.server) {
            fieldErrors[DNS_RULE_FIELD_NAMES.server] = currentError.server
          }
          if (currentError.lists) {
            fieldErrors[DNS_RULE_FIELD_NAMES.lists] = currentError.lists
          }

          setFormServerErrors(form, {
            form: currentError.duplicate,
            fields: fieldErrors,
          })
          return {
            form: currentError.duplicate,
            fields: fieldErrors,
          }
        }

        try {
          await postConfigMutation.mutateAsync({
            data: buildUpdatedConfigWithRules(
              loadedConfig,
              loadedConfig.dns?.fallback ?? [],
              nextRules
            ),
          })
          await queryClient.invalidateQueries({ queryKey: queryKeys.dnsTest() })
          toast.success(t("pages.dnsRuleUpsert.messages.saved"))
          clearFormServerErrors(form)
          navigate("/dns-rules")
          return undefined
        } catch (error) {
          const result = splitFormApiErrors({
            error: error as ApiError,
            fieldNames: Object.values(DNS_RULE_FIELD_NAMES),
            resolvePath: resolveDnsRuleFieldPath,
          })

          setFormServerErrors(form, {
            form: result.formError ?? undefined,
            fields: result.fieldErrors,
            unmapped: result.unmappedErrors,
          })
          if (result.formError) {
            toast.error(result.formError, { richColors: true })
          }

          return {
            form: result.formError ?? undefined,
            fields: result.fieldErrors,
          }
        }
      },
    },
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
      cardDescription={t("pages.dnsRuleUpsert.cardDescription")}
      cardTitle={
        mode === "create"
          ? t("pages.dnsRuleUpsert.createTitle")
          : t("pages.dnsRuleUpsert.editTitle")
      }
      description={t("pages.dnsRuleUpsert.description")}
      title={
        mode === "create"
          ? t("pages.dnsRuleUpsert.createTitle")
          : t("pages.dnsRuleUpsert.editTitle")
      }
    >
      <form
        className="space-y-6"
        onSubmit={(event) => {
          event.preventDefault()
          form.handleSubmit()
        }}
      >
        <FieldGroup>
          <form.Field name={DNS_RULE_FIELD_NAMES.enabled}>
            {(field) => (
              <Field>
                <FieldContent>
                  <div className="flex items-center space-x-3">
                    <Checkbox
                      checked={field.state.value}
                      id="dns-rule-enabled"
                      onCheckedChange={(checked) =>
                        field.handleChange(checked === true)
                      }
                    />
                    <FieldLabel
                      className="cursor-pointer flex-col items-start gap-0"
                      htmlFor="dns-rule-enabled"
                    >
                      {t("common.enabled")}
                    </FieldLabel>
                  </div>
                </FieldContent>
              </Field>
            )}
          </form.Field>

          <form.Field name={DNS_RULE_FIELD_NAMES.server}>
            {(field) => {
              const error = getFirstFieldError(field.state.meta.errors)
              return (
                <Field invalid={Boolean(error)}>
                  <FieldLabel>
                    {t("pages.dnsRuleUpsert.fields.serverTag")}
                  </FieldLabel>
                  <FieldContent>
                    <Select
                      items={serverSelectItems}
                      onValueChange={(server) =>
                        field.handleChange(server ?? "")
                      }
                      value={field.state.value}
                    >
                      <SelectTrigger aria-invalid={Boolean(error)}>
                        <SelectValue
                          placeholder={t(
                            "pages.dnsRuleUpsert.fields.selectServer"
                          )}
                        />
                      </SelectTrigger>
                      <SelectContent>
                        <SelectGroup>
                          <SelectLabel>
                            {t("pages.dnsRuleUpsert.fields.dnsServers")}
                          </SelectLabel>
                          {serverTags.map((serverTag) => (
                            <SelectItem key={serverTag} value={serverTag}>
                              {serverTag}
                            </SelectItem>
                          ))}
                        </SelectGroup>
                      </SelectContent>
                    </Select>
                    <FieldHint
                      description={
                        serverTags.length === 0
                          ? t("pages.dnsRuleUpsert.fields.noServers")
                          : undefined
                      }
                      error={error}
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>

          <form.Field name={DNS_RULE_FIELD_NAMES.lists}>
            {(field) => {
              const error = getFirstFieldError(field.state.meta.errors)
              return (
                <Field invalid={Boolean(error)}>
                  <FieldLabel>
                    {t("pages.dnsRuleUpsert.fields.listNames")}
                  </FieldLabel>
                  <FieldContent>
                    <MultiSelectList
                      name={DNS_RULE_FIELD_NAMES.lists}
                      onChange={field.handleChange}
                      options={listOptions}
                      error={error}
                      placeholderDescription={t(
                        "pages.dnsRuleUpsert.fields.listPlaceholderDescription"
                      )}
                      placeholderTitle={t(
                        "pages.dnsRuleUpsert.fields.noListsSelected"
                      )}
                      value={field.state.value}
                    />
                    <FieldHint
                      description={
                        listOptions.length === 0
                          ? t("pages.dnsRuleUpsert.fields.noLists")
                          : undefined
                      }
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>

          <form.Field name={DNS_RULE_FIELD_NAMES.allowDomainRebinding}>
            {(field) => (
              <Field>
                <FieldContent>
                  <div className="flex items-center space-x-3">
                    <Checkbox
                      checked={field.state.value}
                      id="allow-domain-rebinding"
                      onCheckedChange={(checked) =>
                        field.handleChange(checked === true)
                      }
                    />
                    <FieldLabel
                      className="cursor-pointer flex-col items-start gap-0"
                      htmlFor="allow-domain-rebinding"
                    >
                      {t("pages.dnsRuleUpsert.fields.allowDomainRebinding")}
                    </FieldLabel>
                  </div>
                  <FieldHint
                    description={t(
                      "pages.dnsRuleUpsert.fields.allowDomainRebindingHint"
                    )}
                  />
                </FieldContent>
              </Field>
            )}
          </form.Field>
        </FieldGroup>

        <ServerValidationAlert errors={unmappedServerErrors} />

        <div className="flex justify-end gap-3">
          <Button
            onClick={() => navigate("/dns-rules")}
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
                  ? t("pages.dnsRuleUpsert.actions.create")
                  : t("pages.dnsRuleUpsert.actions.save")}
              </Button>
            )}
          </form.Subscribe>
        </div>
      </form>
    </UpsertPage>
  )
}

function getFirstFieldError(errors: unknown[]) {
  const firstError = errors[0]
  return typeof firstError === "string" ? firstError : undefined
}

function resolveDnsRuleFieldPath(path: string): DnsRuleFieldName | undefined {
  if (path === "dns.rules") {
    return DNS_RULE_FIELD_NAMES.server
  }

  if (/^dns\.rules(?:\[\d+\]|\.\d+)?$/.test(path)) {
    return DNS_RULE_FIELD_NAMES.server
  }

  if (/^dns\.rules(?:\[\d+\]|\.\d+)?\.server$/.test(path)) {
    return DNS_RULE_FIELD_NAMES.server
  }

  if (/^dns\.rules(?:\[\d+\]|\.\d+)?\.(list|lists)$/.test(path)) {
    return DNS_RULE_FIELD_NAMES.lists
  }

  if (/^dns\.rules(?:\[\d+\]|\.\d+)?\.allow_domain_rebinding$/.test(path)) {
    return DNS_RULE_FIELD_NAMES.allowDomainRebinding
  }

  return undefined
}
