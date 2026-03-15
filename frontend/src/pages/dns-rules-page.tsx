import { Pencil, Plus, Trash2 } from "lucide-react"
import { useMemo, useState } from "react"

import { useQueryClient } from "@tanstack/react-query"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import { usePostConfigMutation } from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
import { useGetConfig } from "@/api/queries"
import { selectConfig } from "@/api/selectors"
import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable } from "@/components/shared/data-table"
import {
  Field,
  FieldContent,
  FieldHint,
  FieldLabel,
} from "@/components/shared/field"
import { ListPlaceholder } from "@/components/shared/list-placeholder"
import { PageHeader } from "@/components/shared/page-header"
import { SectionCard } from "@/components/shared/section-card"
import { TableSkeleton } from "@/components/shared/table-skeleton"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
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

export function DnsRulesPage() {
  const queryClient = useQueryClient()
  const [, navigate] = useLocation()
  const configQuery = useGetConfig()

  const [saveSuccessMessage, setSaveSuccessMessage] = useState<string | null>(
    null
  )
  const [mutationErrorMessage, setMutationErrorMessage] = useState<
    string | null
  >(null)

  const loadedConfig = selectConfig(configQuery.data)

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
        setSaveSuccessMessage(
          "DNS configuration staged. Apply config to persist it."
        )
        setMutationErrorMessage(null)
      },
      onError: (error) => {
        const apiError = error as ApiError
        setSaveSuccessMessage(null)
        setMutationErrorMessage(getApiErrorMessage(apiError))
      },
    },
  })

  const isPending = postConfigMutation.isPending
  const rules = loadedConfig?.dns?.rules ?? []

  const handleFallbackChange = (fallback: string | null) => {
    if (!loadedConfig) {
      return
    }

    if (!fallback) {
      return
    }

    if (!serverTags.includes(fallback)) {
      setMutationErrorMessage(
        "Fallback server must reference an existing server tag."
      )
      return
    }

    const draftRules = rules.map((rule) => getRuleDraft(rule))
    const validation = validateRules(draftRules, serverTags, listOptions)
    if (Object.keys(validation).length > 0) {
      setMutationErrorMessage(
        "Cannot change fallback while DNS rules are invalid."
      )
      return
    }

    setSaveSuccessMessage(null)
    setMutationErrorMessage(null)

    postConfigMutation.mutate({
      data: buildUpdatedConfigWithRules(loadedConfig, fallback, draftRules),
    })
  }

  const handleDeleteRule = (index: number) => {
    if (!loadedConfig) {
      return
    }

    const nextDraftRules = rules
      .filter((_rule, ruleIndex) => ruleIndex !== index)
      .map((rule) => getRuleDraft(rule))

    const validation = validateRules(nextDraftRules, serverTags, listOptions)
    if (Object.keys(validation).length > 0) {
      setMutationErrorMessage(
        "Cannot save because resulting DNS rules are invalid."
      )
      return
    }

    setSaveSuccessMessage(null)
    setMutationErrorMessage(null)

    postConfigMutation.mutate({
      data: buildUpdatedConfigWithRules(
        loadedConfig,
        loadedConfig.dns?.fallback ?? "",
        nextDraftRules
      ),
    })
  }

  return (
    <div className="space-y-6">
      <PageHeader
        actions={
          <Button onClick={() => navigate("/dns-rules/create")}>
            <Plus className="mr-1 h-4 w-4" />
            Add DNS rule
          </Button>
        }
        description="Assign routing lists to specific DNS servers."
        title="DNS Rules"
      />

      {saveSuccessMessage ? (
        <Alert className="border-success/30 bg-success/5 text-success">
          <AlertDescription>{saveSuccessMessage}</AlertDescription>
        </Alert>
      ) : null}

      {mutationErrorMessage ? (
        <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription>{mutationErrorMessage}</AlertDescription>
        </Alert>
      ) : null}

      {configQuery.isLoading ? (
        <TableSkeleton />
      ) : configQuery.isError ? (
        <ListPlaceholder
          description="We can't load DNS rules right now. Try refreshing the page."
          title="Unable to load data"
          variant="error"
        />
      ) : (
        <>
          <SectionCard
            description="Used when no DNS rule matches the current request."
            title="Fallback"
          >
            <Field>
              <FieldLabel>Fallback server tag</FieldLabel>
              <FieldContent>
                <Select
                  disabled={isPending || !loadedConfig}
                  onValueChange={handleFallbackChange}
                  value={loadedConfig?.dns?.fallback ?? ""}
                >
                  <SelectTrigger>
                    <SelectValue placeholder="Select a DNS server" />
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
          </SectionCard>

          {rules.length === 0 ? (
            <ListPlaceholder
              description="Add a DNS rule to map configured lists to DNS servers."
              title="No DNS rules yet"
            />
          ) : (
            <DataTable
              headers={["Lists", "Server tag", "Actions"]}
              rows={rules.map((rule, index) => [
                <div className="flex flex-wrap gap-2" key={`lists-${index}`}>
                  {rule.list.map((listName) => (
                    <Badge key={`${index}-${listName}`} variant="outline">
                      {listName}
                    </Badge>
                  ))}
                </div>,
                <span className="font-medium" key={`server-${index}`}>
                  {rule.server}
                </span>,
                <ActionButtons
                  actions={[
                    {
                      icon: <Pencil className="h-4 w-4" />,
                      label: "Edit",
                      onClick: () => navigate(`/dns-rules/${index}/edit`),
                    },
                    {
                      icon: <Trash2 className="h-4 w-4" />,
                      label: "Delete",
                      onClick: () => handleDeleteRule(index),
                    },
                  ]}
                  key={`actions-${index}`}
                />,
              ])}
            />
          )}
        </>
      )}
    </div>
  )
}

function getApiErrorMessage(error: ApiError): string {
  if (typeof error?.message === "string" && error.message.trim().length > 0) {
    return error.message
  }

  return "Failed to save DNS configuration."
}
