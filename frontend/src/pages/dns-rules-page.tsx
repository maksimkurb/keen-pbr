import { Pencil, Plus, Trash2 } from "lucide-react"
import { useMemo } from "react"
import { useTranslation } from "react-i18next"
import { toast } from "sonner"

import { useQueryClient } from "@tanstack/react-query"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import {
  useConfigMutationPending,
  usePostConfigMutation,
} from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
import { useGetConfig } from "@/api/queries"
import { selectConfig } from "@/api/selectors"
import { ActionButtons } from "@/components/shared/action-buttons"
import { BulkSelectionToolbar } from "@/components/shared/bulk-selection-toolbar"
import { ConfigSaveErrorAlert } from "@/components/shared/config-save-error-alert"
import { DataTable } from "@/components/shared/data-table"
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
import { useRowSelection } from "@/hooks/use-row-selection"
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
  const configMutationPending = useConfigMutationPending()
  const configQuery = useGetConfig()

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

  const rules = loadedConfig?.dns?.rules ?? []
  const ruleRowIds = rules.map((_rule, index) => String(index))
  const ruleSelection = useRowSelection(ruleRowIds)

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

  const persistDnsRules = (
    nextDraftRules: ReturnType<typeof getRuleDraft>[],
    options?: { clearSelection?: boolean }
  ) => {
    if (!loadedConfig) {
      return
    }

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
          nextDraftRules
        ),
      },
      options?.clearSelection
        ? {
            onSuccess: () => {
              ruleSelection.clear()
            },
          }
        : undefined
    )
  }

  const handleBulkDeleteRules = () => {
    if (ruleSelection.selectedCount === 0) {
      return
    }

    if (
      !window.confirm(
        t("pages.dnsRules.bulk.confirmDelete", {
          count: ruleSelection.selectedCount,
        })
      )
    ) {
      return
    }

    persistDnsRules(
      rules
        .filter((_rule, index) => !ruleSelection.selectedIds.has(String(index)))
        .map((rule) => getRuleDraft(rule)),
      { clearSelection: true }
    )
  }

  const handleBulkSetEnabled = (enabled: boolean) => {
    if (ruleSelection.selectedCount === 0) {
      return
    }

    persistDnsRules(
      rules.map((rule, index) => ({
        ...getRuleDraft(rule),
        enabled: ruleSelection.selectedIds.has(String(index))
          ? enabled
          : (rule.enabled ?? true),
      }))
    )
  }

  return (
    <div className="space-y-6">
      <PageHeader
        actions={
          <Button
            disabled={configMutationPending}
            onClick={() => navigate("/dns-rules/create")}
          >
            <Plus className="mr-1 h-4 w-4" />
            {t("pages.dnsRules.actions.add")}
          </Button>
        }
        description={t("pages.dnsRules.description")}
        title={t("pages.dnsRules.title")}
      />

      <ConfigSaveErrorAlert error={postConfigMutation.error} />

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
          <Card>
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
                    placeholderTitle={t(
                      "pages.dnsRules.fallback.placeholderTitle"
                    )}
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
              {ruleSelection.hasSelection ? (
                <BulkSelectionToolbar
                  countLabel={t("pages.dnsRules.bulk.selected", {
                    count: ruleSelection.selectedCount,
                  })}
                >
                  <Button
                    disabled={configMutationPending}
                    onClick={() => handleBulkSetEnabled(true)}
                    size="sm"
                    variant="outline"
                  >
                    {t("pages.dnsRules.bulk.enable")}
                  </Button>
                  <Button
                    disabled={configMutationPending}
                    onClick={() => handleBulkSetEnabled(false)}
                    size="sm"
                    variant="outline"
                  >
                    {t("pages.dnsRules.bulk.disable")}
                  </Button>
                  <Button
                    disabled={configMutationPending}
                    onClick={handleBulkDeleteRules}
                    size="sm"
                    variant="destructive"
                  >
                    <Trash2 className="mr-1 h-4 w-4" />
                    {t("pages.dnsRules.bulk.delete")}
                  </Button>
                </BulkSelectionToolbar>
              ) : null}
              <DataTable
                headers={[
                  "",
                  t("pages.dnsRules.headers.criteria"),
                  t("pages.dnsRules.headers.serverTag"),
                  t("pages.dnsRules.headers.allowDomainRebinding"),
                  t("pages.dnsRules.headers.actions"),
                ]}
                narrowColumns={[0]}
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
                selection={{
                  rowIds: ruleRowIds,
                  selectedIds: ruleSelection.selectedIds,
                  disabled: configMutationPending,
                  onToggle: ruleSelection.toggleOne,
                  onToggleAll: ruleSelection.setAllVisible,
                  selectAllLabel: t("common.selection.selectAll"),
                  getRowLabel: (rowId) =>
                    t("common.selection.selectRow", {
                      rowLabel: `${t("pages.dnsRules.title")} #${Number(rowId) + 1}`,
                    }),
                }}
              />
            </div>
          )}
        </>
      )}
    </div>
  )
}
