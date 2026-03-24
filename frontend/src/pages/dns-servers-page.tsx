import { Pencil, Trash2 } from "lucide-react"
import { useMemo } from "react"
import { useTranslation } from "react-i18next"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { getConfigResponse } from "@/api/generated/keen-api"
import type { ConfigObject } from "@/api/generated/model/configObject"
import { usePostConfigMutation } from "@/api/mutations"
import { useGetConfig } from "@/api/queries"
import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable } from "@/components/shared/data-table"
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
  const configQuery = useGetConfig()
  const postConfigMutation = usePostConfigMutation()

  const config = getConfigData(configQuery.data)
  const dnsServers = useMemo(() => config?.dns?.servers ?? [], [config])

  const mutationErrorMessage = getApiErrorMessage(
    postConfigMutation.error as ApiError | null
  )

  const deleteServer = (serverTag: string) => {
    if (!config) {
      return
    }

    const dnsConfig = config.dns
    const allRules = dnsConfig?.rules ?? []
    const matchingRules = allRules.filter((rule) => rule.server === serverTag)
    const usesFallback = dnsConfig?.fallback === serverTag

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
      shouldCleanupReferences && usesFallback ? undefined : dnsConfig?.fallback

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
          <Button onClick={() => navigate("/dns-servers/create")}>
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
              {server.address}
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
                  icon: <Pencil className="h-4 w-4" />,
                  label: t("common.edit"),
                  onClick: () =>
                    navigate(
                      `/dns-servers/${encodeURIComponent(server.tag)}/edit`
                    ),
                },
                {
                  icon: <Trash2 className="h-4 w-4" />,
                  label: t("common.delete"),
                  onClick: () => deleteServer(server.tag),
                },
              ]}
              key={`${server.tag}-actions`}
            />,
          ])}
        />
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
