import { Pencil, Plus, Trash2 } from "lucide-react"
import { useMemo, useState } from "react"
import { useTranslation } from "react-i18next"
import { toast } from "sonner"

import { useQueryClient } from "@tanstack/react-query"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import { usePostConfigMutation, useConfigMutationPending } from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
import { useGetConfig } from "@/api/queries"
import { selectConfig } from "@/api/selectors"
import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable, type DataTableSelection } from "@/components/shared/data-table"
import {
  Field,
  FieldContent,
  FieldHint,
  FieldLabel,
} from "@/components/shared/field"
import { ListPlaceholder } from "@/components/shared/list-placeholder"
import { MultiSelectList } from "@/components/shared/multi-select-list"
import { PageHeader } from "@/components/shared/page-header"
import { TableSkeleton } from "@/components/shared/table-skeleton"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Card, CardContent } from "@/components/ui/card"
import { Switch } from "@/components/ui/switch"
import { getApiErrorMessage } from "@/lib/api-errors"
import {
  buildUpdatedConfigWithRules,
  getRuleDraft,
  setDnsRuleEnabled,
  validateRules,
} from "@/pages/dns-rules-utils"

export function DnsRulesPage() {
  const { t } = useTranslation()
  const queryClient = useQueryClient()
  const [, navigate] = useLocation()
  const configQuery = useGetConfig()
  const configMutationPending = useConfigMutationPending()

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
        toast.success(t("pages.dnsRules.messages.saved"))
      },
      onError: (error) => {
        const apiError = error as ApiError
        toast.error(getApiErrorMessage(apiError), { richColors: true })
      },
    },
  })

  const rules = useMemo(
    () => loadedConfig?.dns?.rules ?? [],
    [loadedConfig?.dns?.rules],
  )

  const [selectedDnsRuleIndices, setSelectedDnsRuleIndices] = useState<
    Set<string>
  >(() => new Set())

  const validDnsRuleIndexSet = useMemo(
    () => new Set(rules.map((_rule, ruleIndex: number) => String(ruleIndex))),
    [rules],
  )

  const selectedDnsRuleIndicesResolved = useMemo(() => {
    const next = new Set<string>()
    for (const id of selectedDnsRuleIndices) {
      if (validDnsRuleIndexSet.has(id)) {
        next.add(id)
      }
    }

    return next
  }, [selectedDnsRuleIndices, validDnsRuleIndexSet])

  const handleFallbackChange = (fallback: string[]) => {
    if (!loadedConfig) {
      return
    }

    if (fallback.some((tag) => !serverTags.includes(tag))) {
      toast.error(t("pages.dnsRules.validation.invalidFallback"), {
        richColors: true,
      })
      return
    }

    const draftRules = rules.map((rule) => getRuleDraft(rule))
    const validation = validateRules(draftRules, serverTags, listOptions)
    if (Object.keys(validation).length > 0) {
      toast.error(t("pages.dnsRules.validation.invalidFallbackChange"), {
        richColors: true,
      })
      return
    }

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
      toast.error(t("pages.dnsRules.validation.invalidResult"), {
        richColors: true,
      })
      return
    }

    postConfigMutation.mutate({
      data: buildUpdatedConfigWithRules(
        loadedConfig,
        loadedConfig.dns?.fallback ?? [],
        nextDraftRules
      ),
    })
  }

  const handleEnabledChange = (index: number, enabled: boolean) => {
    if (!loadedConfig) {
      return
    }

    const nextDraftRules = setDnsRuleEnabled(
      rules.map((rule) => getRuleDraft(rule)),
      index,
      enabled
    )

    const validation = validateRules(nextDraftRules, serverTags, listOptions)
    if (Object.keys(validation).length > 0) {
      toast.error(t("pages.dnsRules.validation.invalidResult"), {
        richColors: true,
      })
      return
    }

    postConfigMutation.mutate({
      data: buildUpdatedConfigWithRules(
        loadedConfig,
        loadedConfig.dns?.fallback ?? [],
        nextDraftRules
      ),
    })
  }

  const handleBulkDeleteRules = () => {
    if (!loadedConfig || selectedDnsRuleIndicesResolved.size === 0) {
      return
    }

    const confirmed = window.confirm(
      t("pages.dnsRules.bulk.confirmDelete", {
        count: selectedDnsRuleIndicesResolved.size,
      }),
    )

    if (!confirmed) {
      return
    }

    const nextDraftRules = rules
      .filter((_rule, ruleIndex) => !selectedDnsRuleIndicesResolved.has(String(ruleIndex)))
      .map((rule) => getRuleDraft(rule))

    const validation = validateRules(nextDraftRules, serverTags, listOptions)
    if (Object.keys(validation).length > 0) {
      toast.error(t("pages.dnsRules.validation.invalidResult"), {
        richColors: true,
      })
      return
    }

    postConfigMutation.mutate(
      {
        data: buildUpdatedConfigWithRules(
          loadedConfig,
          loadedConfig.dns?.fallback ?? [],
          nextDraftRules,
        ),
      },
      {
        onSuccess: () => {
          setSelectedDnsRuleIndices(new Set())
        },
      },
    )
  }

  const handleBulkSetDnsRulesEnabled = (enabled: boolean) => {
    if (!loadedConfig || selectedDnsRuleIndicesResolved.size === 0) {
      return
    }

    let nextDraftRules = rules.map((rule) => getRuleDraft(rule))
    for (const sid of selectedDnsRuleIndicesResolved) {
      nextDraftRules = setDnsRuleEnabled(nextDraftRules, Number(sid), enabled)
    }

    const validation = validateRules(nextDraftRules, serverTags, listOptions)
    if (Object.keys(validation).length > 0) {
      toast.error(t("pages.dnsRules.validation.invalidResult"), {
        richColors: true,
      })
      return
    }

    postConfigMutation.mutate({
      data: buildUpdatedConfigWithRules(
        loadedConfig,
        loadedConfig.dns?.fallback ?? [],
        nextDraftRules,
      ),
    })
  }

  const dnsRulesSelectionProps: DataTableSelection = {
    rowIds: rules.map((_rule, ruleIndex: number) => String(ruleIndex)),
    selectedIds: selectedDnsRuleIndicesResolved,
    selectionDisabled: configMutationPending,
    selectAllAriaLabel: t("common.selection.selectAll"),
    selectRowAriaLabel: (rowId: string) =>
      t("common.selection.selectRow", {
        rowLabel: `${t("pages.dnsRules.title")} #${Number(rowId) + 1}`,
      }),
    selectAllTooltip: t("common.selection.selectAll"),
    onToggleRow: (rowId: string) => {
      setSelectedDnsRuleIndices((previous) => {
        const next = new Set(previous)
        if (next.has(rowId)) {
          next.delete(rowId)
        } else {
          next.add(rowId)
        }

        return next
      })
    },
    onSelectAllVisible: (selectedAll: boolean) => {
      setSelectedDnsRuleIndices(
        selectedAll ? new Set(rules.map((_r, idx) => String(idx))) : new Set(),
      )
    },
  }

  return (
    <div className="space-y-6">
      <PageHeader
        actions={
          <Button disabled={configMutationPending} onClick={() => navigate("/dns-rules/create")}>
            <Plus className="mr-1 h-4 w-4" />
            {t("pages.dnsRules.actions.add")}
          </Button>
        }
        description={t("pages.dnsRules.description")}
        title={t("pages.dnsRules.title")}
      />

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
          <Card inert={configMutationPending ? true : undefined}>
            <CardContent>
              <Field>
                <FieldLabel>{t("pages.dnsRules.fallback.title")}</FieldLabel>
                <FieldContent>
                  <MultiSelectList
                    addLabel={t("pages.dnsRules.fallback.add")}
                    allowReorder
                    emptyMessage={t("pages.dnsRules.fallback.noneAvailable")}
                    onChange={handleFallbackChange}
                    options={serverTags}
                    placeholderDescription={t(
                      "pages.dnsRules.fallback.placeholderDescription"
                    )}
                    placeholderTitle={t("pages.dnsRules.fallback.placeholderTitle")}
                    value={loadedConfig?.dns?.fallback ?? []}
                  />
                  <FieldHint
                    description={
                      serverTags.length === 0 ? (
                        <>
                          {t("pages.dnsRules.fallback.description")}{" "}
                          {t("pages.dnsRules.fallback.noneDefined")}
                        </>
                      ) : (
                        t("pages.dnsRules.fallback.description")
                      )
                    }
                  />
                </FieldContent>
              </Field>
            </CardContent>
          </Card>

          {rules.length === 0 ? (
            <ListPlaceholder
              description={t("pages.dnsRules.empty.description")}
              title={t("pages.dnsRules.empty.title")}
            />
          ) : (
            <div className="space-y-3">
              {selectedDnsRuleIndicesResolved.size > 0 ? (
                <div className="flex flex-wrap items-center gap-2 rounded-lg border bg-muted/20 px-3 py-2">
                  <span className="text-sm font-medium tabular-nums">
                    {t("pages.dnsRules.bulk.selected", {
                      count: selectedDnsRuleIndicesResolved.size,
                    })}
                  </span>
                  <Button
                    disabled={configMutationPending}
                    onClick={() => handleBulkSetDnsRulesEnabled(true)}
                    size="sm"
                    variant="outline"
                  >
                    {t("pages.dnsRules.bulk.enable")}
                  </Button>
                  <Button
                    disabled={configMutationPending}
                    onClick={() => handleBulkSetDnsRulesEnabled(false)}
                    size="sm"
                    variant="outline"
                  >
                    {t("pages.dnsRules.bulk.disable")}
                  </Button>
                  <Button
                    disabled={configMutationPending}
                    onClick={() => handleBulkDeleteRules()}
                    size="sm"
                    variant="destructive"
                  >
                    <Trash2 className="mr-1 h-4 w-4" />
                    {t("pages.dnsRules.bulk.delete")}
                  </Button>
                </div>
              ) : null}
              <DataTable
                headers={[
                  "",
                  t("pages.dnsRules.headers.criteria"),
                  t("pages.dnsRules.headers.serverTag"),
                  t("pages.dnsRules.headers.allowDomainRebinding"),
                  t("pages.dnsRules.headers.actions"),
                ]}
                narrowColumns={[0, 1]}
                rows={rules.map((rule, index) => [
                  <div className="flex items-center" key={`enabled-${index}`}>
                    <Switch
                      aria-label={t(
                        (rule.enabled ?? true)
                          ? "pages.dnsRules.actions.disableRule"
                          : "pages.dnsRules.actions.enableRule"
                      )}
                      checked={rule.enabled ?? true}
                      disabled={configMutationPending}
                      onCheckedChange={(checked) =>
                        handleEnabledChange(index, checked)
                      }
                      title={t(
                        (rule.enabled ?? true)
                          ? "pages.dnsRules.actions.disableRule"
                          : "pages.dnsRules.actions.enableRule"
                      )}
                    />
                  </div>,
                  <ul
                    className="list-disc space-y-1 pl-5 text-sm"
                    key={`criteria-${index}`}
                  >
                    <li className="text-muted-foreground">
                      <span className="font-medium text-foreground">
                        {t("pages.dnsRules.criteriaLabels.lists")}:
                      </span>{" "}
                      {rule.list.join(", ")}
                    </li>
                  </ul>,
                  <span className="font-medium" key={`server-${index}`}>
                    {rule.server}
                  </span>,
                  <Badge
                    key={`allow-domain-rebinding-${index}`}
                    variant={
                      rule.allow_domain_rebinding ? "default" : "outline"
                    }
                  >
                    {rule.allow_domain_rebinding
                      ? t("pages.dnsRules.rebinding.enabled")
                      : t("pages.dnsRules.rebinding.disabled")}
                  </Badge>,
                  <ActionButtons
                    actions={[
                      {
                        disabled: configMutationPending,
                        icon: <Pencil className="h-4 w-4" />,
                        label: t("common.edit"),
                        onClick: () => navigate(`/dns-rules/${index}/edit`),
                      },
                      {
                        disabled: configMutationPending,
                        icon: <Trash2 className="h-4 w-4" />,
                        label: t("common.delete"),
                        onClick: () => handleDeleteRule(index),
                      },
                    ]}
                    key={`actions-${index}`}
                  />,
                ])}
                selection={dnsRulesSelectionProps}
              />
            </div>
          )}
        </>
      )}
    </div>
  )
}
