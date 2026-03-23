import { ArrowDown, ArrowUp, Pencil, Plus, Trash2 } from "lucide-react"
import { useState } from "react"
import { useTranslation } from "react-i18next"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { RouteRule } from "@/api/generated/model/routeRule"
import { usePostConfigMutation } from "@/api/mutations"
import { useGetConfig } from "@/api/queries"
import { selectConfig } from "@/api/selectors"
import { ActionButtons } from "@/components/shared/action-buttons"
import { DataTable } from "@/components/shared/data-table"
import { ListPlaceholder } from "@/components/shared/list-placeholder"
import { PageHeader } from "@/components/shared/page-header"
import { TableSkeleton } from "@/components/shared/table-skeleton"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { getApiErrorMessage, reorderRules } from "@/pages/routing-rules-utils"

export function RoutingRulesPage() {
  const { t } = useTranslation()
  const [, navigate] = useLocation()
  const [saveSuccessMessage, setSaveSuccessMessage] = useState<string | null>(
    null
  )
  const [mutationErrorMessage, setMutationErrorMessage] = useState<
    string | null
  >(null)

  const configQuery = useGetConfig()
  const loadedConfig = selectConfig(configQuery.data)
  const routeRules = loadedConfig?.route?.rules ?? []

  const tableRows = routeRules.map((rule: RouteRule, index: number) =>
    getRouteRuleRow(rule, index, t)
  )

  const postConfigMutation = usePostConfigMutation({
    mutation: {
      onSuccess: () => {
        setSaveSuccessMessage(t("pages.routingRules.messages.saved"))
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
            {t("pages.routingRules.actions.addRule")}
          </Button>
        }
        description={t("pages.routingRules.description")}
        title={t("pages.routingRules.title")}
      />

      {saveSuccessMessage ? (
        <Alert className="border-success/30 bg-success/5 text-success">
          <AlertDescription>{saveSuccessMessage}</AlertDescription>
        </Alert>
      ) : null}

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
      ) : tableRows.length === 0 ? (
        <ListPlaceholder
          description={t("pages.routingRules.empty.description")}
          title={t("pages.routingRules.empty.title")}
        />
      ) : (
        <DataTable
          headers={[
            t("pages.routingRules.headers.order"),
            t("pages.routingRules.headers.criteria"),
            t("pages.routingRules.headers.outbound"),
            t("pages.routingRules.headers.actions"),
          ]}
          rows={tableRows.map((row: ReturnType<typeof getRouteRuleRow>) => [
            <span className="font-medium" key={`${row.id}-order`}>
              #{row.order}
            </span>,
            <ul
              className="list-disc space-y-1 pl-5 text-sm"
              key={`${row.id}-conditions`}
            >
              {row.conditions.map((condition) => (
                <li
                  className="text-muted-foreground"
                  key={`${row.id}-${condition.label}`}
                >
                  <span className="font-medium text-foreground">
                    {condition.label}:
                  </span>{" "}
                  {condition.value}
                </li>
              ))}
            </ul>,
            <Badge key={`${row.id}-outbound`} variant="outline">
              {row.outbound}
            </Badge>,
            <ActionButtons
              actions={[
                {
                  icon: <ArrowUp className="h-4 w-4" />,
                  label: t("common.moveUp"),
                  onClick: () => handleMove(row.index, -1),
                },
                {
                  icon: <ArrowDown className="h-4 w-4" />,
                  label: t("common.moveDown"),
                  onClick: () => handleMove(row.index, 1),
                },
                {
                  icon: <Pencil className="h-4 w-4" />,
                  label: t("common.edit"),
                  onClick: () => navigate(`/routing-rules/${row.index}/edit`),
                },
                {
                  icon: <Trash2 className="h-4 w-4" />,
                  label: t("common.delete"),
                  onClick: () => handleDelete(row.index),
                },
              ]}
              key={`${row.id}-actions`}
            />,
          ])}
        />
      )}
    </div>
  )
}

function getRouteRuleRow(
  rule: RouteRule,
  index: number,
  t: (key: string) => string
) {
  const conditions = [
    {
      label: t("pages.routingRules.criteriaLabels.lists"),
      value: rule.list.join(", "),
    },
    {
      label: t("pages.routingRules.criteriaLabels.proto"),
      value: rule.proto,
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
  ].filter(
    (
      condition
    ): condition is {
      label: string
      value: string
    } => typeof condition.value === "string" && condition.value.trim().length > 0
  )

  return {
    id: `routing-rule-${index}`,
    index,
    order: index + 1,
    conditions,
    outbound: rule.outbound,
  }
}
