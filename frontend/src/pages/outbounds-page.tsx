import { Pencil, Plus, Trash2 } from "lucide-react"
import type { ReactNode } from "react"
import { useMemo, useState } from "react"
import { useTranslation } from "react-i18next"

import { useQueryClient } from "@tanstack/react-query"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { Outbound } from "@/api/generated/model/outbound"
import type { RuntimeInterfaceInventoryEntry } from "@/api/generated/model/runtimeInterfaceInventoryEntry"
import type { RuntimeOutboundState } from "@/api/generated/model/runtimeOutboundState"
import { usePostConfigMutation, useConfigMutationPending } from "@/api/mutations"
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
import { DataTable, type DataTableSelection } from "@/components/shared/data-table"
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
import { toast } from "sonner"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { getApiErrorMessage } from "@/lib/api-errors"
import {
  ROUTER_RUNTIME_POLL_MS,
  routerFriendlyPollingMs,
} from "@/lib/router-friendly-query"

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
  const configMutationPending = useConfigMutationPending()
  const pollRuntimeOutbounds = useMemo(
    () => routerFriendlyPollingMs(queryClient, ROUTER_RUNTIME_POLL_MS),
    [queryClient],
  )
  const configQuery = useGetConfig()
  const runtimeOutboundsQuery = useGetRuntimeOutbounds({
    query: {
      refetchInterval: pollRuntimeOutbounds,
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

  const runtimeOutboundByTag = useMemo(
    () =>
      new Map(
        (runtimeOutboundsQuery.data?.status === 200
          ? runtimeOutboundsQuery.data.data.outbounds
          : []
        ).map((runtimeOutbound) => [runtimeOutbound.tag, runtimeOutbound]),
      ),
    [runtimeOutboundsQuery.data],
  )
  const runtimeInterfaceByName = useMemo(
    () =>
      new Map(
        (runtimeInterfacesQuery.data?.status === 200
          ? runtimeInterfacesQuery.data.data.interfaces
          : []
        ).map((runtimeInterface) => [runtimeInterface.name, runtimeInterface]),
      ),
    [runtimeInterfacesQuery.data],
  )

  const outboundItems = useMemo(
    () =>
      selectOutbounds(loadedConfig).map((outbound) =>
        mapOutboundToItem(
          outbound,
          runtimeOutboundByTag.get(outbound.tag),
          runtimeInterfaceByName.get(outbound.interface ?? ""),
          t,
        ),
      ),
    [loadedConfig, runtimeOutboundByTag, runtimeInterfaceByName, t],
  )

  const outboundRowIds = useMemo(
    () => outboundItems.map((item) => item.id),
    [outboundItems],
  )

  const [selectedOutboundTags, setSelectedOutboundTags] = useState<Set<string>>(
    () => new Set(),
  )

  const validOutboundIdSet = useMemo(
    () => new Set(outboundRowIds),
    [outboundRowIds],
  )

  const selectedOutboundTagsResolved = useMemo(() => {
    const next = new Set<string>()
    for (const id of selectedOutboundTags) {
      if (validOutboundIdSet.has(id)) {
        next.add(id)
      }
    }

    return next
  }, [selectedOutboundTags, validOutboundIdSet])

  const outboundSelectionProps: DataTableSelection = {
    rowIds: outboundRowIds,
    selectedIds: selectedOutboundTagsResolved,
    selectionDisabled: configMutationPending,
    selectAllAriaLabel: t("common.selection.selectAll"),
    selectRowAriaLabel: (tag: string) =>
      t("common.selection.selectRow", { rowLabel: tag }),
    selectAllTooltip: t("common.selection.selectAll"),
    onToggleRow: (tag: string) => {
      setSelectedOutboundTags((previous) => {
        const next = new Set(previous)
        if (next.has(tag)) {
          next.delete(tag)
        } else {
          next.add(tag)
        }

        return next
      })
    },
    onSelectAllVisible: (selectedAll: boolean) => {
      setSelectedOutboundTags(
        selectedAll ? new Set(outboundRowIds) : new Set(),
      )
    },
  }

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

    const nextOutbounds = selectOutbounds(loadedConfig).filter(
      (item) => item.tag !== tag
    )
    const urltestReferencesError = validateUrltestGroupReferences(nextOutbounds, t)

    if (urltestReferencesError) {
      toast.error(urltestReferencesError, { richColors: true })
      return
    }

    const updatedConfig: ConfigObject = {
      ...loadedConfig,
      outbounds: nextOutbounds,
    }

    // proceed with mutation
    postConfigMutation.mutate({ data: updatedConfig })
  }

  const handleBulkDeleteOutbounds = () => {
    if (!loadedConfig || selectedOutboundTagsResolved.size === 0) {
      return
    }

    const confirmed = window.confirm(
      t("pages.outbounds.bulk.confirmDelete", {
        count: selectedOutboundTagsResolved.size,
      }),
    )

    if (!confirmed) {
      return
    }

    const nextOutbounds = selectOutbounds(loadedConfig).filter(
      (item) => !selectedOutboundTagsResolved.has(item.tag),
    )
    const urltestReferencesError = validateUrltestGroupReferences(
      nextOutbounds,
      t,
    )

    if (urltestReferencesError) {
      toast.error(urltestReferencesError, { richColors: true })
      return
    }

    const updatedConfig: ConfigObject = {
      ...loadedConfig,
      outbounds: nextOutbounds,
    }

    postConfigMutation.mutate(
      { data: updatedConfig },
      {
        onSuccess: () => {
          setSelectedOutboundTags(new Set())
        },
      },
    )
  }

  return (
    <div className="space-y-6">
      <PageHeader
        actions={
          <Button disabled={configMutationPending} onClick={() => navigate("/outbounds/create")}>
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
          {selectedOutboundTagsResolved.size > 0 ? (
            <BulkSelectionToolbar
              countLabel={t("pages.outbounds.bulk.selected", {
                count: selectedOutboundTagsResolved.size,
              })}
            >
              <Button
                disabled={configMutationPending}
                onClick={() => handleBulkDeleteOutbounds()}
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
            narrowColumns={[0]}
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
            selection={outboundSelectionProps}
          />
        </div>
      )}
    </div>
  )
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
                label={t(`runtime.interfaceStatus.${runtimeInterfaceState.status}`)}
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

function validateUrltestGroupReferences(
  outbounds: Outbound[],
  t: (key: string, options?: Record<string, unknown>) => string
): string | null {
  const tags = new Set(outbounds.map((outbound) => outbound.tag))

  for (const outbound of outbounds) {
    if (outbound.type !== "urltest") {
      continue
    }

    for (const group of outbound.outbound_groups ?? []) {
      for (const referencedTag of group.outbounds) {
        if (!tags.has(referencedTag)) {
          return t("pages.outbounds.messages.missingReference", {
            outbound: outbound.tag,
            referenced: referencedTag,
          })
        }
      }
    }
  }

  return null
}
