import { Pencil, Trash2 } from "lucide-react"
import { useState } from "react"

import { useQueryClient } from "@tanstack/react-query"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { Outbound } from "@/api/generated/model/outbound"
import { usePostConfigMutation } from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
import { useGetConfig } from "@/api/queries"
import { selectOutbounds } from "@/api/selectors"
import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable } from "@/components/shared/data-table"
import { ListPlaceholder } from "@/components/shared/list-placeholder"
import { PageHeader } from "@/components/shared/page-header"
import { TableSkeleton } from "@/components/shared/table-skeleton"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"

type OutboundItem = {
  id: string
  tag: string
  type: Outbound["type"]
  typeVariant?: "default" | "outline" | "secondary"
  summary: string
}

export function OutboundsPage() {
  const queryClient = useQueryClient()
  const [, navigate] = useLocation()
  const configQuery = useGetConfig()
  const loadedConfig = configQuery.data?.data
  const [mutationErrorMessage, setMutationErrorMessage] = useState<
    string | null
  >(null)

  const outboundItems = selectOutbounds(loadedConfig).map(mapOutboundToItem)

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
    const urltestReferencesError = validateUrltestGroupReferences(nextOutbounds)

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
            New outbound
          </Button>
        }
        description="Configured outbounds and urltest behavior."
        title="Outbounds"
      />

      {mutationErrorMessage ? (
        <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription>{mutationErrorMessage}</AlertDescription>
        </Alert>
      ) : null}

      {configQuery.isLoading ? (
        <TableSkeleton />
      ) : configQuery.isError ? (
        <ListPlaceholder
          description="We can't load outbounds right now. Try refreshing the page."
          title="Unable to load data"
          variant="error"
        />
      ) : outboundItems.length === 0 ? (
        <ListPlaceholder
          description="Add an outbound to start building routing behavior."
          title="No outbounds yet"
        />
      ) : (
        <DataTable
          headers={["Tag", "Type", "Summary", "Actions"]}
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
                  label: "Edit",
                  onClick: () => navigate(`/outbounds/${outbound.id}/edit`),
                },
                {
                  icon: <Trash2 className="h-4 w-4" />,
                  label: "Delete",
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

function mapOutboundToItem(outbound: Outbound): OutboundItem {
  return {
    id: outbound.tag,
    tag: outbound.tag,
    type: outbound.type,
    summary: getOutboundSummary(outbound),
    typeVariant: outbound.type === "interface" ? "outline" : undefined,
  }
}

function getOutboundSummary(outbound: Outbound): string {
  if (outbound.type === "interface") {
    return `ifname=${outbound.interface ?? "-"}`
  }

  if (outbound.type === "table") {
    return `table=${outbound.table ?? "-"}`
  }

  if (outbound.type === "urltest") {
    const allOutbounds =
      outbound.outbound_groups?.flatMap((group) => group.outbounds) ?? []
    return `outbounds=${allOutbounds.join(",")}`
  }

  return "-"
}

function validateUrltestGroupReferences(outbounds: Outbound[]): string | null {
  const tags = new Set(outbounds.map((outbound) => outbound.tag))

  for (const outbound of outbounds) {
    if (outbound.type !== "urltest") {
      continue
    }

    for (const group of outbound.outbound_groups ?? []) {
      for (const referencedTag of group.outbounds) {
        if (!tags.has(referencedTag)) {
          return `Outbound "${outbound.tag}" references missing outbound tag "${referencedTag}".`
        }
      }
    }
  }

  return null
}

function getApiErrorMessage(error: ApiError): string {
  if (error.data && typeof error.data === "object" && "message" in error.data) {
    const message = error.data.message
    if (typeof message === "string" && message.length > 0) {
      return message
    }
  }

  return "Failed to save outbound configuration."
}
