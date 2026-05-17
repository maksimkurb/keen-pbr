import { useQueryClient } from "@tanstack/react-query"
import { ExternalLink, Pencil, Plus, RefreshCw, Trash2 } from "lucide-react"
import { useMemo, useState } from "react"
import { useTranslation } from "react-i18next"
import { toast } from "sonner"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { ConfigStateResponseListRefreshState } from "@/api/generated/model/configStateResponseListRefreshState"
import { usePostConfigMutation, usePostListsRefreshMutation, useConfigMutationPending } from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
import { useGetConfig } from "@/api/queries"
import {
  selectConfig,
  selectConfigIsDraft,
  selectListRefreshState,
} from "@/api/selectors"
import { ActionButtons } from "@/components/shared/action-buttons"
import { BulkSelectionToolbar } from "@/components/shared/bulk-selection-toolbar"
import { ConfigSaveErrorAlert } from "@/components/shared/config-save-error-alert"
import { DataTable, type DataTableSelection } from "@/components/shared/data-table"
import { ListPlaceholder } from "@/components/shared/list-placeholder"
import { PageHeader } from "@/components/shared/page-header"
import { StatsDisplay } from "@/components/shared/stats-display"
import { TableSkeleton } from "@/components/shared/table-skeleton"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { getApiErrorMessage } from "@/lib/api-errors"

type ListDraft = {
  name: string
  ttlMs: string
  domains: string
  ipCidrs: string
  url: string
  file: string
}

type ListTableRow = {
  id: string
  draft: ListDraft
  locationLabel: string
  locationIcon?: "external"
  lastUpdated?: string
  rule: string
  stats?: {
    totalHosts: number
    ipv4Subnets: number
    ipv6Subnets: number
  }
  canRefresh?: boolean
}

const MAX_FAILED_LIST_NAMES_IN_TOAST = 5
const REFRESH_ALL_TARGET = "__all__"

function isRefreshIconActive(
  activeRefreshTarget: string | null,
  bulkRefreshRunning: boolean,
  selectedListIds: ReadonlySet<string>,
  listId: string,
  canRefresh?: boolean,
) {
  if (activeRefreshTarget === REFRESH_ALL_TARGET) {
    return true
  }

  if (activeRefreshTarget === listId) {
    return true
  }

  if (bulkRefreshRunning && canRefresh && selectedListIds.has(listId)) {
    return true
  }

  return false
}


