import { useEffect, useMemo } from "react"
import { toast } from "sonner"
import { useTranslation } from "react-i18next"
import { useLocation } from "wouter"

import { useForm } from "@tanstack/react-form"
import { useQueryClient } from "@tanstack/react-query"

import type { ApiError } from "@/api/client"
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
import { UpsertPage } from "@/components/shared/upsert-page"
import { Button } from "@/components/ui/button"
import { Checkbox } from "@/components/ui/checkbox"
import {
  applyFormApiErrors,
  clearFormServerErrors,
  setFormServerErrors,
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
  type DnsRuleDraft,
  validateRules,
} from "@/pages/dns-rules-utils"

export function DnsRuleUpsertPage({
  mode,
  ruleIndex,
}: {
  mode: "create" | "edit"
  ruleIndex?: string
}) {
  const { t } = useTranslation()
  const queryClient = useQueryClient()
  const [, navigate] = useLocation()
  const configQuery = useGetConfig()

  const loadedConfig = selectConfig(configQuery.data)
  const rules = loadedConfig?.dns?.rules ?? []
  const parsedRuleIndex = Number(ruleIndex)
  const existingRule =
    mode === "edit" && Number.isInteger(parsedRuleIndex) && parsedRuleIndex >= 0
      ? rules[parsedRuleIndex]
      : undefined

  const serverTags = useMemo(
    () =>
      (loadedConfig?.dns?.servers ?? [])
        .map((server) => server.tag)
        .filter(Boolean),
    [loadedConfig]
  )
  const serverSelectItems = serverTags.map((serverTag) => ({
    value: serverTag,
    label: serverTag,
  }))

  const listOptions = useMemo(
    () => Object.keys(loadedConfig?.lists ?? {}),
    [loadedConfig]
  )

  const postConfigMutation = usePostConfigMutation({
    mutation: {
      onSuccess: async () => {
        await queryClient.invalidateQueries({ queryKey: queryKeys.dnsTest() })
        toast.success(t("pages.dnsRuleUpsert.messages.saved"))
        clearFormServerErrors(form)
        navigate("/dns-rules")
      },
      onError: (error) => {
        const formError = applyFormApiErrors({
          error: error as ApiError,
          form,
          resolvePath: resolveDnsRuleFieldPath,
        })
        if (formError) {
          toast.error(formError, { richColors: true })
        }
      },
    },
  })

  const defaultValues: { rule: DnsRuleDraft } = {
    rule: {
      enabled: true,
      server: serverTags[0] ?? "",
      lists: [],
      allowDomainRebinding: false,
    },
  }

  const form = useForm({
    defaultValues,
    onSubmit: ({ value }) => {
      if (!loadedConfig) {
        return
      }

      const nextRules = rules.map((rule) => getRuleDraft(rule))

      if (mode === "edit") {
        if (!existingRule || Number.isNaN(parsedRuleIndex)) {
          toast.error(t("pages.dnsRuleUpsert.validation.notFound"), {
            richColors: true,
          })
          return
        }

        nextRules[parsedRuleIndex] = value.rule
      } else {
        nextRules.push(value.rule)
      }

      const validation = validateRules(nextRules, serverTags, listOptions)
      if (Object.keys(validation).length > 0) {
        const currentIndex =
          mode === "edit" ? parsedRuleIndex : nextRules.length - 1
        const currentError = validation[currentIndex]
        if (currentError) {
          const fieldErrors: Record<string, string> = {}
          if (currentError.server) {
            fieldErrors["rule.server"] = currentError.server
          }
          if (currentError.lists) {
            fieldErrors["rule.lists"] = currentError.lists
          }

          setFormServerErrors(form, {
            form: currentError.duplicate,
            fields: fieldErrors,
          })
        }
        return
      }

      clearFormServerErrors(form)

      postConfigMutation.mutate({
        data: buildUpdatedConfigWithRules(
          loadedConfig,
          loadedConfig.dns?.fallback ?? [],
          nextRules
        ),
      })
    },
  })

  useEffect(() => {
    if (!loadedConfig) {
      return
    }

    if (mode === "edit") {
      if (!existingRule) {
        return
      }

      form.reset({
        rule: getRuleDraft(existingRule),
      })
      clearFormServerErrors(form)
      return
    }

    form.reset({
      rule: {
        enabled: true,
        server: serverTags[0] ?? "",
        lists: [],
        allowDomainRebinding: false,
      },
    })
    clearFormServerErrors(form)
  }, [existingRule, form, loadedConfig, mode, serverTags])

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
          <form.Field name="rule.enabled">
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
                      {t("pages.routingRules.headers.enabled")}
                    </FieldLabel>
                  </div>
                </FieldContent>
              </Field>
            )}
          </form.Field>

          <form.Field name="rule.server">
            {(field) => {
              const error = field.state.meta.errors[0] as string | undefined
              return (
                <Field invalid={Boolean(error)}>
                  <FieldLabel>
                    {t("pages.dnsRuleUpsert.fields.serverTag")}
                  </FieldLabel>
                  <FieldContent>
                    <Select
                      items={serverSelectItems}
                      onValueChange={(server) => field.handleChange(server ?? "")}
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
                        serverTags.length === 0 ? t("pages.dnsRuleUpsert.fields.noServers") : undefined
                      }
                      error={error}
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>

          <form.Field name="rule.lists">
            {(field) => {
              const error = field.state.meta.errors[0] as string | undefined
              return (
                <Field invalid={Boolean(error)}>
                  <FieldLabel>
                    {t("pages.dnsRuleUpsert.fields.listNames")}
                  </FieldLabel>
                  <FieldContent>
                    <MultiSelectList
                      onChange={field.handleChange}
                      options={listOptions}
                      placeholderDescription={t(
                        "pages.dnsRuleUpsert.fields.listPlaceholderDescription"
                      )}
                      placeholderTitle={t(
                        "pages.dnsRuleUpsert.fields.noListsSelected"
                      )}
                      value={field.state.value}
                      error={error}
                    />
                    <FieldHint
                      description={
                        listOptions.length === 0 ? t("pages.dnsRuleUpsert.fields.noLists") : undefined
                      }
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>

          <form.Field name="rule.allowDomainRebinding">
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
                  postConfigMutation.isPending ||
                  !loadedConfig ||
                  isPristine ||
                  !canSubmit
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

function resolveDnsRuleFieldPath(path: string) {
  if (/^dns\.rules(?:\[\d+\]|\.\d+)?\.server$/.test(path)) {
    return "rule.server"
  }

  if (/^dns\.rules(?:\[\d+\]|\.\d+)?\.(list|lists)$/.test(path)) {
    return "rule.lists"
  }

  if (/^dns\.rules(?:\[\d+\]|\.\d+)?\.allow_domain_rebinding$/.test(path)) {
    return "rule.allowDomainRebinding"
  }

  return undefined
}
