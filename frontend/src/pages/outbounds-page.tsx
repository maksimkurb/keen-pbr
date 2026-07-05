import { ArrowRight, Pencil, Plus, Trash2 } from "lucide-react"
import type { ReactNode } from "react"
import { useState } from "react"
import { useTranslation } from "react-i18next"

import { useQueryClient } from "@tanstack/react-query"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { Outbound } from "@/api/generated/model/outbound"
import type { RouteRule } from "@/api/generated/model/routeRule"
import type { RuntimeInterfaceInventoryEntry } from "@/api/generated/model/runtimeInterfaceInventoryEntry"
import type { RuntimeOutboundState } from "@/api/generated/model/runtimeOutboundState"
import {
  useConfigMutationPending,
  usePostConfigMutation,
} from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
import {
  useGetConfig,
  useGetRuntimeInterfaces,
  useGetRuntimeOutbounds,
} from "@/api/queries"
import { selectConfig, selectOutbounds } from "@/api/selectors"
import { ActionButtons } from "@/components/shared/action-buttons"
import { BulkSelectionToolbar } from "@/components/shared/bulk-selection-toolbar"
import { ConfigSaveErrorAlert } from "@/components/shared/config-save-error-alert"
import { DataTable } from "@/components/shared/data-table"
import {
  DeleteImpactDialog,
  type DeleteImpactItem,
} from "@/components/shared/delete-impact-dialog"
import { InterfaceRowContent } from "@/components/shared/interface-picker"
import { ListPlaceholder } from "@/components/shared/list-placeholder"
import {
  RuntimeInterfaceStatusRow,
  RuntimeStateBadge,
} from "@/components/shared/outbound-interface-status-list"
import { PageHeader } from "@/components/shared/page-header"
import {
  RuntimeOutboundDetails,
  RuntimeOutboundEntry,
} from "@/components/shared/runtime-outbound-state"
import { TableSkeleton } from "@/components/shared/table-skeleton"
import { useRowSelection } from "@/hooks/use-row-selection"
import { toast } from "sonner"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { getApiErrorMessage } from "@/lib/api-errors"
import {
  buildUpdatedConfigForOutboundsDelete,
  getOutboundDeleteImpact,
  type OutboundDeleteImpact,
} from "@/pages/outbounds-utils"

type OutboundItem = {
  id: string
  tag: string
  type: Outbound["type"]
  summary: ReactNode
  outbound: Outbound
  runtimeInterface?: RuntimeInterfaceInventoryEntry
  runtimeState?: RuntimeOutboundState
}

