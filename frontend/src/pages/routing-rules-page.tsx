import { ArrowDown, ArrowUp, Pencil, Plus, Trash2 } from "lucide-react"
import { useMemo, useState } from "react"
import { useQueryClient } from "@tanstack/react-query"
import { useTranslation } from "react-i18next"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { RouteRule } from "@/api/generated/model/routeRule"
import { usePostConfigMutation, useConfigMutationPending } from "@/api/mutations"
import type { RuntimeOutboundState } from "@/api/generated/model"
import { useGetConfig, useGetRuntimeOutbounds } from "@/api/queries"
import { selectConfig } from "@/api/selectors"
import { ActionButtons } from "@/components/shared/action-buttons"
import { BulkSelectionToolbar } from "@/components/shared/bulk-selection-toolbar"
import { ConfigSaveErrorAlert } from "@/components/shared/config-save-error-alert"
import { DataTable, type DataTableSelection } from "@/components/shared/data-table"
import { ListPlaceholder } from "@/components/shared/list-placeholder"
import { PageHeader } from "@/components/shared/page-header"
import { RuntimeOutboundEntry } from "@/components/shared/runtime-outbound-state"
import { TableSkeleton } from "@/components/shared/table-skeleton"
import { toast } from "sonner"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Switch } from "@/components/ui/switch"
import {
  getApiErrorMessage,
  getRuleDetailPieces,
  reorderRules,
  setRouteRuleEnabled,
  type RuleDetailPiece,
} from "@/pages/routing-rules-utils"
import {
  ROUTER_RUNTIME_POLL_MS,
  routerFriendlyPollingMs,
} from "@/lib/router-friendly-query"

