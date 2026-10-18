import { DeleteImpactDialog } from "@/components/shared/delete-impact-dialog"
import { DnsManagementDisabled } from "@/components/shared/dns-management-disabled"
import { Pencil, Plus, Trash2 } from "lucide-react"
import { useMemo, useState } from "react"
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
import { effectiveResolverIntegration, selectConfig } from "@/api/selectors"
import { ActionButtons } from "@/components/shared/action-buttons"
import { MobileAddFab } from "@/components/shared/mobile-add-fab"
import { BulkSelectionToolbar } from "@/components/shared/bulk-selection-toolbar"
import { DataTable } from "@/components/shared/data-table"
import { ListPlaceholder } from "@/components/shared/list-placeholder"
import { PageHeader } from "@/components/shared/page-header"
import { TableSkeleton } from "@/components/shared/table-skeleton"
import { useRowSelection } from "@/hooks/use-row-selection"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Switch } from "@/components/ui/switch"
import { getApiErrorMessage } from "@/lib/api-errors"
import {
  buildUpdatedConfigWithRules,
  getRuleDraft,
  setDnsRuleEnabled,
  validateRules,
} from "@/pages/dns-rules-utils"
import { DnsServerListsAccordion } from "@/pages/dns-servers-lists"
import {
  buildUpdatedConfigWithServerList,
  getBootstrapCandidateTags,
  getDnsServerListWarnings,
} from "@/pages/dns-servers-utils"

