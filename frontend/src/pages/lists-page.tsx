import { useQueryClient } from "@tanstack/react-query"
import {
  ArrowRight,
  ExternalLink,
  Pencil,
  Plus,
  RefreshCw,
  Trash2,
} from "lucide-react"
import type { ReactNode } from "react"
import { useMemo, useState } from "react"
import { useTranslation } from "react-i18next"
import { toast } from "sonner"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { ConfigStateResponseListRefreshState } from "@/api/generated/model/configStateResponseListRefreshState"
import type { DnsRule } from "@/api/generated/model/dnsRule"
import type { RouteRule } from "@/api/generated/model/routeRule"
import {
  useConfigMutationPending,
  usePostConfigMutation,
  usePostListsRefreshMutation,
} from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
import { useGetConfig } from "@/api/queries"
import {
  selectConfig,
  selectConfigIsDraft,
  selectListRefreshState,
} from "@/api/selectors"
import { ActionButtons } from "@/components/shared/action-buttons"
import { MobileAddFab } from "@/components/shared/mobile-add-fab"
import { BulkSelectionToolbar } from "@/components/shared/bulk-selection-toolbar"
import { ConfigSaveErrorAlert } from "@/components/shared/config-save-error-alert"
import { DataTable } from "@/components/shared/data-table"
import {
  DeleteImpactDialog,
  type DeleteImpactItem,
} from "@/components/shared/delete-impact-dialog"
import { ListPlaceholder } from "@/components/shared/list-placeholder"
import { PageHeader } from "@/components/shared/page-header"
import { StatsDisplay } from "@/components/shared/stats-display"
import { TableSkeleton } from "@/components/shared/table-skeleton"
import { useRowSelection } from "@/hooks/use-row-selection"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { getRulesUsingList } from "@/lib/list-usage"
import { getApiErrorMessage } from "@/lib/api-errors"
import {
  buildUpdatedConfigForListsDelete,
  getListDeleteImpact,
  type ListDeleteImpact,
  splitListSourceUrl,
} from "@/pages/lists-utils"

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
  ruleCount: number
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
  selectedIds: ReadonlySet<string>,
  listId: string
) {
  return (
    activeRefreshTarget === REFRESH_ALL_TARGET ||
    activeRefreshTarget === listId ||
    (bulkRefreshRunning && selectedIds.has(listId))
  )
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
    null
  )
  const [bulkRefreshRunning, setBulkRefreshRunning] = useState(false)
  const [deleteRequest, setDeleteRequest] = useState<{
    ids: string[]
    impact: ListDeleteImpact
    config: ConfigObject
    clearSelectionOnSuccess: boolean
  } | null>(null)
  const [deletePreview, setDeletePreview] = useState<typeof deleteRequest>(null)
  const visibleDeleteRequest = deleteRequest ?? deletePreview

  const listRefreshMutation = usePostListsRefreshMutation({
    mutation: {
      onSuccess: async (response, variables) => {
        const requestedName = variables?.data?.name
        const failedLists =
          response.status === 200 ? response.data.failed_lists : []
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
    () => getTableRowsFromListMap(loadedConfig, listRefreshState, t),
    [loadedConfig, listRefreshState, t]
  )
  const listRowIds = tableRows.map((row) => row.id)
  const listSelection = useRowSelection(listRowIds)
  const hasRefreshableLists = tableRows.some((row) => row.canRefresh)
  const selectedRefreshableLists = tableRows.filter(
    (row) => listSelection.selectedIds.has(row.id) && row.canRefresh
  )
  const refreshDisabled =
    listRefreshMutation.isPending || bulkRefreshRunning || configMutationPending

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

    const request = {
      ids: [listId],
      impact: getListDeleteImpact(loadedConfig, [listId]),
      config: loadedConfig,
      clearSelectionOnSuccess: false,
    }
    setDeletePreview(request)
    setDeleteRequest(request)
  }

  const handleBulkDelete = () => {
    if (!loadedConfig || listSelection.selectedCount === 0) {
      return
    }

    const listIds = [...listSelection.selectedIds]
    const request = {
      ids: listIds,
      impact: getListDeleteImpact(loadedConfig, listIds),
      config: loadedConfig,
      clearSelectionOnSuccess: true,
    }
    setDeletePreview(request)
    setDeleteRequest(request)
  }

  const confirmDelete = () => {
    if (!loadedConfig || !deleteRequest) {
      return
    }

    postConfigMutation.mutate(
      {
        data: buildUpdatedConfigForListsDelete(loadedConfig, deleteRequest.ids),
      },
      {
        onSuccess: () => {
          if (deleteRequest.clearSelectionOnSuccess) {
            listSelection.clear()
          }
          setDeleteRequest(null)
        },
      }
    )
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

    if (selectedRefreshableLists.length === 0) {
      toast.warning(t("pages.lists.bulk.noUrlBacked"), { richColors: true })
      return
    }

    setBulkRefreshRunning(true)
    try {
      for (const list of selectedRefreshableLists) {
        await listRefreshMutation.mutateAsync({ data: { name: list.id } })
      }
      listSelection.clear()
    } finally {
      setBulkRefreshRunning(false)
    }
  }

  return (
    <div className="space-y-6 pb-20 md:pb-0">
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
            <Button
              className="hidden md:inline-flex"
              disabled={configMutationPending}
              onClick={() => navigate("/lists/create")}
            >
              <Plus className="mr-1 h-4 w-4" />
              {t("pages.lists.actions.new")}
            </Button>
            <MobileAddFab
              disabled={configMutationPending}
              icon={<Plus className="size-4" />}
              onClick={() => navigate("/lists/create")}
            />
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
          {listSelection.isSelecting ? (
            <BulkSelectionToolbar
              selection={listSelection}
              disabled={configMutationPending}
              countLabel={t("pages.lists.bulk.selected", {
                count: listSelection.selectedCount,
              })}
            >
              {hasRefreshableLists ? (
                <Button
                  disabled={
                    refreshDisabled || selectedRefreshableLists.length === 0
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
                  {t("pages.lists.bulk.refreshSelected", {
                    count: selectedRefreshableLists.length,
                  })}
                </Button>
              ) : null}
              <Button
                disabled={configMutationPending}
                onClick={handleBulkDelete}
                size="sm"
                variant="destructive"
              >
                <Trash2 className="mr-1 h-4 w-4" />
                {t("pages.lists.bulk.deleteSelected", {
                  count: listSelection.selectedCount,
                })}
              </Button>
            </BulkSelectionToolbar>
          ) : null}
          <DataTable
            mobileCards={{
              titleColumns: [0, 1],
              bodyColumns: [2, 3],
              hideLabels: [3],
              beforeBody: (index) => (
                <ListSource list={tableRows[index]} t={t} />
              ),
            }}
            headers={[
              t("pages.lists.headers.name"),
              t("pages.lists.headers.type"),
              t("pages.lists.headers.stats"),
              t("pages.lists.headers.rules"),
              t("pages.lists.headers.actions"),
            ]}
            rows={tableRows.map((list) => [
              <div className="md:space-y-1" key={`${list.id}-name`}>
                <div className="flex items-center gap-2 font-medium">
                  {list.draft.name}
                  {list.locationIcon === "external" ? (
                    <a
                      aria-label={list.locationLabel}
                      className="hidden text-muted-foreground transition-colors hover:text-foreground md:inline-flex"
                      href={list.draft.url}
                      rel="noreferrer"
                      target="_blank"
                    >
                      <ExternalLink className="h-3 w-3" />
                    </a>
                  ) : null}
                </div>
                <div className="hidden text-sm text-muted-foreground md:block md:text-xs">
                  {list.locationLabel}
                </div>
                {list.canRefresh ? (
                  <div className="hidden text-sm text-muted-foreground md:block md:text-xs">
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
              <div key={`${list.id}-rule`}>
                <span className="md:hidden">
                  {t("pages.lists.rule.used", { count: list.ruleCount })}
                </span>
                <span className="hidden md:inline">{list.ruleCount}</span>
              </div>,
              <ActionButtons
                mobileIcons
                actions={[
                  ...(list.canRefresh
                    ? [
                        {
                          disabled: refreshDisabled,
                          icon: (
                            <RefreshCw
                              className={`h-4 w-4 ${
                                isRefreshIconActive(
                                  activeRefreshTarget,
                                  bulkRefreshRunning,
                                  listSelection.selectedIds,
                                  list.id
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
                    destructive: true,
                    label: t("common.delete"),
                    onClick: () => handleDelete(list.id),
                  },
                ]}
                key={`${list.id}-actions`}
              />,
            ])}
            selection={{
              isSelecting: listSelection.isSelecting,
              onStartSelecting: listSelection.startSelecting,
              rowIds: listRowIds,
              selectedIds: listSelection.selectedIds,
              disabled: configMutationPending,
              onToggle: listSelection.toggleOne,
              onToggleAll: listSelection.setAllVisible,
              selectAllLabel: t("common.selection.selectAll"),
              getRowLabel: (rowId) =>
                t("common.selection.selectRow", { rowLabel: rowId }),
            }}
          />
        </div>
      )}
      <DeleteImpactDialog
        confirmLabel={t("pages.lists.deleteDialog.confirm")}
        description={t("pages.lists.deleteDialog.description", {
          names: visibleDeleteRequest?.ids.join(", ") ?? "",
        })}
        impactItems={
          visibleDeleteRequest
            ? getListDeleteImpactItems(
                visibleDeleteRequest.config,
                visibleDeleteRequest.ids,
                visibleDeleteRequest.impact,
                t
              )
            : []
        }
        isPending={postConfigMutation.isPending}
        onConfirm={confirmDelete}
        onOpenChange={(open) => {
          if (!open && !postConfigMutation.isPending) {
            setDeleteRequest(null)
          }
        }}
        open={deleteRequest !== null}
        title={t("pages.lists.deleteDialog.title")}
      />
    </div>
  )
}

function getListDeleteImpactItems(
  config: ConfigObject | undefined,
  listIds: string[],
  impact: ListDeleteImpact,
  t: (key: string, options?: Record<string, unknown>) => string
) {
  const items: DeleteImpactItem[] = []
  const deletedListIds = new Set(listIds)

  for (const listId of listIds) {
    items.push({
      label: (
        <>
          {t("pages.lists.deleteDialog.items.listPrefix")}{" "}
          <strong>{listId}</strong>{" "}
          {t("pages.lists.deleteDialog.items.listSuffix")}
        </>
      ),
    })
  }

  for (const index of impact.removedRouteRuleIndexes) {
    const rule = config?.route?.rules?.[index]
    items.push({
      label: t("pages.lists.deleteDialog.items.routeRuleRemoved", {
        number: index + 1,
      }),
      details: getRouteRuleDetails(rule, deletedListIds, true, t),
    })
  }

  for (const index of impact.routeRuleIndexes) {
    if (impact.removedRouteRuleIndexes.includes(index)) {
      continue
    }
    const rule = config?.route?.rules?.[index]
    items.push({
      label: t("pages.lists.deleteDialog.items.routeRuleUpdated", {
        number: index + 1,
      }),
      details: getRouteRuleDetails(rule, deletedListIds, false, t),
    })
  }

  for (const index of impact.removedDnsRuleIndexes) {
    const rule = config?.dns?.rules?.[index]
    items.push({
      label: t("pages.lists.deleteDialog.items.dnsRuleRemoved", {
        number: index + 1,
      }),
      details: getDnsRuleDetails(rule, deletedListIds, true, t),
    })
  }

  for (const index of impact.dnsRuleIndexes) {
    if (impact.removedDnsRuleIndexes.includes(index)) {
      continue
    }
    const rule = config?.dns?.rules?.[index]
    items.push({
      label: t("pages.lists.deleteDialog.items.dnsRuleUpdated", {
        number: index + 1,
      }),
      details: getDnsRuleDetails(rule, deletedListIds, false, t),
    })
  }

  return items
}

function getRouteRuleDetails(
  rule: RouteRule | undefined,
  deletedListIds: ReadonlySet<string>,
  isRemoved: boolean,
  t: (key: string, options?: Record<string, unknown>) => string
) {
  if (!rule) {
    return []
  }

  const beforeLists = rule.list ?? []
  const afterLists = beforeLists.filter((name) => !deletedListIds.has(name))
  const details: ReactNode[] = []

  if (beforeLists.length > 0) {
    details.push(
      formatDetail(
        t("pages.routingRules.criteriaLabels.lists"),
        isRemoved
          ? formatListValue(beforeLists, t)
          : formatTransition(beforeLists, afterLists, t)
      )
    )
  }

  appendOptionalDetail(
    details,
    t("pages.routingRules.criteriaLabels.proto"),
    rule.proto
  )
  appendOptionalDetail(
    details,
    t("pages.routingRules.criteriaLabels.dscp"),
    rule.dscp?.toString()
  )
  appendOptionalDetail(
    details,
    t("pages.routingRules.criteriaLabels.sourceIp"),
    rule.src_addr
  )
  appendOptionalDetail(
    details,
    t("pages.routingRules.criteriaLabels.destinationIp"),
    rule.dest_addr
  )
  appendOptionalDetail(
    details,
    t("pages.routingRules.criteriaLabels.sourcePort"),
    rule.src_port
  )
  appendOptionalDetail(
    details,
    t("pages.routingRules.criteriaLabels.destinationPort"),
    rule.dest_port
  )

  return details
}

function getDnsRuleDetails(
  rule: DnsRule | undefined,
  deletedListIds: ReadonlySet<string>,
  isRemoved: boolean,
  t: (key: string, options?: Record<string, unknown>) => string
) {
  if (!rule) {
    return []
  }

  const afterLists = rule.list.filter((name) => !deletedListIds.has(name))

  return [
    formatDetail(
      t("pages.dnsRules.criteriaLabels.lists"),
      isRemoved
        ? formatListValue(rule.list, t)
        : formatTransition(rule.list, afterLists, t)
    ),
    formatDetail(t("pages.dnsRules.headers.serverTag"), rule.server),
  ]
}

function appendOptionalDetail(
  details: ReactNode[],
  label: string,
  value: string | undefined
) {
  if (typeof value !== "string" || value.trim().length === 0) {
    return
  }

  details.push(formatDetail(label, value))
}

function formatDetail(label: string, value: ReactNode) {
  return (
    <>
      {label}: {value}
    </>
  )
}

function formatTransition(
  before: string[],
  after: string[],
  t: (key: string, options?: Record<string, unknown>) => string
) {
  return (
    <ChangeValue
      after={formatListValue(after, t)}
      before={formatListValue(before, t)}
    />
  )
}

function ChangeValue({ after, before }: { after: string; before: string }) {
  return (
    <span className="inline-flex min-w-0 items-center gap-1 leading-4">
      <span className="min-w-0 truncate">{before}</span>
      <ArrowRight className="mt-px size-3 shrink-0" />
      <span className="min-w-0 truncate">{after}</span>
    </span>
  )
}

function formatListValue(
  values: string[],
  t: (key: string, options?: Record<string, unknown>) => string
) {
  return values.length > 0 ? values.join(", ") : t("common.noneShort")
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
  config: ConfigObject | undefined,
  listRefreshState: ConfigStateResponseListRefreshState,
  t: (key: string) => string
): ListTableRow[] {
  return Object.entries(config?.lists ?? {}).map(([name, listConfig]) => {
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
      ruleCount:
        getRulesUsingList(
          name,
          config?.route?.rules ?? [],
          (rule) => rule.list,
          (rule) => rule.outbound
        ).length +
        getRulesUsingList(
          name,
          config?.dns?.rules ?? [],
          (rule) => rule.list,
          (rule) => rule.server
        ).length,
      stats: showInlineStats
        ? {
            totalHosts: domains.length,
            ipv4Subnets: ipCidrs.filter((value) => value.includes(".")).length,
            ipv6Subnets: ipCidrs.filter((value) => value.includes(":")).length,
          }
        : undefined,
      canRefresh: Boolean(listConfig.url),
    }
  })
}

function getListSourceLabel(draft: ListDraft, t: (key: string) => string) {
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

function ListSource({
  list,
  t,
}: {
  list: ListTableRow
  t: (key: string, options?: Record<string, unknown>) => string
}) {
  if (!list.draft.url && !list.draft.file) return null
  return (
    <div className="min-w-0 space-y-2">
      {list.draft.url ? (
        <div className="flex min-w-0 items-center gap-2 text-sm text-muted-foreground">
          <SourceUrl text={list.draft.url} />
          <a
            href={list.draft.url}
            target="_blank"
            rel="noreferrer"
            aria-label={list.draft.url}
            className="flex size-8 shrink-0 items-center justify-center rounded-md hover:text-foreground focus-visible:outline-2 focus-visible:outline-ring"
          >
            <ExternalLink className="size-4" />
          </a>
        </div>
      ) : null}
      {list.draft.file ? <StartEllipsis text={list.draft.file} /> : null}
      {list.canRefresh ? (
        <div className="text-xs text-muted-foreground">
          {t("pages.lists.lastUpdated", {
            value: formatLastUpdatedLabel(
              list.lastUpdated,
              t("pages.lists.neverUpdated")
            ),
          })}
        </div>
      ) : null}
    </div>
  )
}

function StartEllipsis({ text }: { text: string }) {
  return (
    <span
      title={text}
      className="min-w-0 truncate text-left text-sm text-muted-foreground [direction:rtl]"
    >
      <bdi dir="ltr">{text}</bdi>
    </span>
  )
}

function SourceUrl({ text }: { text: string }) {
  const { domain, remainder } = splitListSourceUrl(text)
  const hasSlash = remainder.startsWith("/")
  return (
    <div title={text} className="flex min-w-0 flex-1 items-center">
      <span className="max-w-[70%] shrink-0 break-all">
        {domain}
        {hasSlash ? "/" : ""}
      </span>
      <span className="min-w-0 truncate text-right [direction:rtl]">
        <bdi dir="ltr">{hasSlash ? remainder.slice(1) : remainder}</bdi>
      </span>
    </div>
  )
}
