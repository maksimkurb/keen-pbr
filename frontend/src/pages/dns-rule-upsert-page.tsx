import { toast } from "sonner"
import { Trans, useTranslation } from "react-i18next"
import { Redirect, useLocation } from "wouter"

import { useQueryClient } from "@tanstack/react-query"

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
import { useListUsageSubtitle } from "@/hooks/use-list-usage-subtitle"
import { useDraftForm } from "@/lib/draft-form"
import {
  buildUpdatedConfigWithRules,
  type DnsRuleDraft,
  getRuleDraft,
  newRuleDraft,
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
  const configQuery = useGetConfig()

  const loadedConfig = selectConfig(configQuery.data)
  const rules = loadedConfig?.dns?.rules ?? []
  const parsedRuleIndex = Number(ruleIndex)
  const existingRule =
    mode === "edit" && Number.isInteger(parsedRuleIndex) && parsedRuleIndex >= 0
      ? rules[parsedRuleIndex]
      : undefined

  if (mode === "edit" && loadedConfig && !existingRule) {
    return <Redirect replace to="/dns-rules" />
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
  const listOptions = Object.keys(loadedConfig.lists ?? {})
  const listUsageSubtitle = useListUsageSubtitle(
    rules,
    "dns",
    mode === "edit" ? parsedRuleIndex : undefined
  )
  const postConfigMutation = usePostConfigMutation()

  const draft =
    mode === "edit" && existingRule
      ? getRuleDraft(existingRule)
      : newRuleDraft()

  const form = useDraftForm<DnsRuleDraft>(draft, {
    // Index the rule has in the array that is sent (new ones are appended).
    apiPrefix: `dns.rules[${mode === "edit" ? parsedRuleIndex : rules.length}]`,
  })
  const { values } = form

  const save = async (value: DnsRuleDraft) => {
    const nextRules = rules.map((rule) => getRuleDraft(rule))

    if (mode === "edit") {
      if (!existingRule || Number.isNaN(parsedRuleIndex)) {
        toast.error(t("pages.dnsRuleUpsert.validation.notFound"), {
          richColors: true,
        })
        return
      }

      nextRules[parsedRuleIndex] = value
    } else {
      nextRules.push(value)
    }

    const validation = validateRules(nextRules, serverTags, listOptions)
    if (Object.keys(validation).length > 0) {
      const currentIndex =
        mode === "edit" ? parsedRuleIndex : nextRules.length - 1
      const currentError = validation[currentIndex]
      if (!currentError) {
        return
      }

      const fieldErrors: Record<string, string> = {}
      if (currentError.servers) {
        fieldErrors.servers = currentError.servers
      }
      if (currentError.lists) {
        fieldErrors.list = currentError.lists
      }

      form.setServerErrors({
        form: currentError.duplicate,
        fields: fieldErrors,
      })
      return
    }

    try {
      await postConfigMutation.mutateAsync({
        data: buildUpdatedConfigWithRules(loadedConfig, nextRules),
      })
      await queryClient.invalidateQueries({ queryKey: queryKeys.dnsTest() })
      toast.success(t("pages.dnsRuleUpsert.messages.saved"))
      navigate("/dns-rules")
    } catch (error) {
      const message = form.setApiError(error as ApiError)
      if (message) {
        toast.error(message, { richColors: true })
      }
    }
  }

  // The servers control also shows errors addressed to the whole rule.
  const serverError = form.errorFor("servers", { alsoClaims: [""] })

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
        mode === "create" ? (
          t("pages.dnsRuleUpsert.createTitle")
        ) : (
          <Trans
            i18nKey="pages.dnsRuleUpsert.editNamedTitle"
            values={{ number: parsedRuleIndex + 1 }}
            components={{ entity: <span className="text-primary" /> }}
          />
        )
      }
    >
      <form className="space-y-6" onSubmit={form.onSubmit(save)}>
        <FieldGroup>
          <Field>
            <FieldContent>
              <div className="flex items-center space-x-3">
                <Checkbox
                  checked={values.enabled}
                  id="dns-rule-enabled"
                  onCheckedChange={(checked) =>
                    form.setValue("enabled", checked === true)
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

          <Field invalid={Boolean(serverError)}>
            <FieldLabel>{t("pages.dnsRuleUpsert.fields.serverTag")}</FieldLabel>
            <FieldContent>
              <MultiSelectList
                name="servers"
                onChange={(servers) => form.setValue("servers", servers)}
                options={serverTags}
                error={serverError}
                placeholderDescription={t(
                  "pages.dnsRuleUpsert.fields.serversPlaceholderDescription"
                )}
                placeholderTitle={t(
                  "pages.dnsRuleUpsert.fields.noServersSelected"
                )}
                value={values.servers}
              />
              <FieldHint
                description={
                  serverTags.length === 0
                    ? t("pages.dnsRuleUpsert.fields.noServers")
                    : values.servers.length >= 2
                      ? t("pages.dnsRuleUpsert.fields.serversRoundRobinHint")
                      : undefined
                }
              />
            </FieldContent>
          </Field>

          <Field invalid={Boolean(form.errorFor("list"))}>
            <FieldLabel>{t("pages.dnsRuleUpsert.fields.listNames")}</FieldLabel>
            <FieldContent>
              <MultiSelectList
                name="list"
                onChange={(newLists) => form.setValue("list", newLists)}
                options={listOptions}
                error={form.errorFor("list")}
                placeholderDescription={t(
                  "pages.dnsRuleUpsert.fields.listPlaceholderDescription"
                )}
                placeholderTitle={t(
                  "pages.dnsRuleUpsert.fields.noListsSelected"
                )}
                usageSubtitle={listUsageSubtitle}
                value={values.list}
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

          <Field>
            <FieldContent>
              <div className="flex items-center space-x-3">
                <Checkbox
                  checked={values.allow_domain_rebinding}
                  id="allow-domain-rebinding"
                  onCheckedChange={(checked) =>
                    form.setValue("allow_domain_rebinding", checked === true)
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
        </FieldGroup>

        <ServerValidationAlert errors={form.unmappedErrors()} />

        <div className="flex justify-end gap-3">
          <Button
            onClick={() => navigate("/dns-rules")}
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
              ? t("pages.dnsRuleUpsert.actions.create")
              : t("pages.dnsRuleUpsert.actions.save")}
          </Button>
        </div>
      </form>
    </UpsertPage>
  )
}
