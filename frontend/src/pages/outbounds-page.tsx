import { Pencil, Trash2 } from "lucide-react"
import { useState } from "react"
import { useTranslation } from "react-i18next"

import { useQueryClient } from "@tanstack/react-query"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { Outbound } from "@/api/generated/model/outbound"
import { usePostConfigMutation } from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
import { useGetConfig } from "@/api/queries"
import { selectConfig, selectOutbounds } from "@/api/selectors"
import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable } from "@/components/shared/data-table"
import { ListPlaceholder } from "@/components/shared/list-placeholder"
import { PageHeader } from "@/components/shared/page-header"
import { TableSkeleton } from "@/components/shared/table-skeleton"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { getApiErrorMessage } from "@/lib/api-errors"

type OutboundItem = {
  id: string
  tag: string
  type: Outbound["type"]
  typeVariant?: "default" | "outline" | "secondary"
  summary: string
}

export function OutboundsPage() {
  const { t } = useTranslation()
  const queryClient = useQueryClient()
  const [, navigate] = useLocation()
  const configQuery = useGetConfig()
  const loadedConfig = selectConfig(configQuery.data)
  const [mutationErrorMessage, setMutationErrorMessage] = useState<
    string | null
  >(null)

  const outboundItems = selectOutbounds(loadedConfig).map((outbound) =>
    mapOutboundToItem(outbound, t)
  )

  const postConfigMutation = usePostConfigMutation({
    mutation: {
      onSuccess: async () => {
        setMutationErrorMessage(null)
        await Promise.all([
          queryClient.invalidateQueries({ queryKey: queryKeys.config() }),
          queryClient.invalidateQueries({
            queryKey: queryKeys.healthService(),
          }),
          queryClient.invalidateQueries({
            queryKey: queryKeys.healthRouting(),
          }),
        ])
      },
      onError: (error) => {
        setMutationErrorMessage(getApiErrorMessage(error as ApiError))
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
      setMutationErrorMessage(urltestReferencesError)
      return
    }

    const updatedConfig: ConfigObject = {
      ...loadedConfig,
      outbounds: nextOutbounds,
    }

    setMutationErrorMessage(null)
    postConfigMutation.mutate({ data: updatedConfig })
  }

  return (
    <div className="space-y-6">
      <PageHeader
        actions={
          <Button onClick={() => navigate("/outbounds/create")}>
            {t("pages.outbounds.actions.new")}
          </Button>
        }
        description={t("pages.outbounds.description")}
        title={t("pages.outbounds.title")}
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
        <DataTable
          headers={[
            t("pages.outbounds.headers.tag"),
            t("pages.outbounds.headers.type"),
            t("pages.outbounds.headers.summary"),
            t("pages.outbounds.headers.actions"),
          ]}
          rows={outboundItems.map((outbound) => [
            <div className="font-medium" key={`${outbound.id}-tag`}>
              {outbound.tag}
            </div>,
            <Badge key={`${outbound.id}-type`} variant={outbound.typeVariant}>
              {outbound.type}
            </Badge>,
            <span
              className="text-sm text-muted-foreground"
              key={`${outbound.id}-summary`}
            >
              {outbound.summary}
            </span>,
            <ActionButtons
              actions={[
                {
                  icon: <Pencil className="h-4 w-4" />,
                  label: t("common.edit"),
                  onClick: () => navigate(`/outbounds/${outbound.id}/edit`),
                },
                {
                  icon: <Trash2 className="h-4 w-4" />,
                  label: t("common.delete"),
                  onClick: () => handleDelete(outbound.id),
                },
              ]}
              key={`${outbound.id}-actions`}
            />,
          ])}
        />
      )}
    </div>
  )
}

function mapOutboundToItem(
  outbound: Outbound,
  t: (key: string, options?: Record<string, unknown>) => string
): OutboundItem {
  return {
    id: outbound.tag,
    tag: outbound.tag,
    type: outbound.type,
    summary: getOutboundSummary(outbound, t),
    typeVariant: outbound.type === "interface" ? "outline" : undefined,
  }
}

function getOutboundSummary(
  outbound: Outbound,
  t: (key: string, options?: Record<string, unknown>) => string
): string {
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
