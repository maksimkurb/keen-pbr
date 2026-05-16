import { Pencil, Plus, Trash2 } from "lucide-react"
import { useMemo, useState } from "react"
import { useTranslation } from "react-i18next"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { getConfigResponse } from "@/api/generated/keen-api"
import type { ConfigObject } from "@/api/generated/model/configObject"
import { DnsServerType } from "@/api/generated/model/dnsServerType"
import { usePostConfigMutation, useConfigMutationPending } from "@/api/mutations"
import { useGetConfig } from "@/api/queries"
import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable, type DataTableSelection } from "@/components/shared/data-table"
import { ListPlaceholder } from "@/components/shared/list-placeholder"
import { PageHeader } from "@/components/shared/page-header"
import { TableSkeleton } from "@/components/shared/table-skeleton"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { getApiErrorMessage } from "@/lib/api-errors"

export function DnsServersPage() {
  const { t } = useTranslation()
  const [, navigate] = useLocation()
  const configMutationPending = useConfigMutationPending()
  const configQuery = useGetConfig()
  const postConfigMutation = usePostConfigMutation()

  const config = getConfigData(configQuery.data)
  const dnsServers = useMemo(() => config?.dns?.servers ?? [], [config])

  const rowIdsRaw = dnsServers.map((server) => server.tag)

  const [selectedDnsServerTags, setSelectedDnsServerTags] = useState<Set<string>>(
    () => new Set(),
  )

  const validDnsServerTagSet = useMemo(
    () => new Set(dnsServers.map((server) => server.tag)),
    [dnsServers],
  )

  const selectedDnsServerTagsResolved = useMemo(() => {
    const next = new Set<string>()
    for (const tag of selectedDnsServerTags) {
      if (validDnsServerTagSet.has(tag)) {
        next.add(tag)
      }
    }

    return next
  }, [selectedDnsServerTags, validDnsServerTagSet])

  const dnsServerSelectionProps: DataTableSelection = {
    rowIds: rowIdsRaw,
    selectedIds: selectedDnsServerTagsResolved,
    selectionDisabled: configMutationPending,
    selectAllAriaLabel: t("common.selection.selectAll"),
    selectRowAriaLabel: (rowTag: string) =>
      t("common.selection.selectRow", { rowLabel: rowTag }),
    selectAllTooltip: t("common.selection.selectAll"),
    onToggleRow: (tag: string) => {
      setSelectedDnsServerTags((previous) => {
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
      setSelectedDnsServerTags(
        selectedAll ? new Set(rowIdsRaw) : new Set(),
      )
    },
  }

  const mutationErrorMessage = getApiErrorMessage(
    postConfigMutation.error as ApiError | null
  )

  const deleteServersBulk = () => {
    if (!config || selectedDnsServerTagsResolved.size === 0) {
      return
    }

    const dnsConfig = config.dns
    const allRules = dnsConfig?.rules ?? []
    const fallbackServers = dnsConfig?.fallback ?? []
    const selectedTagsArray = [...selectedDnsServerTagsResolved]
    const tagSet = new Set(selectedDnsServerTagsResolved)

    const matchingRulesCount = allRules.filter((rule) =>
      selectedTagsArray.includes(rule.server),
    ).length
    const usesFallback = fallbackServers.some((tag) => tagSet.has(tag))

    let stripReferencesFromConfig = !(matchingRulesCount > 0 || usesFallback)

    if (matchingRulesCount > 0 || usesFallback) {
      stripReferencesFromConfig = window.confirm(
        t("pages.dnsServers.bulk.confirmDelete", {
          tags: selectedTagsArray.join(", "),
        }),
      )

      if (!stripReferencesFromConfig) {
        return
      }
    }

    const updatedConfig = {
      ...config,
      dns: {
        ...(dnsConfig ?? {}),
        servers: dnsServers.filter((server) => !tagSet.has(server.tag)),
        rules: stripReferencesFromConfig
          ? allRules.filter((rule) => !tagSet.has(rule.server))
          : allRules,
        fallback: stripReferencesFromConfig
          ? fallbackServers.filter((tag) => !tagSet.has(tag))
          : fallbackServers,
      },
    } satisfies ConfigObject

    postConfigMutation.mutate(
      { data: updatedConfig },
      {
        onSuccess: () => {
          setSelectedDnsServerTags(new Set())
        },
      },
    )
  }

  const deleteServer = (serverTag: string) => {
    if (!config) {
      return
    }

    const dnsConfig = config.dns
    const allRules = dnsConfig?.rules ?? []
    const matchingRules = allRules.filter((rule) => rule.server === serverTag)
    const fallbackServers = dnsConfig?.fallback ?? []
    const usesFallback = fallbackServers.includes(serverTag)

    let shouldCleanupReferences = false
    if (matchingRules.length > 0 || usesFallback) {
      shouldCleanupReferences = window.confirm(
        t("pages.dnsServers.delete.confirmWithReferences", {
          count: matchingRules.length,
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

    const nextServers = dnsServers.filter((server) => server.tag !== serverTag)
    const nextRules = shouldCleanupReferences
      ? allRules.filter((rule) => rule.server !== serverTag)
      : allRules
    const nextFallback =
      shouldCleanupReferences && usesFallback
        ? fallbackServers.filter((tag) => tag !== serverTag)
        : fallbackServers

    const updatedConfig = {
      ...config,
      dns: {
        ...(dnsConfig ?? {}),
        servers: nextServers,
        rules: nextRules,
        fallback: nextFallback,
      },
    } satisfies ConfigObject

    postConfigMutation.mutate({ data: updatedConfig })
  }

  return (
    <div className="space-y-6">
      <PageHeader
        actions={
          <Button disabled={configMutationPending} onClick={() => navigate("/dns-servers/create")}>
            <Plus className="mr-1 h-4 w-4" />
            {t("pages.dnsServers.actions.add")}
          </Button>
        }
        description={t("pages.dnsServers.description")}
        title={t("pages.dnsServers.title")}
      />

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
          {selectedDnsServerTagsResolved.size > 0 ? (
            <div className="flex flex-wrap items-center gap-2 rounded-lg border bg-muted/20 px-3 py-2">
              <span className="text-sm font-medium tabular-nums">
                {t("pages.dnsServers.bulk.selected", {
                  count: selectedDnsServerTagsResolved.size,
                })}
              </span>
              <Button
                disabled={configMutationPending}
                onClick={() => deleteServersBulk()}
                size="sm"
                variant="destructive"
              >
                <Trash2 className="mr-1 h-4 w-4" />
                {t("pages.dnsServers.bulk.delete")}
              </Button>
            </div>
          ) : null}
          <DataTable
            headers={[
              t("pages.dnsServers.headers.name"),
              t("pages.dnsServers.headers.address"),
              t("pages.dnsServers.headers.outbound"),
              t("pages.dnsServers.headers.actions"),
            ]}
            narrowColumns={[0]}
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
            selection={dnsServerSelectionProps}
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
