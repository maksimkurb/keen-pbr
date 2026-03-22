import { useEffect, useMemo, useState } from "react"
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
  getAddressSpecError,
  getFirstFieldError,
  getPortSpecError,
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
        setSaveSuccessMessage(
          "Routing rule staged. Apply config to persist it."
        )
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
        cardDescription="The requested routing rule could not be found."
        cardTitle="Missing routing rule"
        description="Return to the routing rules table and choose a valid entry."
        title="Edit routing rule"
      >
        <div className="flex justify-end">
          <Button onClick={() => navigate("/routing-rules")} variant="outline">
            Back to routing rules
          </Button>
        </div>
      </UpsertPage>
    )
  }

  return (
    <UpsertPage
      cardDescription="Choose lists and outbound, then optionally narrow by protocol, ports, and addresses."
      cardTitle={mode === "create" ? "Create routing rule" : "Edit routing rule"}
      description="Routing rules are matched in order and direct traffic to configured outbounds."
      title={mode === "create" ? "Create routing rule" : "Edit routing rule"}
    >
      {saveSuccessMessage ? (
        <Alert className="mb-4 border-success/30 bg-success/5 text-success">
          <AlertDescription>{saveSuccessMessage}</AlertDescription>
        </Alert>
      ) : null}

      {mutationErrorMessage ? (
        <Alert className="mb-4 border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription className="whitespace-pre-wrap">
            {mutationErrorMessage}
          </AlertDescription>
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
                value.length > 0 ? undefined : "Select at least one list.",
            }}
          >
            {(field) => {
              const error = getFirstFieldError(field.state.meta.errors)

              return (
                <Field invalid={Boolean(error)}>
                  <FieldLabel>Lists</FieldLabel>
                  <FieldContent>
                    <MultiSelectList
                      onChange={field.handleChange}
                      options={listOptions}
                      placeholderDescription="Add one or more configured list names to match for this rule."
                      placeholderTitle="No lists selected"
                      value={field.state.value}
                    />
                    <FieldHint
                      description="List names are sourced from config.lists keys."
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
                <FieldLabel>Proto</FieldLabel>
                <FieldContent>
                  <Select
                    onValueChange={(value) => field.handleChange(value ?? "")}
                    value={field.state.value}
                  >
                    <SelectTrigger>
                      <SelectValue placeholder="Any" />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectGroup>
                        <SelectLabel>Protocol</SelectLabel>
                        {protoOptions.map((option) => (
                          <SelectItem key={option || "any"} value={option}>
                            {option || "any"}
                          </SelectItem>
                        ))}
                      </SelectGroup>
                    </SelectContent>
                  </Select>
                  <FieldHint description='Use "any" by leaving proto empty.' />
                </FieldContent>
              </Field>
            )}
          </form.Field>

          <form.Field
            name="src_port"
            validators={{
              onChange: ({ value }) => getPortSpecError(value) ?? undefined,
            }}
          >
            {(field) => {
              const error = getFirstFieldError(field.state.meta.errors)

              return (
                <Field invalid={Boolean(error)}>
                  <FieldLabel htmlFor="routing-src-port">Source port</FieldLabel>
                  <FieldContent>
                    <Input
                      aria-invalid={Boolean(error)}
                      id="routing-src-port"
                      onBlur={field.handleBlur}
                      onChange={(event) => field.handleChange(event.target.value)}
                      placeholder="80,443 or 10000-20000"
                      value={field.state.value}
                    />
                    <FieldHint
                      description="Comma-separated ports or ranges. Optional leading ! to negate."
                      error={error}
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>

          <form.Field
            name="dest_port"
            validators={{
              onChange: ({ value }) => getPortSpecError(value) ?? undefined,
            }}
          >
            {(field) => {
              const error = getFirstFieldError(field.state.meta.errors)

              return (
                <Field invalid={Boolean(error)}>
                  <FieldLabel htmlFor="routing-dest-port">
                    Destination port
                  </FieldLabel>
                  <FieldContent>
                    <Input
                      aria-invalid={Boolean(error)}
                      id="routing-dest-port"
                      onBlur={field.handleBlur}
                      onChange={(event) => field.handleChange(event.target.value)}
                      placeholder="443 or !53,123"
                      value={field.state.value}
                    />
                    <FieldHint
                      description="Comma-separated ports or ranges. Optional leading ! to negate."
                      error={error}
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>

          <form.Field
            name="src_addr"
            validators={{
              onChange: ({ value }) => getAddressSpecError(value) ?? undefined,
            }}
          >
            {(field) => {
              const error = getFirstFieldError(field.state.meta.errors)

              return (
                <Field invalid={Boolean(error)}>
                  <FieldLabel htmlFor="routing-src-addr">
                    Source addresses
                  </FieldLabel>
                  <FieldContent>
                    <Input
                      aria-invalid={Boolean(error)}
                      id="routing-src-addr"
                      onBlur={field.handleBlur}
                      onChange={(event) => field.handleChange(event.target.value)}
                      placeholder="192.168.1.10,10.0.0.0/8"
                      value={field.state.value}
                    />
                    <FieldHint
                      description="Comma-separated IP addresses or CIDRs. Optional leading ! negates the entire spec."
                      error={error}
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>

          <form.Field
            name="dest_addr"
            validators={{
              onChange: ({ value }) => getAddressSpecError(value) ?? undefined,
            }}
          >
            {(field) => {
              const error = getFirstFieldError(field.state.meta.errors)

              return (
                <Field invalid={Boolean(error)}>
                  <FieldLabel htmlFor="routing-dest-addr">
                    Destination addresses
                  </FieldLabel>
                  <FieldContent>
                    <Input
                      aria-invalid={Boolean(error)}
                      id="routing-dest-addr"
                      onBlur={field.handleBlur}
                      onChange={(event) => field.handleChange(event.target.value)}
                      placeholder="2001:db8::1 or !203.0.113.0/24"
                      value={field.state.value}
                    />
                    <FieldHint
                      description="Comma-separated IP addresses or CIDRs. Optional leading ! negates the entire spec."
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
                  : "Outbound tag is required.",
            }}
          >
            {(field) => {
              const error = getFirstFieldError(field.state.meta.errors)

              return (
                <Field invalid={Boolean(error)}>
                  <FieldLabel>Outbound</FieldLabel>
                  <FieldContent>
                    <Select
                      onValueChange={(value) => field.handleChange(value ?? "")}
                      value={field.state.value}
                    >
                      <SelectTrigger aria-invalid={Boolean(error)}>
                        <SelectValue placeholder="Select outbound" />
                      </SelectTrigger>
                      <SelectContent>
                        <SelectGroup>
                          <SelectLabel>Configured outbounds</SelectLabel>
                          {outboundOptions.map((option: string) => (
                            <SelectItem key={option} value={option}>
                              {option}
                            </SelectItem>
                          ))}
                        </SelectGroup>
                      </SelectContent>
                    </Select>
                    <FieldHint
                      description="Outbound tags are sourced from config.outbounds."
                      error={error}
                    />
                  </FieldContent>
                </Field>
              )
            }}
          </form.Field>
        </FieldGroup>

        <div className="flex justify-end gap-3">
          <Button
            onClick={() => navigate("/routing-rules")}
            size="xl"
            type="button"
            variant="outline"
          >
            Cancel
          </Button>
          <form.Subscribe
            selector={(state) => ({
              canSubmit: state.canSubmit,
            })}
          >
            {({ canSubmit }) => (
              <Button
                disabled={
                  postConfigMutation.isPending || !loadedConfig || !canSubmit
                }
                size="xl"
                type="submit"
              >
                {mode === "create" ? "Create rule" : "Save rule"}
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
