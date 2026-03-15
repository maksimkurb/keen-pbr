import { useEffect, useMemo, useState } from "react"
import { useLocation } from "wouter"

import { useForm } from "@tanstack/react-form"
import { useQueryClient } from "@tanstack/react-query"

import type { ApiError } from "@/api/client"
import { usePostConfigMutation } from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
import { useGetConfig } from "@/api/queries"
import {
  Field,
  FieldContent,
  FieldGroup,
  FieldHint,
  FieldLabel,
} from "@/components/shared/field"
import { UpsertPage } from "@/components/shared/upsert-page"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
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
  const queryClient = useQueryClient()
  const [, navigate] = useLocation()
  const configQuery = useGetConfig()

  const [saveSuccessMessage, setSaveSuccessMessage] = useState<string | null>(
    null
  )
  const [mutationErrorMessage, setMutationErrorMessage] = useState<
    string | null
  >(null)

  const loadedConfig = configQuery.data?.data
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

  const listOptions = useMemo(
    () => Object.keys(loadedConfig?.lists ?? {}),
    [loadedConfig]
  )

  const postConfigMutation = usePostConfigMutation({
    mutation: {
      onSuccess: async () => {
        await queryClient.invalidateQueries({ queryKey: queryKeys.dnsTest() })
        setSaveSuccessMessage("DNS rule saved.")
        setMutationErrorMessage(null)
        navigate("/dns-rules")
      },
      onError: (error) => {
        const apiError = error as ApiError
        setSaveSuccessMessage(null)
        setMutationErrorMessage(getApiErrorMessage(apiError))
      },
    },
  })

  const form = useForm<{ rule: DnsRuleDraft }>({
    defaultValues: {
      rule: {
        server: serverTags[0] ?? "",
        lists: "",
      },
    },
    onSubmit: ({ value }) => {
      if (!loadedConfig) {
        return
      }

      const nextRules = rules.map((rule) => getRuleDraft(rule))

      if (mode === "edit") {
        if (!existingRule || Number.isNaN(parsedRuleIndex)) {
          setMutationErrorMessage("The requested DNS rule was not found.")
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

        setMutationErrorMessage(
          currentError?.duplicate ??
            currentError?.server ??
            currentError?.lists ??
            "Fix validation errors before saving."
        )
        return
      }

      setSaveSuccessMessage(null)
      setMutationErrorMessage(null)

      postConfigMutation.mutate({
        data: buildUpdatedConfigWithRules(
          loadedConfig,
          loadedConfig.dns?.fallback ?? "",
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

      form.setFieldValue("rule", getRuleDraft(existingRule))
      return
    }

    form.setFieldValue("rule", {
      server: serverTags[0] ?? "",
      lists: "",
    })
  }, [existingRule, form, loadedConfig, mode, serverTags])

  if (mode === "edit" && loadedConfig && !existingRule) {
    return (
      <UpsertPage
        cardDescription="The requested DNS rule could not be found."
        cardTitle="Missing DNS rule"
        description="Return to DNS Rules and choose a valid entry."
        title="Edit DNS rule"
      >
        <div className="flex justify-end">
          <Button onClick={() => navigate("/dns-rules")} variant="outline">
            Back to DNS rules
          </Button>
        </div>
      </UpsertPage>
    )
  }

  return (
    <UpsertPage
      cardDescription="Set the list names and DNS server for this rule."
      cardTitle={mode === "create" ? "Create DNS rule" : "Edit DNS rule"}
      description="DNS rules map configured lists to DNS server tags."
      title={mode === "create" ? "Create DNS rule" : "Edit DNS rule"}
    >
      {saveSuccessMessage ? (
        <Alert className="mb-4 border-success/30 bg-success/5 text-success">
          <AlertDescription>{saveSuccessMessage}</AlertDescription>
        </Alert>
      ) : null}

      {mutationErrorMessage ? (
        <Alert className="mb-4 border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription>{mutationErrorMessage}</AlertDescription>
        </Alert>
      ) : null}

      <form
        className="space-y-6"
        onSubmit={(event) => {
          event.preventDefault()
          form.handleSubmit()
        }}
      >
        <FieldGroup>
          <Field>
            <FieldLabel>Server tag</FieldLabel>
            <FieldContent>
              <Select
                onValueChange={(server) =>
                  form.setFieldValue("rule.server", server)
                }
                value={form.state.values.rule.server}
              >
                <SelectTrigger>
                  <SelectValue placeholder="Select DNS server" />
                </SelectTrigger>
                <SelectContent>
                  <SelectGroup>
                    <SelectLabel>DNS servers</SelectLabel>
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
                  serverTags.length > 0
                    ? `Available: ${serverTags.join(", ")}`
                    : "No DNS servers defined in config.dns.servers."
                }
              />
            </FieldContent>
          </Field>

          <Field>
            <FieldLabel>List names</FieldLabel>
            <FieldContent>
              <Input
                onChange={(event) =>
                  form.setFieldValue("rule.lists", event.target.value)
                }
                placeholder="comma-separated list names"
                value={form.state.values.rule.lists}
              />
              <FieldHint
                description={
                  listOptions.length > 0
                    ? `Known lists: ${listOptions.join(", ")}`
                    : "No lists found in config.lists."
                }
              />
            </FieldContent>
          </Field>
        </FieldGroup>

        <div className="flex justify-end gap-3">
          <Button
            onClick={() => navigate("/dns-rules")}
            type="button"
            variant="outline"
          >
            Cancel
          </Button>
          <Button
            disabled={postConfigMutation.isPending || !loadedConfig}
            type="submit"
          >
            {mode === "create" ? "Create rule" : "Save rule"}
          </Button>
        </div>
      </form>
    </UpsertPage>
  )
}

function getApiErrorMessage(error: ApiError): string {
  if (typeof error?.message === "string" && error.message.trim().length > 0) {
    return error.message
  }

  return "Failed to save DNS configuration."
}