export function RoutingRulesPage() {
  const { t } = useTranslation()
  const [, navigate] = useLocation()
  const queryClient = useQueryClient()

  const configMutationPending = useConfigMutationPending()
  const pollRuntimeRoutes = useMemo(
    () => routerFriendlyPollingMs(queryClient, ROUTER_RUNTIME_POLL_MS),
    [queryClient],
  )
  const configQuery = useGetConfig()
  const loadedConfig = selectConfig(configQuery.data)
  const routeRules = useMemo(
    () => loadedConfig?.route?.rules ?? [],
    [loadedConfig?.route?.rules],
  )

  const [selectedRuleIndices, setSelectedRuleIndices] = useState<Set<string>>(
    () => new Set(),
  )

  const validRuleIndexSet = useMemo(
    () =>
      new Set(
        routeRules.map((_rule: RouteRule, ruleIndex: number) => String(ruleIndex)),
      ),
    [routeRules],
  )

  const selectedRuleIndicesResolved = useMemo(() => {
    const next = new Set<string>()
    for (const id of selectedRuleIndices) {
      if (validRuleIndexSet.has(id)) {
        next.add(id)
      }
    }

    return next
  }, [selectedRuleIndices, validRuleIndexSet])

  const runtimeOutboundsQuery = useGetRuntimeOutbounds({
    query: {
      refetchInterval: pollRuntimeRoutes,
      refetchIntervalInBackground: false,
    },
  })
  const runtimeOutbounds = useMemo(
    () =>
      runtimeOutboundsQuery.data?.status === 200
        ? runtimeOutboundsQuery.data.data.outbounds
        : [],
    [runtimeOutboundsQuery.data]
  )
  const runtimeOutboundByTag = useMemo(
    () =>
      new Map(
        runtimeOutbounds.map((runtimeOutbound) => [runtimeOutbound.tag, runtimeOutbound])
      ),
    [runtimeOutbounds]
  )

  const tableRows = routeRules.map((rule: RouteRule, index: number) => {
    const runtimeState = runtimeOutboundByTag.get(rule.outbound)
    return getRouteRuleRow(rule, index, t, runtimeState)
  })

  const postConfigMutation = usePostConfigMutation({
    mutation: {
      onSuccess: () => {
        toast.success(t("pages.routingRules.messages.saved"))
      },
      onError: (error) => {
        const apiError = error as ApiError
        toast.error(getApiErrorMessage(apiError), { richColors: true })
      },
    },
  })

  const persistRules = (
    config: NonNullable<typeof loadedConfig>,
    nextRules: RouteRule[],
    options?: { clearSelection?: boolean },
  ) => {
    postConfigMutation.mutate(
      {
        data: {
          ...config,
          route: {
            ...config.route,
            rules: nextRules,
          },
        },
      },
      options?.clearSelection
        ? {
            onSuccess: () => {
              setSelectedRuleIndices(new Set())
            },
          }
        : undefined,
    )
  }

  const handleDelete = (index: number) => {
    if (!loadedConfig) {
      return
    }

    const nextRules = routeRules.filter(
      (_rule: RouteRule, ruleIndex: number) => ruleIndex !== index
    )
    persistRules(loadedConfig, nextRules)
  }

  const handleMove = (index: number, direction: -1 | 1) => {
    if (!loadedConfig) {
      return
    }

    const targetIndex = index + direction
    if (targetIndex < 0 || targetIndex >= routeRules.length) {
      return
    }

    const nextRules = reorderRules(routeRules, index, targetIndex)
    persistRules(loadedConfig, nextRules)
  }

  const handleEnabledChange = (index: number, enabled: boolean) => {
    if (!loadedConfig) {
      return
    }

    persistRules(loadedConfig, setRouteRuleEnabled(routeRules, index, enabled))
  }

  const handleBulkDelete = () => {
    if (!loadedConfig || selectedRuleIndicesResolved.size === 0) {
      return
    }

    const confirmed = window.confirm(
      t("pages.routingRules.bulk.confirmDelete", {
        count: selectedRuleIndicesResolved.size,
      }),
    )
    if (!confirmed) {
      return
    }

    const nextRules = routeRules.filter(
      (_rule: RouteRule, ruleIndex: number) =>
        !selectedRuleIndicesResolved.has(String(ruleIndex)),
    )
    persistRules(loadedConfig, nextRules, { clearSelection: true })
  }

  const handleBulkSetEnabled = (enabled: boolean) => {
    if (!loadedConfig || selectedRuleIndicesResolved.size === 0) {
      return
    }

    const nextRules = routeRules.map((rule: RouteRule, ruleIndex: number) =>
      selectedRuleIndicesResolved.has(String(ruleIndex))
        ? { ...rule, enabled }
        : rule,
    )
    persistRules(loadedConfig, nextRules)
  }

  const ruleSelectionProps: DataTableSelection = {
    rowIds: routeRules.map((_rule: RouteRule, index: number) => String(index)),
    selectedIds: selectedRuleIndicesResolved,
    selectionDisabled: configMutationPending,
    selectAllAriaLabel: t("common.selection.selectAll"),
    selectRowAriaLabel: (rowId: string) =>
      t("common.selection.selectRow", {
        rowLabel:
          `${t("pages.routingRules.title")} #${Number(rowId) + 1}`,
      }),
    selectAllTooltip: t("common.selection.selectAll"),
    onToggleRow: (rowId: string) => {
      setSelectedRuleIndices((previous) => {
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
      setSelectedRuleIndices(
        selectedAll ? new Set(routeRules.map((_r, idx) => String(idx))) : new Set(),
      )
    },
  }

  return (
    <div className="space-y-6">
      <PageHeader
        actions={
          <Button disabled={configMutationPending} onClick={() => navigate("/routing-rules/create")}>
            <Plus className="mr-1 h-4 w-4" />
            {t("pages.routingRules.actions.addRule")}
          </Button>
        }
        description={t("pages.routingRules.description")}
        title={t("pages.routingRules.title")}
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
          description={t("pages.routingRules.empty.description")}
          title={t("pages.routingRules.empty.title")}
        />
      ) : (
        <div className="space-y-3">
          {selectedRuleIndicesResolved.size > 0 ? (
            <BulkSelectionToolbar
              countLabel={t("pages.routingRules.bulk.selected", {
                count: selectedRuleIndicesResolved.size,
              })}
            >
              <Button
                disabled={configMutationPending}
                onClick={() => handleBulkSetEnabled(true)}
                size="sm"
                variant="outline"
              >
                {t("pages.routingRules.bulk.enable")}
              </Button>
              <Button
                disabled={configMutationPending}
                onClick={() => handleBulkSetEnabled(false)}
                size="sm"
                variant="outline"
              >
                {t("pages.routingRules.bulk.disable")}
              </Button>
              <Button disabled={configMutationPending} onClick={() => handleBulkDelete()} size="sm" variant="destructive">
                <Trash2 className="mr-1 h-4 w-4" />
                {t("pages.routingRules.bulk.delete")}
              </Button>
            </BulkSelectionToolbar>
          ) : null}
          <DataTable
            headers={[
              "",
              t("pages.routingRules.headers.order"),
              t("pages.routingRules.headers.criteria"),
              t("pages.routingRules.headers.outbound"),
              t("pages.routingRules.headers.actions"),
            ]}
            narrowColumns={[0, 1]}
            rows={tableRows.map((row: ReturnType<typeof getRouteRuleRow>) => [
            <div className="flex items-center" key={`${row.id}-enabled`}>
              <Switch
                aria-label={t(
                  row.enabled
                    ? "pages.routingRules.actions.disableRule"
                    : "pages.routingRules.actions.enableRule"
                )}
                checked={row.enabled}
                disabled={configMutationPending}
                onCheckedChange={(checked) => handleEnabledChange(row.index, checked)}
                title={t(
                  row.enabled
                    ? "pages.routingRules.actions.disableRule"
                    : "pages.routingRules.actions.enableRule"
                )}
              />
            </div>,
            <span className="font-medium" key={`${row.id}-order`}>
              #{row.order}
            </span>,
            <div
              className="flex flex-wrap gap-1"
              key={`${row.id}-conditions`}
            >
              {row.conditions.map((condition) => (
                <Badge
                  className="max-w-[20rem] truncate"
                  key={`${row.id}-${condition.key}`}
                  title={`${condition.label}: ${condition.value}`}
                  variant="outline"
                >
                  <span className="font-semibold">{condition.label}:</span>
                  &nbsp;{condition.value}
                </Badge>
              ))}
            </div>,
            <div key={`${row.id}-outbound`}>
              <RuntimeOutboundEntry
                runtimeState={row.runtimeState}
                title={row.outbound}
                t={t}
              />
            </div>,
            <ActionButtons
              actions={[
                {
                  disabled: configMutationPending,
                  icon: <ArrowUp className="h-4 w-4" />,
                  label: t("common.moveUp"),
                  onClick: () => handleMove(row.index, -1),
                },
                {
                  disabled: configMutationPending,
                  icon: <ArrowDown className="h-4 w-4" />,
                  label: t("common.moveDown"),
                  onClick: () => handleMove(row.index, 1),
                },
                {
                  disabled: configMutationPending,
                  icon: <Pencil className="h-4 w-4" />,
                  label: t("common.edit"),
                  onClick: () => navigate(`/routing-rules/${row.index}/edit`),
                },
                {
                  disabled: configMutationPending,
                  icon: <Trash2 className="h-4 w-4" />,
                  label: t("common.delete"),
                  onClick: () => handleDelete(row.index),
                },
              ]}
              key={`${row.id}-actions`}
            />,
          ])}
            selection={ruleSelectionProps}
          />
        </div>
      )}
    </div>
  )
}

const CRITERIA_LABEL_KEY: Record<
  RuleDetailPiece["key"],
  string
> = {
  lists: "pages.routingRules.criteriaLabels.lists",
  proto: "pages.routingRules.criteriaLabels.proto",
  src_addr: "pages.routingRules.criteriaLabels.sourceIp",
  dest_addr: "pages.routingRules.criteriaLabels.destinationIp",
  src_port: "pages.routingRules.criteriaLabels.sourcePort",
  dest_port: "pages.routingRules.criteriaLabels.destinationPort",
}

function getRouteRuleRow(
  rule: RouteRule,
  index: number,
  t: (key: string) => string,
  runtimeState?: RuntimeOutboundState
) {
  const conditions = getRuleDetailPieces(rule).map((piece) => ({
    key: piece.key,
    label: t(CRITERIA_LABEL_KEY[piece.key]),
    value: piece.value,
  }))

  return {
    id: `routing-rule-${index}`,
    enabled: rule.enabled ?? true,
    index,
    order: index + 1,
    conditions,
    outbound: rule.outbound,
    runtimeState,
  }
}