export function DnsRulesPage() {
  const { t } = useTranslation()
  const queryClient = useQueryClient()
  const [, navigate] = useLocation()
  const [deleteIds, setDeleteIds] = useState<string[]>([])
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

  const resolverIntegration = effectiveResolverIntegration(loadedConfig)

  const fallback = loadedConfig?.dns?.fallback ?? []
  const bootstrap = loadedConfig?.dns?.bootstrap ?? []
  const bootstrapOptions = getBootstrapCandidateTags(
    loadedConfig?.dns?.servers ?? []
  )
  const listWarnings = getDnsServerListWarnings(loadedConfig)

  const saveServerList = (key: "fallback" | "bootstrap", tags: string[]) => {
    if (!loadedConfig) {
      return
    }

    const knownTags = key === "bootstrap" ? bootstrapOptions : serverTags
    if (tags.some((tag) => !knownTags.includes(tag))) {
      toast.error(
        t(
          key === "bootstrap"
            ? "pages.dnsServers.validation.invalidBootstrap"
            : "pages.dnsServers.validation.invalidFallback"
        ),
        { richColors: true }
      )
      return
    }

    const draftRules = rules.map((rule) => getRuleDraft(rule))
    if (
      Object.keys(validateRules(draftRules, serverTags, listOptions)).length > 0
    ) {
      toast.error(t("pages.dnsServers.validation.invalidFallbackChange"), {
        richColors: true,
      })
      return
    }

    postConfigMutation.mutate({
      data: buildUpdatedConfigWithServerList(loadedConfig, key, tags),
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
      data: buildUpdatedConfigWithRules(loadedConfig, nextDraftRules),
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
      data: buildUpdatedConfigWithRules(loadedConfig, nextDraftRules),
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
        data: buildUpdatedConfigWithRules(loadedConfig, nextDraftRules),
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
    if (!configMutationPending && ruleSelection.selectedCount > 0) {
      setDeleteIds([...ruleSelection.selectedIds])
    }
  }

  const confirmBulkDelete = () => {
    if (!loadedConfig || configMutationPending || deleteIds.length === 0) return
    persistDnsRules(
      rules
        .filter((_rule, index) => !deleteIds.includes(String(index)))
        .map((rule) => getRuleDraft(rule)),
      { clearSelection: true }
    )
    setDeleteIds([])
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
    <div className="space-y-6 pb-20 md:pb-0">
      <DeleteImpactDialog
        open={deleteIds.length > 0}
        onOpenChange={(open) => {
          if (!open) setDeleteIds([])
        }}
        onConfirm={confirmBulkDelete}
        isPending={configMutationPending}
        title={t("pages.dnsRules.bulk.confirmDelete", {
          count: deleteIds.length,
        })}
        description={t("pages.dnsRules.bulk.deleteConsequences")}
        confirmLabel={t("pages.dnsRules.bulk.delete", {
          count: deleteIds.length,
        })}
        impactItems={deleteIds.map((id) => ({
          label: t("common.ruleNumber", { number: Number(id) + 1 }),
        }))}
      />
      <PageHeader
        actions={
          resolverIntegration === "dnsmasq" ? (
            <>
              <Button
                className="hidden md:inline-flex"
                disabled={configMutationPending}
                onClick={() => navigate("/dns-rules/create")}
              >
                <Plus className="mr-1 h-4 w-4" />
                {t("pages.dnsRules.actions.add")}
              </Button>
              <MobileAddFab
                disabled={configMutationPending}
                icon={<Plus className="size-4" />}
                onClick={() => navigate("/dns-rules/create")}
              />
            </>
          ) : null
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
      ) : resolverIntegration !== "dnsmasq" ? (
        <DnsManagementDisabled />
      ) : (
        <>
          <DnsServerListsAccordion
            bootstrap={bootstrap}
            bootstrapOptions={bootstrapOptions}
            dohWithoutIps={listWarnings.dohWithoutIps}
            fallback={fallback}
            fallbackWarning={listWarnings.fallback}
            onBootstrapChange={(tags) => saveServerList("bootstrap", tags)}
            onFallbackChange={(tags) => saveServerList("fallback", tags)}
            serverTags={serverTags}
          />
          {rules.length === 0 ? (
            <ListPlaceholder
              description={t("pages.dnsRules.empty.description")}
              title={t("pages.dnsRules.empty.title")}
            />
          ) : (
            <div className="space-y-3">
              {ruleSelection.isSelecting ? (
                <BulkSelectionToolbar
                  selection={ruleSelection}
                  disabled={configMutationPending}
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
                    {t("pages.dnsRules.bulk.enable", {
                      count: ruleSelection.selectedCount,
                    })}
                  </Button>
                  <Button
                    disabled={configMutationPending}
                    onClick={() => handleBulkSetEnabled(false)}
                    size="sm"
                    variant="outline"
                  >
                    {t("pages.dnsRules.bulk.disable", {
                      count: ruleSelection.selectedCount,
                    })}
                  </Button>
                  <Button
                    disabled={configMutationPending}
                    onClick={handleBulkDeleteRules}
                    size="sm"
                    variant="destructive"
                  >
                    <Trash2 className="mr-1 h-4 w-4" />
                    {t("pages.dnsRules.bulk.delete", {
                      count: ruleSelection.selectedCount,
                    })}
                  </Button>
                </BulkSelectionToolbar>
              ) : null}
              <DataTable
                mobileCards={{
                  titleColumns: [0, 2],
                  bodyColumns: [1, 3],
                  hideLabels: [3],
                }}
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
                    {rule.servers.join(", ")}
                  </span>,
                  <Badge
                    key={`allow-domain-rebinding-${index}`}
                    variant={
                      rule.allow_domain_rebinding ? "warning" : "outline"
                    }
                  >
                    {rule.allow_domain_rebinding
                      ? t("pages.dnsRules.rebinding.enabled")
                      : t("pages.dnsRules.rebinding.disabled")}
                  </Badge>,
                  <ActionButtons
                    mobileIcons
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
                        destructive: true,
                        label: t("common.delete"),
                        onClick: () => handleDeleteRule(index),
                      },
                    ]}
                    key={`actions-${index}`}
                  />,
                ])}
                selection={{
                  isSelecting: ruleSelection.isSelecting,
                  onStartSelecting: ruleSelection.startSelecting,
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