export function ListsPage() {
  const { t } = useTranslation()
  const [, navigate] = useLocation()
  const queryClient = useQueryClient()
  const configMutationPending = useConfigMutationPending()
  const configQuery = useGetConfig()
  const loadedConfig = selectConfig(configQuery.data)
  const isDraft = selectConfigIsDraft(configQuery.data)
  const listRefreshState = selectListRefreshState(configQuery.data)
  const [activeRefreshTarget, setActiveRefreshTarget] = useState<string | null>(
    null,
  )
  const [bulkRefreshRunning, setBulkRefreshRunning] = useState(false)
  const [selectedListIds, setSelectedListIds] = useState<Set<string>>(
    () => new Set(),
  )

  const listRefreshMutation = usePostListsRefreshMutation({
    mutation: {
      onSuccess: async (response, variables) => {
        const requestedName = variables?.data?.name
        const failedLists =
          response.status === 200 &&
          (response.data.status === "partial" || response.data.failed_lists.length > 0)
            ? response.data.failed_lists
            : []
        if (failedLists.length > 0) {
          toast.error(
            failedLists.length === 1
              ? t("pages.lists.messages.refreshFailedOne", {
                  names: failedLists[0],
                })
              : t("pages.lists.messages.refreshFailedMany", {
                  count: failedLists.length,
                  names: formatFailedListNamesForToast(failedLists, t),
                }),
            { richColors: true }
          )
          return
        }

        toast.success(
          requestedName
            ? t("pages.lists.messages.refreshedOne")
            : t("pages.lists.messages.refreshedAll")
        )
      },
      onError: (error) => {
        toast.error(getApiErrorMessage(error as ApiError), { richColors: true })
      },
      onSettled: () => {
        setActiveRefreshTarget(null)
      },
    },
  })

  const tableRows = useMemo(
    () => getTableRowsFromListMap(loadedConfig?.lists, listRefreshState, t),
    [loadedConfig?.lists, listRefreshState, t],
  )

  const validListIdSet = useMemo(
    () => new Set(tableRows.map((row) => row.id)),
    [tableRows],
  )

  const selectedListIdsResolved = useMemo(() => {
    const next = new Set<string>()
    for (const id of selectedListIds) {
      if (validListIdSet.has(id)) {
        next.add(id)
      }
    }

    return next
  }, [selectedListIds, validListIdSet])

  const hasRefreshableLists = tableRows.some((row) => row.canRefresh)
  const refreshDisabled =
    listRefreshMutation.isPending || configMutationPending

  const postConfigMutation = usePostConfigMutation({
    mutation: {
      onSuccess: async () => {
        await Promise.all([
          queryClient.invalidateQueries({ queryKey: queryKeys.config() }),
          queryClient.invalidateQueries({ queryKey: queryKeys.dnsTest() }),
        ])
      },
    },
  })

  const handleDelete = (listId: string) => {
    if (!loadedConfig) {
      return
    }

    const nextConfig = buildUpdatedConfigForListDelete(loadedConfig, listId)
    const hasRouteReferenceUpdates =
      (loadedConfig.route?.rules ?? []).length !==
        (nextConfig.route?.rules ?? []).length ||
      (loadedConfig.route?.rules ?? []).some((rule, index) => {
        const nextRule = nextConfig.route?.rules?.[index]
        return !nextRule || (nextRule.list ?? []).length !== (rule.list ?? []).length
      })
    const hasDnsReferenceUpdates =
      (loadedConfig.dns?.rules ?? []).length !==
        (nextConfig.dns?.rules ?? []).length ||
      (loadedConfig.dns?.rules ?? []).some((rule, index) => {
        const nextRule = nextConfig.dns?.rules?.[index]
        return !nextRule || nextRule.list.length !== rule.list.length
      })

    const deletePrompt =
      hasRouteReferenceUpdates || hasDnsReferenceUpdates
        ? t("pages.lists.delete.confirmWithReferences", { name: listId })
        : t("pages.lists.delete.confirm", { name: listId })

    if (!window.confirm(deletePrompt)) {
      return
    }

    postConfigMutation.mutate({ data: nextConfig })
  }

  const handleRefreshAll = () => {
    if (isDraft) {
      toast.warning(t("pages.lists.refresh.draftBlocked"), { richColors: true })
      return
    }

    setActiveRefreshTarget(REFRESH_ALL_TARGET)
    listRefreshMutation.mutate({ data: {} })
  }

  const handleRefreshOne = (listId: string) => {
    if (isDraft) {
      toast.warning(t("pages.lists.refresh.draftBlocked"), { richColors: true })
      return
    }

    setActiveRefreshTarget(listId)
    listRefreshMutation.mutate({ data: { name: listId } })
  }

  const handleBulkRefreshSelected = async () => {
    if (isDraft) {
      toast.warning(t("pages.lists.refresh.draftBlocked"), { richColors: true })
      return
    }

    const ids = tableRows
      .filter((row) => selectedListIdsResolved.has(row.id) && row.canRefresh)
      .map((row) => row.id)
    if (ids.length === 0) {
      toast.warning(t("pages.lists.bulk.noUrlBacked"), { richColors: true })
      return
    }

    setBulkRefreshRunning(true)
    try {
      for (const name of ids) {
        await listRefreshMutation.mutateAsync({ data: { name } })
      }

      toast.success(
        ids.length <= 1
          ? t("pages.lists.messages.refreshedOne")
          : t("pages.lists.messages.refreshedAll"),
      )
      setSelectedListIds(new Set())
    } catch (error) {
      toast.error(getApiErrorMessage(error as ApiError), { richColors: true })
    } finally {
      setBulkRefreshRunning(false)
    }
  }

  const handleBulkDelete = () => {
    if (!loadedConfig) {
      return
    }

    const ids = [...selectedListIdsResolved]
    const nextConfig = buildUpdatedConfigForListsDelete(loadedConfig, ids)
    const refsChange = listDeletesAltersRoutingOrDnsRefs(loadedConfig, nextConfig)

    const namesLabel = ids.join(", ")
    const confirmed = window.confirm(
      refsChange
        ? t("pages.lists.bulk.confirmDeleteWithRefs", {
            names: namesLabel,
          })
        : t("pages.lists.bulk.confirmDeleteSimple", {
            names: namesLabel,
          }),
    )

    if (!confirmed) {
      return
    }

    postConfigMutation.mutate(
      { data: nextConfig },
      {
        onSuccess: () => {
          setSelectedListIds(new Set())
        },
      },
    )
  }

  const listSelectionProps: DataTableSelection = {
    rowIds: tableRows.map((row) => row.id),
    selectedIds: selectedListIdsResolved,
    selectionDisabled: configMutationPending,
    selectAllAriaLabel: t("common.selection.selectAll"),
    selectRowAriaLabel: (rowId: string) =>
      t("common.selection.selectRow", { rowLabel: rowId }),
    selectAllTooltip: t("common.selection.selectAll"),
    onToggleRow: (rowId: string) => {
      setSelectedListIds((previous) => {
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
      if (selectedAll) {
        setSelectedListIds(new Set(tableRows.map((row) => row.id)))
      } else {
        setSelectedListIds(new Set())
      }
    },
  }

  return (
    <div className="space-y-6">
      <PageHeader
        actions={
          <div className="flex flex-wrap justify-end gap-2">
            {hasRefreshableLists ? (
              <Button
                disabled={refreshDisabled}
                onClick={handleRefreshAll}
                variant="outline"
              >
                <RefreshCw
                  className={`mr-1 h-4 w-4 ${
                    activeRefreshTarget === REFRESH_ALL_TARGET
                      ? "animate-spin"
                      : ""
                  }`}
                />
                {t("pages.lists.actions.updateAll")}
              </Button>
            ) : null}
            <Button disabled={configMutationPending} onClick={() => navigate("/lists/create")}>
              <Plus className="mr-1 h-4 w-4" />
              {t("pages.lists.actions.new")}
            </Button>
          </div>
        }
        description={t("pages.lists.description")}
        title={t("pages.lists.title")}
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
      ) : tableRows.length === 0 ? (
        <ListPlaceholder
          description={t("pages.lists.empty.description")}
          title={t("pages.lists.empty.title")}
        />
      ) : (
        <div className="space-y-3">
          {selectedListIdsResolved.size > 0 ? (
            <BulkSelectionToolbar
              countLabel={t("pages.lists.bulk.selected", {
                count: selectedListIdsResolved.size,
              })}
            >
              {hasRefreshableLists ? (
                <Button
                  disabled={
                    refreshDisabled ||
                    bulkRefreshRunning ||
                    isDraft ||
                    configMutationPending ||
                    !tableRows.some(
                      (row) =>
                        selectedListIdsResolved.has(row.id) &&
                        Boolean(row.canRefresh),
                    )
                  }
                  onClick={() => void handleBulkRefreshSelected()}
                  size="sm"
                  variant="outline"
                >
                  <RefreshCw
                    className={`mr-1 h-4 w-4 ${
                      bulkRefreshRunning ? "animate-spin" : ""
                    }`}
                  />
                  {t("pages.lists.bulk.refreshSelected")}
                </Button>
              ) : null}
              <Button
                disabled={configMutationPending}
                onClick={() => handleBulkDelete()}
                size="sm"
                variant="destructive"
              >
                <Trash2 className="mr-1 h-4 w-4" />
                {t("pages.lists.bulk.deleteSelected")}
              </Button>
            </BulkSelectionToolbar>
          ) : null}
          <DataTable
            headers={[
              t("pages.lists.headers.name"),
              t("pages.lists.headers.type"),
              t("pages.lists.headers.stats"),
              t("pages.lists.headers.rules"),
              t("pages.lists.headers.actions"),
            ]}
            narrowColumns={[0]}
            rows={tableRows.map((list) => [
            <div className="space-y-1" key={`${list.id}-name`}>
              <div className="flex items-center gap-2 font-medium">
                {list.draft.name}
                {list.locationIcon === "external" ? (
                  <a
                    aria-label={list.locationLabel}
                    className="text-muted-foreground transition-colors hover:text-foreground"
                    href={list.draft.url}
                    rel="noreferrer"
                    target="_blank"
                  >
                    <ExternalLink className="h-3 w-3" />
                  </a>
                ) : null}
              </div>
              <div className="text-sm text-muted-foreground md:text-xs">
                {list.locationLabel}
              </div>
              {list.canRefresh ? (
                <div className="text-sm text-muted-foreground md:text-xs">
                  {t("pages.lists.lastUpdated", {
                    value: formatLastUpdatedLabel(
                      list.lastUpdated,
                      t("pages.lists.neverUpdated")
                    ),
                  })}
                </div>
              ) : null}
            </div>,
            <Badge key={`${list.id}-type`} variant="outline">
              {getListSourceLabel(list.draft, t)}
            </Badge>,
            list.stats ? (
              <StatsDisplay
                ipv4Subnets={list.stats.ipv4Subnets}
                ipv6Subnets={list.stats.ipv6Subnets}
                key={`${list.id}-stats`}
                totalHosts={list.stats.totalHosts}
              />
            ) : (
              <span
                className="text-sm text-muted-foreground"
                key={`${list.id}-stats-empty`}
              >
                {t("pages.lists.noStats")}
              </span>
            ),
            <Badge key={`${list.id}-rule`} variant="outline">
              {list.rule}
            </Badge>,
            <ActionButtons
              actions={[
                ...(list.canRefresh
                  ? [
                      {
                        disabled:
                          refreshDisabled ||
                          bulkRefreshRunning ||
                          configMutationPending,
                        icon: (
                          <RefreshCw
                            className={`h-4 w-4 ${
                              isRefreshIconActive(
                                activeRefreshTarget,
                                bulkRefreshRunning,
                                selectedListIdsResolved,
                                list.id,
                                list.canRefresh,
                              )
                                ? "animate-spin"
                                : ""
                            }`}
                          />
                        ),
                        label: t("pages.lists.actions.update"),
                        onClick: () => handleRefreshOne(list.id),
                      },
                    ]
                  : []),
                {
                  disabled: configMutationPending,
                  icon: <Pencil className="h-4 w-4" />,
                  label: t("common.edit"),
                  onClick: () => navigate(`/lists/${list.id}/edit`),
                },
                {
                  disabled: configMutationPending,
                  icon: <Trash2 className="h-4 w-4" />,
                  label: t("common.delete"),
                  onClick: () => handleDelete(list.id),
                },
              ]}
              key={`${list.id}-actions`}
            />,
          ])}
            selection={listSelectionProps}
          />
        </div>
      )}
    </div>
  )
}

