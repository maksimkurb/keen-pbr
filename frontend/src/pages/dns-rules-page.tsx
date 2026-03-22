import { Pencil, Plus, Trash2 } from "lucide-react"
import { useMemo, useState } from "react"
import { useTranslation } from "react-i18next"

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
import { getApiErrorMessage } from "@/lib/api-errors"
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
  const { t } = useTranslation()
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
          t("pages.dnsRules.messages.saved")
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
      setMutationErrorMessage(t("pages.dnsRules.validation.invalidFallback"))
      return
    }

    const draftRules = rules.map((rule) => getRuleDraft(rule))
    const validation = validateRules(draftRules, serverTags, listOptions)
    if (Object.keys(validation).length > 0) {
      setMutationErrorMessage(
        t("pages.dnsRules.validation.invalidFallbackChange")
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
        t("pages.dnsRules.validation.invalidResult")
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
            {t("pages.dnsRules.actions.add")}
          </Button>
        }
        description={t("pages.dnsRules.description")}
        title={t("pages.dnsRules.title")}
      />

      {saveSuccessMessage ? (
        <Alert className="border-success/30 bg-success/5 text-success">
          <AlertDescription>{saveSuccessMessage}</AlertDescription>
        </Alert>
      ) : null}

      {mutationErrorMessage ? (
        <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription className="whitespace-pre-wrap">
            {mutationErrorMessage}
          </AlertDescription>
        </Alert>
      ) : null}

      {configQuery.isLoading ? (
        <TableSkeleton />
      ) : configQuery.isError ? (
        <ListPlaceholder
          description={t("common.loadErrorDescription")}
          title={t("common.unableToLoadData")}
          variant="error"
        />
      ) : (
        <>
          <SectionCard
            description={t("pages.dnsRules.fallback.description")}
            title={t("pages.dnsRules.fallback.title")}
          >
            <Field>
              <FieldLabel>{t("pages.dnsRules.fallback.field")}</FieldLabel>
              <FieldContent>
                <Select
                  disabled={isPending || !loadedConfig}
                  onValueChange={handleFallbackChange}
                  value={loadedConfig?.dns?.fallback ?? ""}
                >
                  <SelectTrigger>
                    <SelectValue placeholder={t("pages.dnsRules.fallback.placeholder")} />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectGroup>
                      <SelectLabel>{t("pages.dnsRules.fallback.group")}</SelectLabel>
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
                      ? t("pages.dnsRules.fallback.available", { tags: serverTags.join(", ") })
                      : t("pages.dnsRules.fallback.noneDefined")
                  }
                />
              </FieldContent>
            </Field>
          </SectionCard>

          {rules.length === 0 ? (
            <ListPlaceholder
              description={t("pages.dnsRules.empty.description")}
              title={t("pages.dnsRules.empty.title")}
            />
          ) : (
            <DataTable
              headers={[
                t("pages.dnsRules.headers.lists"),
                t("pages.dnsRules.headers.serverTag"),
                t("pages.dnsRules.headers.actions"),
              ]}
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
                      label: t("common.edit"),
                      onClick: () => navigate(`/dns-rules/${index}/edit`),
                    },
                    {
                      icon: <Trash2 className="h-4 w-4" />,
                      label: t("common.delete"),
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