export function OutboundsPage() {
  const { t } = useTranslation()
  const queryClient = useQueryClient()
  const [, navigate] = useLocation()
  const [deleteRequest, setDeleteRequest] = useState<{
    tags: string[]
    impact: OutboundDeleteImpact
    config: ConfigObject
    clearSelectionOnSuccess: boolean
  } | null>(null)
  const [deletePreview, setDeletePreview] = useState<typeof deleteRequest>(null)
  const configMutationPending = useConfigMutationPending()
  const configQuery = useGetConfig()
  const runtimeOutboundsQuery = useGetRuntimeOutbounds({
    query: {
      refetchInterval: 10_000,
      refetchIntervalInBackground: false,
    },
  })
  const runtimeInterfacesQuery = useGetRuntimeInterfaces({
    query: {
      refetchInterval: 10_000,
      refetchIntervalInBackground: false,
    },
  })
  const loadedConfig = selectConfig(configQuery.data)
  const visibleDeleteRequest = deleteRequest ?? deletePreview
  // using toasts for mutation errors

  const runtimeOutboundByTag = new Map(
    (runtimeOutboundsQuery.data?.status === 200
      ? runtimeOutboundsQuery.data.data.outbounds
      : []
    ).map((runtimeOutbound) => [runtimeOutbound.tag, runtimeOutbound])
  )
  const runtimeInterfaceByName = new Map(
    (runtimeInterfacesQuery.data?.status === 200
      ? runtimeInterfacesQuery.data.data.interfaces
      : []
    ).map((runtimeInterface) => [runtimeInterface.name, runtimeInterface])
  )
  const outboundItems = selectOutbounds(loadedConfig).map((outbound) =>
    mapOutboundToItem(
      outbound,
      runtimeOutboundByTag.get(outbound.tag),
      runtimeInterfaceByName.get(outbound.interface ?? ""),
      t
    )
  )
  const outboundRowIds = outboundItems.map((outbound) => outbound.id)
  const outboundSelection = useRowSelection(outboundRowIds)

  const postConfigMutation = usePostConfigMutation({
    mutation: {
      onSuccess: async () => {
        // success — nothing to show here (toasts handled on error)
        await Promise.all([
          queryClient.invalidateQueries({ queryKey: queryKeys.config() }),
          queryClient.invalidateQueries({
            queryKey: queryKeys.healthService(),
          }),
          queryClient.invalidateQueries({
            queryKey: queryKeys.healthRouting(),
          }),
          queryClient.invalidateQueries({
            queryKey: queryKeys.runtimeOutbounds(),
          }),
        ])
      },
      onError: (error) => {
        toast.error(getApiErrorMessage(error as ApiError), {
          richColors: true,
        })
      },
    },
  })

  const handleDelete = (tag: string) => {
    if (!loadedConfig) {
      return
    }

    const request = {
      tags: [tag],
      impact: getOutboundDeleteImpact(loadedConfig, [tag]),
      config: loadedConfig,
      clearSelectionOnSuccess: false,
    }
    setDeletePreview(request)
    setDeleteRequest(request)
  }

  const handleBulkDelete = () => {
    if (!loadedConfig || outboundSelection.selectedCount === 0) {
      return
    }

    const tags = [...outboundSelection.selectedIds]
    const request = {
      tags,
      impact: getOutboundDeleteImpact(loadedConfig, tags),
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
        data: buildUpdatedConfigForOutboundsDelete(
          loadedConfig,
          deleteRequest.tags
        ),
      },
      {
        onSuccess: () => {
          if (deleteRequest.clearSelectionOnSuccess) {
            outboundSelection.clear()
          }
          setDeleteRequest(null)
        },
      }
    )
  }

  return (
    <div className="space-y-6">
      <PageHeader
        actions={
          <Button
            disabled={configMutationPending}
            onClick={() => navigate("/outbounds/create")}
          >
            <Plus className="mr-1 h-4 w-4" />
            {t("pages.outbounds.actions.new")}
          </Button>
        }
        description={t("pages.outbounds.description")}
        title={t("pages.outbounds.title")}
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
      ) : outboundItems.length === 0 ? (
        <ListPlaceholder
          description={t("pages.outbounds.empty.description")}
          title={t("pages.outbounds.empty.title")}
        />
      ) : (
        <div className="space-y-3">
          {outboundSelection.hasSelection ? (
            <BulkSelectionToolbar
              countLabel={t("pages.outbounds.bulk.selected", {
                count: outboundSelection.selectedCount,
              })}
            >
              <Button
                disabled={configMutationPending}
                onClick={handleBulkDelete}
                size="sm"
                variant="destructive"
              >
                <Trash2 className="mr-1 h-4 w-4" />
                {t("pages.outbounds.bulk.delete")}
              </Button>
            </BulkSelectionToolbar>
          ) : null}
          <DataTable
            headers={[
              t("pages.outbounds.headers.tag"),
              t("pages.outbounds.headers.type"),
              t("pages.outbounds.headers.summary"),
              t("pages.outbounds.headers.runtime"),
              t("pages.outbounds.headers.actions"),
            ]}
            rows={outboundItems.map((outbound) => [
              <RuntimeOutboundEntry
                key={`${outbound.id}-tag`}
                runtimeState={outbound.runtimeState}
                title={outbound.tag}
                t={t}
              />,
              <Badge key={`${outbound.id}-type`} variant="outline">
                {outbound.type}
              </Badge>,
              <div
                className="min-w-0 text-sm text-muted-foreground"
                key={`${outbound.id}-summary`}
              >
                {outbound.summary}
              </div>,
              <OutboundRuntimeCell
                key={`${outbound.id}-runtime`}
                outbound={outbound.outbound}
                runtimeInterface={outbound.runtimeInterface}
                runtimeInterfaces={runtimeInterfaceByName}
                runtimeState={outbound.runtimeState}
                t={t}
              />,
              <ActionButtons
                actions={[
                  {
                    disabled: configMutationPending,
                    icon: <Pencil className="h-4 w-4" />,
                    label: t("common.edit"),
                    onClick: () => navigate(`/outbounds/${outbound.id}/edit`),
                  },
                  {
                    disabled: configMutationPending,
                    icon: <Trash2 className="h-4 w-4" />,
                    label: t("common.delete"),
                    onClick: () => handleDelete(outbound.id),
                  },
                ]}
                key={`${outbound.id}-actions`}
              />,
            ])}
            selection={{
              rowIds: outboundRowIds,
              selectedIds: outboundSelection.selectedIds,
              disabled: configMutationPending,
              onToggle: outboundSelection.toggleOne,
              onToggleAll: outboundSelection.setAllVisible,
              selectAllLabel: t("common.selection.selectAll"),
              getRowLabel: (rowId) =>
                t("common.selection.selectRow", { rowLabel: rowId }),
            }}
          />
        </div>
      )}
      <DeleteImpactDialog
        confirmLabel={t("pages.outbounds.deleteDialog.confirm")}
        description={t("pages.outbounds.deleteDialog.description", {
          tags: visibleDeleteRequest?.tags.join(", ") ?? "",
        })}
        impactItems={
          visibleDeleteRequest
            ? getOutboundDeleteImpactItems(
                visibleDeleteRequest.config,
                visibleDeleteRequest.tags,
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
        title={t("pages.outbounds.deleteDialog.title")}
      />
    </div>
  )
}