function formatFailedListNamesForToast(
  names: string[],
  t: ReturnType<typeof useTranslation>["t"]
) {
  const visibleNames = names.slice(0, MAX_FAILED_LIST_NAMES_IN_TOAST)
  const hiddenCount = names.length - visibleNames.length
  const label = visibleNames.join(", ")

  if (hiddenCount <= 0) {
    return label
  }

  return `${label}, ${t("pages.lists.messages.refreshFailedMore", {
    count: hiddenCount,
  })}`
}

function getTableRowsFromListMap(
  lists: ConfigObject["lists"],
  listRefreshState: ConfigStateResponseListRefreshState,
  t: (key: string) => string
): ListTableRow[] {
  return Object.entries(lists ?? {}).map(([name, listConfig]) => {
    const domains = listConfig.domains ?? []
    const ipCidrs = listConfig.ip_cidrs ?? []
    const showInlineStats = !listConfig.url && !listConfig.file

    return {
      id: name,
      draft: {
        name,
        ttlMs: String(listConfig.ttl_ms ?? 0),
        domains: domains.join("\n"),
        ipCidrs: ipCidrs.join("\n"),
        url: listConfig.url ?? "",
        file: listConfig.file ?? "",
      },
      locationLabel:
        listConfig.url || listConfig.file || t("pages.lists.location.inline"),
      locationIcon: listConfig.url ? "external" : undefined,
      lastUpdated: listRefreshState[name]?.last_updated,
      rule: t("pages.lists.rule.configured"),
      stats: showInlineStats
        ? {
            totalHosts: domains.length + ipCidrs.length,
            ipv4Subnets: ipCidrs.filter((value) => value.includes(".")).length,
            ipv6Subnets: ipCidrs.filter((value) => value.includes(":")).length,
          }
        : undefined,
      canRefresh: Boolean(listConfig.url),
    }
  })
}

