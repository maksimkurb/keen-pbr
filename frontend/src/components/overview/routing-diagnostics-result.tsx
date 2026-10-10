import { CircleOff } from "lucide-react"
import { useMemo, useState } from "react"
import { useTranslation } from "react-i18next"

import type {
  RoutingTestResponse,
  RuntimeOutboundState,
} from "@/api/generated/model"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Checkbox } from "@/components/ui/checkbox"
import {
  Accordion,
  AccordionItem,
  AccordionTrigger,
  AccordionPanel,
} from "@/components/ui/accordion"
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table"

import { IpSetStateIcon } from "./ipset-state-icon"
import {
  getRuleConditions,
  getVisibleRuleDiagnostics,
} from "./routing-diagnostics-utils"
import { RoutingRouteSummary } from "./routing-route-summary"
import { RoutingLegend } from "./routing-legend"

const emptyRuleDiagnostics: RoutingTestResponse["rule_diagnostics"] = []

export function RoutingDiagnosticsResult({
  diagnostics,
  onRefresh,
  isRefreshing,
  runtimeOutbounds,
}: {
  diagnostics: RoutingTestResponse
  onRefresh?: () => void
  isRefreshing?: boolean
  runtimeOutbounds?: RuntimeOutboundState[]
}) {
  const { t } = useTranslation()
  const [showAllRules, setShowAllRules] = useState(false)
  const ruleDiagnostics = diagnostics.rule_diagnostics ?? emptyRuleDiagnostics
  const visibleRuleDiagnostics = useMemo(
    () => getVisibleRuleDiagnostics(ruleDiagnostics, showAllRules),
    [ruleDiagnostics, showAllRules]
  )
  const ipRows = diagnostics.is_domain
    ? diagnostics.resolved_ips
    : [diagnostics.target]

  return (
    <div className="space-y-4">
      <RoutingRouteSummary
        diagnostics={diagnostics}
        onRefresh={onRefresh}
        isRefreshing={isRefreshing}
        runtimeOutbounds={runtimeOutbounds}
      />

      <Accordion className="rounded-lg border px-3">
        <AccordionItem>
          <AccordionTrigger className="py-3 font-medium">
            {t("overview.routingDiagnostics.ruleDetailsTitle")}
          </AccordionTrigger>
          <AccordionPanel className="pb-3">
            <div className="space-y-4">
              {(diagnostics.dns_error || diagnostics.warnings.length > 0) && (
                <Alert variant="warning">
                  <AlertDescription className="space-y-1 text-sm">
                    {diagnostics.dns_error ? (
                      <div>{diagnostics.dns_error}</div>
                    ) : null}
                    {diagnostics.warnings
                      .filter((warning) => warning !== diagnostics.dns_error)
                      .map((warning) => (
                        <div key={warning}>{warning}</div>
                      ))}
                  </AlertDescription>
                </Alert>
              )}

              {ruleDiagnostics.length > 0 ? (
                <div className="space-y-3">
                  <label className="flex items-center gap-2 text-sm text-muted-foreground">
                    <Checkbox
                      checked={showAllRules}
                      onCheckedChange={(checked) =>
                        setShowAllRules(checked === true)
                      }
                    />
                    <span>{t("overview.routingDiagnostics.showAllRules")}</span>
                  </label>

                  <div className="overflow-x-auto rounded-md border">
                    <Table className="min-w-[720px]">
                      <TableHeader className="bg-muted/40">
                        <TableRow>
                          <TableHead className="min-w-48 font-semibold">
                            <div>
                              {t("overview.routingDiagnostics.hostLabel", {
                                target: diagnostics.target,
                              })}
                            </div>
                          </TableHead>
                          {visibleRuleDiagnostics.map((rule) => (
                            <TableHead
                              key={`rule-head-${rule.rule_index}`}
                              className="min-w-52 text-center align-top"
                            >
                              <div className="space-y-1 py-1">
                                <div className="font-semibold">
                                  #{rule.rule_index + 1}
                                </div>
                                <div>{rule.outbound}</div>
                                <div className="text-xs text-muted-foreground">
                                  {rule.interface_name || t("common.noneShort")}
                                </div>
                                <RuleConditions rule={rule.rule} />
                              </div>
                            </TableHead>
                          ))}
                        </TableRow>
                        <TableRow>
                          <TableHead>
                            {t("overview.routingDiagnostics.inRuleLists")}
                          </TableHead>
                          {visibleRuleDiagnostics.map((rule) => (
                            <TableHead
                              key={`rule-list-${rule.rule_index}`}
                              className="text-center"
                            >
                              {rule.target_match ? (
                                <span className="text-xs font-medium text-green-700">
                                  {t("overview.routingDiagnostics.listMatch", {
                                    list: rule.target_match.list,
                                    via: rule.target_match.via,
                                  })}
                                </span>
                              ) : (
                                <CircleOff className="mx-auto h-5 w-5 text-gray-400" />
                              )}
                            </TableHead>
                          ))}
                        </TableRow>
                      </TableHeader>
                      <TableBody>
                        {ipRows.map((ip) => (
                          <TableRow key={ip}>
                            <TableCell className="font-mono text-sm">
                              {ip}
                            </TableCell>
                            {visibleRuleDiagnostics.map((rule) => {
                              const ipDiag = rule.ip_rows.find(
                                (item) => item.ip === ip
                              )
                              return (
                                <TableCell
                                  key={`cell-${rule.rule_index}-${ip}`}
                                  className="text-center"
                                >
                                  <IpSetStateIcon
                                    targetInLists={ipDiag?.in_lists ?? false}
                                    inIpset={ipDiag?.in_ipset}
                                  />
                                </TableCell>
                              )
                            })}
                          </TableRow>
                        ))}
                      </TableBody>
                    </Table>
                  </div>
                </div>
              ) : null}

              <RoutingLegend />
            </div>
          </AccordionPanel>
        </AccordionItem>
      </Accordion>
    </div>
  )
}

function RuleConditions({
  rule,
}: {
  rule: RoutingTestResponse["rule_diagnostics"][number]["rule"]
}) {
  const { t } = useTranslation()
  const conditions = getRuleConditions(rule)

  if (conditions.length === 0) {
    return (
      <div className="text-xs font-normal text-muted-foreground">
        {t("overview.routingDiagnostics.noConditions")}
      </div>
    )
  }

  return (
    <dl className="space-y-0.5 text-left text-xs font-normal text-muted-foreground">
      {conditions.map((condition) => (
        <div
          className="grid grid-cols-[auto_minmax(0,1fr)] gap-x-1"
          key={condition.key}
        >
          <dt className="text-foreground">
            {t(`overview.routingDiagnostics.conditions.${condition.key}`)}:
          </dt>
          <dd className="wrap-break-words min-w-0 whitespace-normal">
            {condition.value}
          </dd>
        </div>
      ))}
    </dl>
  )
}