function getOutboundDeleteImpactItems(
  config: ConfigObject | undefined,
  requestedTags: string[],
  impact: OutboundDeleteImpact,
  t: (key: string, options?: Record<string, unknown>) => string
) {
  const items: DeleteImpactItem[] = []
  const requestedTagSet = new Set(requestedTags)

  for (const tag of requestedTags) {
    items.push({
      label: (
        <>
          {t("pages.outbounds.deleteDialog.items.outboundPrefix")}{" "}
          <strong>{tag}</strong>{" "}
          {t("pages.outbounds.deleteDialog.items.outboundSuffix")}
        </>
      ),
    })
  }

  for (const tag of impact.deletedOutboundTags) {
    if (requestedTagSet.has(tag)) {
      continue
    }

    items.push({
      label: (
        <>
          {t("pages.outbounds.deleteDialog.items.dependentOutboundPrefix")}{" "}
          <strong>{tag}</strong>{" "}
          {t("pages.outbounds.deleteDialog.items.dependentOutboundSuffix")}
        </>
      ),
    })
  }

  for (const index of impact.routeRuleIndexes) {
    items.push({
      label: t("pages.outbounds.deleteDialog.items.routingRule", {
        number: index + 1,
      }),
      details: getRouteRuleImpactDetails(config?.route?.rules?.[index], t),
    })
  }

  for (const server of impact.dnsServerDetours) {
    items.push({
      label: t("pages.outbounds.deleteDialog.items.dnsDetour", { server }),
      details: [
        formatDetail(
          t("pages.dnsServers.headers.outbound"),
          formatValueTransition(
            config?.dns?.servers?.find((item) => item.tag === server)?.detour ??
              t("common.noneShort"),
            t("common.noneShort")
          )
        ),
      ],
    })
  }

  for (const membership of impact.urltestMemberships) {
    const group = config?.outbounds?.find(
      (outbound) => outbound.tag === membership.outboundTag
    )?.outbound_groups?.[membership.groupIndex]
    const remainingTags =
      group?.outbounds.filter(
        (tag) => !impact.deletedOutboundTags.includes(tag)
      ) ?? []
    const isRemoved = remainingTags.length === 0

    items.push({
      label: isRemoved
        ? t("pages.outbounds.deleteDialog.items.urltestGroupRemoved", {
            group: membership.groupIndex + 1,
            outbound: membership.outboundTag,
          })
        : t("pages.outbounds.deleteDialog.items.urltestGroupChanged", {
            group: membership.groupIndex + 1,
            outbound: membership.outboundTag,
          }),
      details: [
        formatDetail(
          t("pages.outbounds.deleteDialog.items.groupOutbounds"),
          isRemoved
            ? formatListValue(group?.outbounds ?? [], t)
            : formatTransition(group?.outbounds ?? [], remainingTags, t)
        ),
      ],
    })
  }

  return items
}

function getRouteRuleImpactDetails(
  rule: RouteRule | undefined,
  t: (key: string, options?: Record<string, unknown>) => string
) {
  if (!rule) {
    return []
  }

  const details = [
    {
      label: t("pages.routingRules.headers.outbound"),
      value: rule.outbound,
    },
    {
      label: t("pages.routingRules.criteriaLabels.lists"),
      value: (rule.list ?? []).join(", "),
    },
    {
      label: t("pages.routingRules.criteriaLabels.proto"),
      value: rule.proto,
    },
    {
      label: t("pages.routingRules.criteriaLabels.dscp"),
      value: rule.dscp?.toString(),
    },
    {
      label: t("pages.routingRules.criteriaLabels.sourceIp"),
      value: rule.src_addr,
    },
    {
      label: t("pages.routingRules.criteriaLabels.destinationIp"),
      value: rule.dest_addr,
    },
    {
      label: t("pages.routingRules.criteriaLabels.sourcePort"),
      value: rule.src_port,
    },
    {
      label: t("pages.routingRules.criteriaLabels.destinationPort"),
      value: rule.dest_port,
    },
  ]
    .filter(
      (
        item
      ): item is {
        label: string
        value: string
      } => typeof item.value === "string" && item.value.trim().length > 0
    )
    .map((item) =>
      t("pages.outbounds.deleteDialog.items.ruleDetail", {
        label: item.label,
        value: item.value,
      })
    )

  return details
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
  return formatValueTransition(
    formatListValue(before, t),
    formatListValue(after, t)
  )
}

