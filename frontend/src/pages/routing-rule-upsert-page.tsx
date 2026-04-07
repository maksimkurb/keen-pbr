import { useEffect, useMemo, useState } from "react"
import { useTranslation } from "react-i18next"
import { useLocation } from "wouter"

import { useForm } from "@tanstack/react-form"

import type { ApiError } from "@/api/client"
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
import { UpsertPage } from "@/components/shared/upsert-page"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import {
  applyFormApiErrors,
  clearFormServerErrors,
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

  const [saveSuccessMessage, setSaveSuccessMessage] = useState<string | null>(
    null
  )
  const [mutationErrorMessage, setMutationErrorMessage] = useState<
    string | null
  >(null)

  const listOptions = useMemo(
    () =>
      Object.keys(loadedConfig?.lists ?? {}).sort((left, right) =>
        left.localeCompare(right)
      ),
    [loadedConfig]
  )
  const outboundOptions = useMemo(
    () =>
      (loadedConfig?.outbounds ?? [])
        .map((outbound: Outbound) => outbound.tag)
        .filter((tag): tag is string => Boolean(tag))
        .sort((left: string, right: string) => left.localeCompare(right)),
    [loadedConfig]
  )

  const postConfigMutation = usePostConfigMutation({
    mutation: {
      onSuccess: () => {
        setSaveSuccessMessage(t("pages.routingRuleUpsert.messages.saved"))
        setMutationErrorMessage(null)
        clearFormServerErrors(form)
        navigate("/routing-rules")
      },
      onError: (error) => {
        setSaveSuccessMessage(null)
        setMutationErrorMessage(
          applyFormApiErrors({
            error: error as ApiError,
            form,
            resolvePath: resolveRoutingRuleFieldPath,
          }) ?? null
        )
      },
    },
  })

  const form = useForm({
    defaultValues: emptyRouteRuleDraft,
    onSubmit: ({ value }) => {
      if (!loadedConfig) {
        return
      }

      const nextRule = normalizeRouteRuleDraft(value)
      const nextRules =
        mode === "edit"
          ? rules.map((rule: RouteRule, index: number) =>
              index === parsedRuleIndex ? nextRule : rule
            )
          : [...rules, nextRule]

      setSaveSuccessMessage(null)
      setMutationErrorMessage(null)
      clearFormServerErrors(form)

      postConfigMutation.mutate({
        data: {
          ...loadedConfig,
          route: {
            ...loadedConfig.route,
            rules: nextRules,
          },
        },
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

      form.reset(toRouteRuleDraft(existingRule))
      clearFormServerErrors(form)
      return
    }

    form.reset(emptyRouteRuleDraft)
    clearFormServerErrors(form)
  }, [existingRule, form, loadedConfig, mode])

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
      {saveSuccessMessage ? (
        <Alert className="mb-4 border-success/30 bg-success/5 text-success">
          <AlertDescription>{saveSuccessMessage}</AlertDescription>
        </Alert>
      ) : null}

      <form
        className="space-y-6"
        onSubmit={(event) => {
          event.preventDefault()
          void form.handleSubmit()
        }}
      >
        <FieldGroup>
          <form.Field
            name="list"
            validators={{
              onChange: ({ value }) =>
                value.length > 0
                  ? undefined
                  : t("pages.routingRuleUpsert.validation.selectList"),
            }}
          >
            {(field) => {
              const error = getFirstFieldError(field.state.meta.errors)

              return (
                <Field invalid={Boolean(error)}>
                  <FieldLabel>{t("pages.routingRuleUpsert.fields.lists")}</FieldLabel>
                  <FieldContent>
                    <MultiSelectList
                      onChange={field.handleChange}
                      options={listOptions}
                      placeholderDescription={t("pages.routingRuleUpsert.fields.listsPlaceholderDescription")}
                      placeholderTitle={t("pages.routingRuleUpsert.fields.noListsSelected")}
                      value={field.state.value}
                    />
                    <FieldHint
                      description={t("pages.routingRuleUpsert.fields.listsHint")}
                      error={error}
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>

          <form.Field name="proto">
            {(field) => (
              <Field>
                <FieldLabel>{t("pages.routingRuleUpsert.fields.proto")}</FieldLabel>
                <FieldContent>
                  <Select
                    onValueChange={(value) => field.handleChange(value ?? "")}
                    value={field.state.value}
                  >
                    <SelectTrigger>
                      <SelectValue placeholder={t("pages.routingRuleUpsert.fields.any")} />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectGroup>
                        <SelectLabel>{t("pages.routingRuleUpsert.fields.protocol")}</SelectLabel>
                        {protoOptions.map((option) => (
                          <SelectItem key={option || "any"} value={option}>
                            {option || t("pages.routingRuleUpsert.fields.anyLower")}
                          </SelectItem>
                        ))}
                      </SelectGroup>
                    </SelectContent>
                  </Select>
                  <FieldHint description={t("pages.routingRuleUpsert.fields.protoHint")} />
                </FieldContent>
              </Field>
            )}
          </form.Field>

          <form.Field name="src_port">
            {(field) => {
              const error = getFirstFieldError(field.state.meta.errors)

              return (
                <Field invalid={Boolean(error)}>
                  <FieldLabel htmlFor="routing-src-port">{t("pages.routingRuleUpsert.fields.sourcePort")}</FieldLabel>
                  <FieldContent>
                    <Input
                      aria-invalid={Boolean(error)}
                      id="routing-src-port"
                      onBlur={field.handleBlur}
                      onChange={(event) => field.handleChange(event.target.value)}
                      placeholder={t("pages.routingRuleUpsert.placeholders.sourcePort")}
                      value={field.state.value}
                    />
                    <FieldHint
                      description={t("pages.routingRuleUpsert.fields.sourcePortHint")}
                      error={error}
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>

          <form.Field name="dest_port">
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
                      onChange={(event) => field.handleChange(event.target.value)}
                      placeholder={t("pages.routingRuleUpsert.placeholders.destinationPort")}
                      value={field.state.value}
                    />
                    <FieldHint
                      description={t("pages.routingRuleUpsert.fields.destinationPortHint")}
                      error={error}
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>

          <form.Field name="src_addr">
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
                      onChange={(event) => field.handleChange(event.target.value)}
                      placeholder={t("pages.routingRuleUpsert.placeholders.sourceAddresses")}
                      value={field.state.value}
                    />
                    <FieldHint
                      description={t("pages.routingRuleUpsert.fields.sourceAddressHint")}
                      error={error}
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>

          <form.Field name="dest_addr">
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
                      onChange={(event) => field.handleChange(event.target.value)}
                      placeholder={t("pages.routingRuleUpsert.placeholders.destinationAddresses")}
                      value={field.state.value}
                    />
                    <FieldHint
                      description={t("pages.routingRuleUpsert.fields.destinationAddressHint")}
                      error={error}
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>

          <form.Field
            name="outbound"
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
                  <FieldLabel>{t("pages.routingRuleUpsert.fields.outbound")}</FieldLabel>
                  <FieldContent>
                    <Select
                      onValueChange={(value) => field.handleChange(value ?? "")}
                      value={field.state.value}
                    >
                      <SelectTrigger aria-invalid={Boolean(error)}>
                        <SelectValue placeholder={t("pages.routingRuleUpsert.fields.selectOutbound")} />
                      </SelectTrigger>
                      <SelectContent>
                        <SelectGroup>
                          <SelectLabel>{t("pages.routingRuleUpsert.fields.configuredOutbounds")}</SelectLabel>
                          {outboundOptions.map((option: string) => (
                            <SelectItem key={option} value={option}>
                              {option}
                            </SelectItem>
                          ))}
                        </SelectGroup>
                      </SelectContent>
                    </Select>
                    <FieldHint
                      description={t("pages.routingRuleUpsert.fields.outboundHint")}
                      error={error}
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>
        </FieldGroup>

        {mutationErrorMessage ? (
          <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
            <AlertDescription className="whitespace-pre-wrap">
              {mutationErrorMessage}
            </AlertDescription>
          </Alert>
        ) : null}

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
                  postConfigMutation.isPending ||
                  !loadedConfig ||
                  isPristine ||
                  !canSubmit
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

function resolveRoutingRuleFieldPath(path: string) {
  if (/^route\.rules(?:\[\d+\]|\.\d+)?\.(list|lists)$/.test(path)) {
    return "list"
  }

  if (/^route\.rules(?:\[\d+\]|\.\d+)?\.outbound$/.test(path)) {
    return "outbound"
  }

  if (/^route\.rules(?:\[\d+\]|\.\d+)?\.proto$/.test(path)) {
    return "proto"
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