function buildUpdatedConfigForListsDelete(
  config: ConfigObject,
  listIds: string[],
): ConfigObject {
  return listIds.reduce(
    (acc, id) => buildUpdatedConfigForListDelete(acc, id),
    config,
  )
}

function listDeletesAltersRoutingOrDnsRefs(
  before: ConfigObject,
  after: ConfigObject,
): boolean {
  return (
    JSON.stringify(before.route?.rules ?? []) !==
      JSON.stringify(after.route?.rules ?? []) ||
    JSON.stringify(before.dns?.rules ?? []) !==
      JSON.stringify(after.dns?.rules ?? [])
  )
}

function buildUpdatedConfigForListDelete(
  config: ConfigObject,
  listId: string
): ConfigObject {
  const nextLists = { ...(config.lists ?? {}) }
  delete nextLists[listId]

  return {
    ...config,
    lists: nextLists,
    route: {
      ...config.route,
      rules: (config.route?.rules ?? [])
        .map((rule) => ({
          ...rule,
          list: (rule.list ?? []).filter((name) => name !== listId),
        }))
        .filter((rule) => rule.list.length > 0),
    },
    dns: {
      ...config.dns,
      rules: (config.dns?.rules ?? [])
        .map((rule) => ({
          ...rule,
          list: rule.list.filter((name) => name !== listId),
        }))
        .filter((rule) => rule.list.length > 0),
    },
  }
}


function getListSourceLabel(
  draft: ListDraft,
  t: (key: string) => string
) {
  const sources = [
    draft.url ? "url" : null,
    draft.file ? "file" : null,
    draft.domains ? "domains" : null,
    draft.ipCidrs ? "ip_cidrs" : null,
  ].filter(Boolean)

  if (sources.length === 0) {
    return t("pages.lists.source.empty")
  }

  return sources.map((source) => t(`pages.lists.source.${source}`)).join(", ")
}

function formatLastUpdatedLabel(value: string | undefined, fallback: string) {
  if (!value) {
    return fallback
  }

  const parsedDate = new Date(value)
  if (Number.isNaN(parsedDate.getTime())) {
    return value
  }

  return new Intl.DateTimeFormat(undefined, {
    dateStyle: "medium",
    timeStyle: "short",
  }).format(parsedDate)
}