function formatValueTransition(before: string, after: string) {
  return <ChangeValue after={after} before={before} />
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

function mapOutboundToItem(
  outbound: Outbound,
  runtimeState: RuntimeOutboundState | undefined,
  runtimeInterface: RuntimeInterfaceInventoryEntry | undefined,
  t: (key: string, options?: Record<string, unknown>) => string
): OutboundItem {
  return {
    id: outbound.tag,
    tag: outbound.tag,
    type: outbound.type,
    summary: getOutboundSummary(outbound, t),
    outbound,
    runtimeInterface,
    runtimeState,
  }
}

function getOutboundSummary(
  outbound: Outbound,
  t: (key: string, options?: Record<string, unknown>) => string
): ReactNode {
  if (outbound.type === "interface") {
    return t("pages.outbounds.summary.interface", {
      value: outbound.interface ?? "-",
    })
  }

  if (outbound.type === "table") {
    return t("pages.outbounds.summary.table", {
      value: outbound.table ?? "-",
    })
  }

  if (outbound.type === "urltest") {
    const allOutbounds =
      outbound.outbound_groups?.flatMap((group) => group.outbounds) ?? []
    return t("pages.outbounds.summary.urltest", {
      value: allOutbounds.join(","),
    })
  }

  return t("common.noneShort")
}

function SummaryChip({ value }: { value: string }) {
  return (
    <code className="rounded bg-muted px-1.5 py-0.5 text-xs text-muted-foreground">
      {value}
    </code>
  )
}

function OutboundRuntimeCell({
  outbound,
  runtimeState,
  runtimeInterface,
  runtimeInterfaces,
  t,
}: {
  outbound: Outbound
  runtimeState?: RuntimeOutboundState
  runtimeInterface?: RuntimeInterfaceInventoryEntry
  runtimeInterfaces: Map<string, RuntimeInterfaceInventoryEntry>
  t: (key: string, options?: Record<string, unknown>) => string
}) {
  if (outbound.type === "interface") {
    const runtimeInterfaceState = runtimeState?.interfaces[0]

    return (
      <RuntimeInterfaceStatusRow
        item={{
          name: outbound.interface ?? "-",
          tone: getInterfaceRuntimeTone(runtimeInterfaceState?.status),
        }}
        variant="list"
      >
        <InterfaceRowContent
          afterStatus={
            runtimeInterfaceState ? (
              <RuntimeStateBadge
                active={runtimeInterfaceState.status === "active"}
                label={t(
                  `runtime.interfaceStatus.${runtimeInterfaceState.status}`
                )}
                tone={getInterfaceRuntimeTone(runtimeInterfaceState.status)}
              />
            ) : null
          }
          grow={false}
          interfaceEntry={runtimeInterface}
          isVirtual={!runtimeInterface}
          name={outbound.interface ?? "-"}
        />
        {outbound.gateway ? (
          <SummaryChip
            value={t("pages.outbounds.summary.gateway4", {
              value: outbound.gateway,
            })}
          />
        ) : null}
        {outbound.gateway6 ? (
          <SummaryChip
            value={t("pages.outbounds.summary.gateway6", {
              value: outbound.gateway6,
            })}
          />
        ) : null}
      </RuntimeInterfaceStatusRow>
    )
  }

  return (
    <RuntimeOutboundDetails
      fallbackLabel={getRuntimeFallbackLabel(outbound, t)}
      fallbackTone={getRuntimeFallbackTone(outbound)}
      runtimeState={runtimeState}
      runtimeInterfaces={runtimeInterfaces}
      t={t}
      variant="list"
    />
  )
}

function getInterfaceRuntimeTone(
  status?: RuntimeOutboundState["interfaces"][number]["status"]
) {
  switch (status) {
    case "active":
    case "backup":
      return "healthy"
    case "degraded":
    case "unavailable":
      return "degraded"
    case "unknown":
    default:
      return "unknown"
  }
}

function getRuntimeFallbackLabel(
  outbound: Outbound,
  t: (key: string, options?: Record<string, unknown>) => string
): string | undefined {
  if (outbound.type === "table" && typeof outbound.table === "number") {
    return t("runtime.fallback.table", { value: outbound.table })
  }

  if (outbound.type === "blackhole") {
    return t("runtime.fallback.blackhole")
  }

  return undefined
}

function getRuntimeFallbackTone(
  outbound: Outbound
): "info" | "unknown" | undefined {
  if (outbound.type === "table") {
    return "info"
  }

  if (outbound.type === "blackhole") {
    return "unknown"
  }

  return undefined
}
