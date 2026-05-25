import { Pencil, Plus, Trash2 } from "lucide-react"
import { useMemo } from "react"
import { useTranslation } from "react-i18next"
import { useLocation } from "wouter"

import type { getConfigResponse } from "@/api/generated/keen-api"
import { DnsServerType } from "@/api/generated/model/dnsServerType"
import {
  useConfigMutationPending,
  usePostConfigMutation,
} from "@/api/mutations"
import { useGetConfig } from "@/api/queries"
import { ActionButtons } from "@/components/shared/action-buttons"
import { BulkSelectionToolbar } from "@/components/shared/bulk-selection-toolbar"
import { ConfigSaveErrorAlert } from "@/components/shared/config-save-error-alert"
import { DataTable } from "@/components/shared/data-table"
import { ListPlaceholder } from "@/components/shared/list-placeholder"
import { PageHeader } from "@/components/shared/page-header"
import { TableSkeleton } from "@/components/shared/table-skeleton"
import { useRowSelection } from "@/hooks/use-row-selection"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import {
  buildUpdatedConfigForDnsServersDelete,
  getDnsServerDeleteReferenceInfo,
} from "@/pages/dns-servers-utils"

export function DnsServersPage() {
  const { t } = useTranslation()
  const [, navigate] = useLocation()
  const configMutationPending = useConfigMutationPending()
  const configQuery = useGetConfig()
  const postConfigMutation = usePostConfigMutation()

  const config = getConfigData(configQuery.data)
  const dnsServers = useMemo(() => config?.dns?.servers ?? [], [config])
  const serverRowIds = dnsServers.map((server) => server.tag)
  const serverSelection = useRowSelection(serverRowIds)

  const deleteServer = (serverTag: string) => {
    if (!config) {
      return
    }

    const { matchingRulesCount, usesFallback } =
      getDnsServerDeleteReferenceInfo(config, [serverTag])

    let shouldCleanupReferences = false
    if (matchingRulesCount > 0 || usesFallback) {
      shouldCleanupReferences = window.confirm(
        t("pages.dnsServers.delete.confirmWithReferences", {
          count: matchingRulesCount,
          fallbackSuffix: usesFallback
            ? t("pages.dnsServers.delete.fallbackSuffix")
            : "",
          serverTag,
        })
      )

      if (!shouldCleanupReferences) {
        return
      }
    }

    const updatedConfig = buildUpdatedConfigForDnsServersDelete(
      config,
      [serverTag],
      shouldCleanupReferences
    )

    postConfigMutation.mutate({ data: updatedConfig })
  }

  const deleteServersBulk = () => {
    if (!config || serverSelection.selectedCount === 0) {
      return
    }

    const selectedTags = [...serverSelection.selectedIds]
    const { matchingRulesCount, usesFallback } =
      getDnsServerDeleteReferenceInfo(config, selectedTags)
    const hasReferences = matchingRulesCount > 0 || usesFallback

    if (
      hasReferences &&
      !window.confirm(
        t("pages.dnsServers.bulk.confirmDelete", {
          tags: selectedTags.join(", "),
        })
      )
    ) {
      return
    }

    const updatedConfig = buildUpdatedConfigForDnsServersDelete(
      config,
      selectedTags,
      hasReferences
    )

    postConfigMutation.mutate(
      { data: updatedConfig },
      {
        onSuccess: () => {
          serverSelection.clear()
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
            onClick={() => navigate("/dns-servers/create")}
          >
            <Plus className="mr-1 h-4 w-4" />
            {t("pages.dnsServers.actions.add")}
          </Button>
        }
        description={t("pages.dnsServers.description")}
        title={t("pages.dnsServers.title")}
      />

      <ConfigSaveErrorAlert error={postConfigMutation.error} />

      {configQuery.isLoading ? (
        <TableSkeleton />
      ) : configQuery.isError ? (
        <ListPlaceholder
          description={t("pages.dnsServers.loadErrorDescription")}
          title={t("common.unableToLoadData")}
          variant="error"
        />
      ) : dnsServers.length === 0 ? (
        <ListPlaceholder
          description={t("pages.dnsServers.empty.description")}
          title={t("pages.dnsServers.empty.title")}
        />
      ) : (
        <div className="space-y-3">
          {serverSelection.hasSelection ? (
            <BulkSelectionToolbar
              countLabel={t("pages.dnsServers.bulk.selected", {
                count: serverSelection.selectedCount,
              })}
            >
              <Button
                disabled={configMutationPending}
                onClick={deleteServersBulk}
                size="sm"
                variant="destructive"
              >
                <Trash2 className="mr-1 h-4 w-4" />
                {t("pages.dnsServers.bulk.delete")}
              </Button>
            </BulkSelectionToolbar>
          ) : null}
          <DataTable
            headers={[
              t("pages.dnsServers.headers.name"),
              t("pages.dnsServers.headers.address"),
              t("pages.dnsServers.headers.outbound"),
              t("pages.dnsServers.headers.actions"),
            ]}
            rows={dnsServers.map((server) => [
              <div className="font-medium" key={`${server.tag}-tag`}>
                {server.tag}
              </div>,
              <span
                className="text-sm text-muted-foreground"
                key={`${server.tag}-address`}
              >
                {server.type === DnsServerType.keenetic
                  ? t("pages.dnsServers.keeneticAddress")
                  : server.address}
              </span>,
              <Badge
                key={`${server.tag}-detour`}
                variant={server.detour ? "outline" : "secondary"}
              >
                {server.detour || t("pages.dnsServers.none")}
              </Badge>,
              <ActionButtons
                actions={[
                  {
                    disabled: configMutationPending,
                    icon: <Pencil className="h-4 w-4" />,
                    label: t("common.edit"),
                    onClick: () =>
                      navigate(
                        `/dns-servers/${encodeURIComponent(server.tag)}/edit`
                      ),
                  },
                  {
                    disabled: configMutationPending,
                    icon: <Trash2 className="h-4 w-4" />,
                    label: t("common.delete"),
                    onClick: () => deleteServer(server.tag),
                  },
                ]}
                key={`${server.tag}-actions`}
              />,
            ])}
            selection={{
              rowIds: serverRowIds,
              selectedIds: serverSelection.selectedIds,
              disabled: configMutationPending,
              onToggle: serverSelection.toggleOne,
              onToggleAll: serverSelection.setAllVisible,
              selectAllLabel: t("common.selection.selectAll"),
              getRowLabel: (rowId) =>
                t("common.selection.selectRow", { rowLabel: rowId }),
            }}
          />
        </div>
      )}
    </div>
  )
}

function getConfigData(response: getConfigResponse | undefined) {
  if (!response || response.status !== 200) {
    return undefined
  }

  return response.data.config
}
