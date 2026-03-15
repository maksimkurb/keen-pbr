import { ArrowDown, ArrowUp, Pencil, Plus, Trash2 } from "lucide-react"
import { useMemo, useState } from "react"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { RouteRule } from "@/api/generated/model/routeRule"
import { usePostConfigMutation } from "@/api/mutations"
import { useGetConfig } from "@/api/queries"
import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable } from "@/components/shared/data-table"
import { PageHeader } from "@/components/shared/page-header"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import {
  getApiErrorMessage,
  getRuleDetails,
  reorderRules,
} from "@/pages/routing-rules-utils"

export function RoutingRulesPage() {
  const [, navigate] = useLocation()
  const [saveSuccessMessage, setSaveSuccessMessage] = useState<string | null>(
    null
  )
  const [mutationErrorMessage, setMutationErrorMessage] = useState<
    string | null
  >(null)

  const configQuery = useGetConfig()
  const loadedConfig =
    configQuery.data?.status === 200 ? configQuery.data.data : undefined
  const routeRules = loadedConfig?.route?.rules ?? []

  const tableRows = useMemo(
    () => routeRules.map((rule: RouteRule, index: number) => getRouteRuleRow(rule, index)),
    [routeRules]
  )

  const postConfigMutation = usePostConfigMutation({
    mutation: {
      onSuccess: () => {
        setSaveSuccessMessage("Routing rules saved.")
        setMutationErrorMessage(null)
      },
      onError: (error) => {
        const apiError = error as ApiError
        setSaveSuccessMessage(null)
        setMutationErrorMessage(getApiErrorMessage(apiError))
      },
    },
  })

  const persistRules = (
    config: NonNullable<typeof loadedConfig>,
    nextRules: RouteRule[]
  ) => {
    setSaveSuccessMessage(null)
    setMutationErrorMessage(null)

    postConfigMutation.mutate({
      data: {
        ...config,
        route: {
          ...config.route,
          rules: nextRules,
        },
      },
    })
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

  return (
    <div className="space-y-6">
      <PageHeader
        actions={
          <Button onClick={() => navigate("/routing-rules/create")}>
            <Plus className="mr-1 h-4 w-4" />
            Add rule
          </Button>
        }
        description="Manage route rule order and field-level match criteria."
        title="Routing rules"
      />

      {saveSuccessMessage ? (
        <Alert className="border-success/30 bg-success/5 text-success">
          <AlertDescription>{saveSuccessMessage}</AlertDescription>
        </Alert>
      ) : null}

      {mutationErrorMessage ? (
        <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription>{mutationErrorMessage}</AlertDescription>
        </Alert>
      ) : null}

      <DataTable
        headers={["Order", "Lists", "Outbound", "Matchers", "Actions"]}
        rows={tableRows.map((row: ReturnType<typeof getRouteRuleRow>) => [
          <span className="font-medium" key={`${row.id}-order`}>
            #{row.order}
          </span>,
          <div className="flex flex-wrap gap-2" key={`${row.id}-lists`}>
            {row.lists.map((listName: string) => (
              <Badge key={`${row.id}-${listName}`} variant="outline">
                {listName}
              </Badge>
            ))}
          </div>,
          <div className="space-y-1" key={`${row.id}-outbound`}>
            <Badge>{row.outbound}</Badge>
            <div className="text-sm text-muted-foreground">
              proto: {row.proto}
            </div>
          </div>,
          <span className="text-sm text-muted-foreground" key={`${row.id}-details`}>
            {row.details}
          </span>,
          <ActionButtons
            actions={[
              {
                icon: <ArrowUp className="h-4 w-4" />,
                label: "Move up",
                onClick: () => handleMove(row.index, -1),
              },
              {
                icon: <ArrowDown className="h-4 w-4" />,
                label: "Move down",
                onClick: () => handleMove(row.index, 1),
              },
              {
                icon: <Pencil className="h-4 w-4" />,
                label: "Edit",
                onClick: () => navigate(`/routing-rules/${row.index}/edit`),
              },
              {
                icon: <Trash2 className="h-4 w-4" />,
                label: "Delete",
                onClick: () => handleDelete(row.index),
              },
            ]}
            key={`${row.id}-actions`}
          />,
        ])}
      />
    </div>
  )
}

function getRouteRuleRow(rule: RouteRule, index: number) {
  return {
    id: `routing-rule-${index}`,
    index,
    order: index + 1,
    lists: rule.list,
    outbound: rule.outbound,
    proto: rule.proto || "any",
    details: getRuleDetails(rule),
  }
}
