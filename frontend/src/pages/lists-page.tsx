import { useQueryClient } from "@tanstack/react-query"
import { ExternalLink, Pencil, RefreshCw, Trash2 } from "lucide-react"
import { useMemo } from "react"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
import { usePostConfigMutation } from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
import { useGetConfig } from "@/api/queries"
import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable } from "@/components/shared/data-table"
import { PageHeader } from "@/components/shared/page-header"
import { StatsDisplay } from "@/components/shared/stats-display"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"

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
  typeVariant?: "default" | "secondary" | "outline"
  rule: string
  stats: {
    totalHosts: number
    ipv4Subnets: number
    ipv6Subnets: number
  }
  canRefresh?: boolean
}

export function ListsPage() {
  const [, navigate] = useLocation()
  const queryClient = useQueryClient()
  const configQuery = useGetConfig()
  const loadedConfig = configQuery.data?.data

  const tableRows = useMemo(
    () => getTableRowsFromListMap(loadedConfig?.lists),
    [loadedConfig?.lists]
  )

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
        return !nextRule || nextRule.list.length !== rule.list.length
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
        ? `Delete list "${listId}" and remove its references from routing/dns rules?`
        : `Delete list "${listId}"?`

    if (!window.confirm(deletePrompt)) {
      return
    }

    postConfigMutation.mutate({ data: nextConfig })
  }

  return (
    <div className="space-y-6">
      <PageHeader
        actions={
          <Button onClick={() => navigate("/lists/create")}>New list</Button>
        }
        description="Manage domain and IP lists used by routing rules."
        title="Lists"
      />

      {postConfigMutation.error ? (
        <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription>
            {getApiErrorMessage(postConfigMutation.error as ApiError)}
          </AlertDescription>
        </Alert>
      ) : null}

      <DataTable
        headers={["Name", "Type", "Stats", "Rules", "Actions"]}
        rows={tableRows.map((list) => [
          <div className="space-y-1" key={`${list.id}-name`}>
            <div className="flex items-center gap-2 font-medium">
              {list.draft.name}
              {list.locationIcon === "external" ? (
                <ExternalLink className="h-3 w-3 text-muted-foreground" />
              ) : null}
            </div>
            <div className="text-sm text-muted-foreground md:text-xs">
              {list.locationLabel}
            </div>
          </div>,
          <Badge key={`${list.id}-type`} variant={list.typeVariant}>
            {getListSourceLabel(list.draft)}
          </Badge>,
          <StatsDisplay
            ipv4Subnets={list.stats.ipv4Subnets}
            ipv6Subnets={list.stats.ipv6Subnets}
            key={`${list.id}-stats`}
            totalHosts={list.stats.totalHosts}
          />,
          <Badge key={`${list.id}-rule`} variant="outline">
            {list.rule}
          </Badge>,
          <ActionButtons
            actions={[
              ...(list.canRefresh
                ? [{ icon: <RefreshCw className="h-4 w-4" />, label: "Update" }]
                : []),
              {
                icon: <Pencil className="h-4 w-4" />,
                label: "Edit",
                onClick: () => navigate(`/lists/${list.id}/edit`),
              },
              {
                icon: <Trash2 className="h-4 w-4" />,
                label: "Delete",
                onClick: () => handleDelete(list.id),
              },
            ]}
            key={`${list.id}-actions`}
          />,
        ])}
      />
    </div>
  )
}

function getTableRowsFromListMap(lists: ConfigObject["lists"]): ListTableRow[] {
  return Object.entries(lists ?? {}).map(([name, listConfig]) => {
    const domains = listConfig.domains ?? []
    const ipCidrs = listConfig.ip_cidrs ?? []

    return {
      id: name,
      draft: {
        name,
        ttlMs: String(listConfig.ttl_ms ?? 300000),
        domains: domains.join("\n"),
        ipCidrs: ipCidrs.join("\n"),
        url: listConfig.url ?? "",
        file: listConfig.file ?? "",
      },
      locationLabel: listConfig.file || listConfig.url || "inline",
      locationIcon: listConfig.url ? "external" : undefined,
      typeVariant: listConfig.url
        ? "default"
        : listConfig.file
          ? "secondary"
          : "outline",
      rule: "configured",
      stats: {
        totalHosts: domains.length + ipCidrs.length,
        ipv4Subnets: ipCidrs.filter((value) => value.includes(".")).length,
        ipv6Subnets: ipCidrs.filter((value) => value.includes(":")).length,
      },
      canRefresh: Boolean(listConfig.url),
    }
  })
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
          list: rule.list.filter((name) => name !== listId),
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

function getApiErrorMessage(error: ApiError) {
  const details = error.details
    ? ` Details: ${JSON.stringify(error.details)}`
    : ""
  return `${error.message}.${details}`
}

function getListSourceLabel(draft: ListDraft) {
  const sources = [
    draft.url ? "url" : null,
    draft.file ? "file" : null,
    draft.domains ? "domains" : null,
    draft.ipCidrs ? "ip_cidrs" : null,
  ].filter(Boolean)

  if (sources.length === 0) {
    return "empty"
  }

  if (sources.length === 1) {
    return sources[0]
  }

  return "combined"
}
