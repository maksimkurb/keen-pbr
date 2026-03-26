import { useQueryClient } from "@tanstack/react-query"
import { ExternalLink, Pencil, Plus, RefreshCw, Trash2 } from "lucide-react"
import { useMemo } from "react"
import { useTranslation } from "react-i18next"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
import { usePostConfigMutation } from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
import { useGetConfig } from "@/api/queries"
import { selectConfig } from "@/api/selectors"
import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable } from "@/components/shared/data-table"
import { ListPlaceholder } from "@/components/shared/list-placeholder"
import { PageHeader } from "@/components/shared/page-header"
import { StatsDisplay } from "@/components/shared/stats-display"
import { TableSkeleton } from "@/components/shared/table-skeleton"
import { Alert, AlertDescription } from "@/components/ui/alert"
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
  const { t } = useTranslation()
  const [, navigate] = useLocation()
  const queryClient = useQueryClient()
  const configQuery = useGetConfig()
  const loadedConfig = selectConfig(configQuery.data)

  const tableRows = useMemo(
    () => getTableRowsFromListMap(loadedConfig?.lists, t),
    [loadedConfig?.lists, t]
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
        ? t("pages.lists.delete.confirmWithReferences", { name: listId })
        : t("pages.lists.delete.confirm", { name: listId })

    if (!window.confirm(deletePrompt)) {
      return
    }

    postConfigMutation.mutate({ data: nextConfig })
  }

  return (
    <div className="space-y-6">
      <PageHeader
        actions={
          <Button onClick={() => navigate("/lists/create")}>
            <Plus className="mr-1 h-4 w-4" />
            {t("pages.lists.actions.new")}
          </Button>
        }
        description={t("pages.lists.description")}
        title={t("pages.lists.title")}
      />

      {postConfigMutation.error ? (
        <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription className="whitespace-pre-wrap">
            {getApiErrorMessage(postConfigMutation.error as ApiError)}
          </AlertDescription>
        </Alert>
      ) : null}

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
        <DataTable
          headers={[
            t("pages.lists.headers.name"),
            t("pages.lists.headers.type"),
            t("pages.lists.headers.stats"),
            t("pages.lists.headers.rules"),
            t("pages.lists.headers.actions"),
          ]}
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
              {getListSourceLabel(list.draft, t)}
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
                  ? [
                      {
                        icon: <RefreshCw className="h-4 w-4" />,
                        label: t("pages.lists.actions.update"),
                      },
                    ]
                  : []),
                {
                  icon: <Pencil className="h-4 w-4" />,
                  label: t("common.edit"),
                  onClick: () => navigate(`/lists/${list.id}/edit`),
                },
                {
                  icon: <Trash2 className="h-4 w-4" />,
                  label: t("common.delete"),
                  onClick: () => handleDelete(list.id),
                },
              ]}
              key={`${list.id}-actions`}
            />,
          ])}
        />
      )}
    </div>
  )
}

function getTableRowsFromListMap(
  lists: ConfigObject["lists"],
  t: (key: string) => string
): ListTableRow[] {
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
      locationLabel:
        listConfig.file || listConfig.url || t("pages.lists.location.inline"),
      locationIcon: listConfig.url ? "external" : undefined,
      typeVariant: listConfig.url
        ? "default"
        : listConfig.file
          ? "secondary"
          : "outline",
      rule: t("pages.lists.rule.configured"),
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
