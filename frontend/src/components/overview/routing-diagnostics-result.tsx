import { CircleCheckBig, CircleOff, Link } from "lucide-react"
import { useMemo } from "react"
import { useTranslation } from "react-i18next"

import type { RoutingTestResponse } from "@/api/generated/model"
import { Alert, AlertDescription } from "@/components/ui/alert"
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table"

import { IpSetStateIcon } from "./ipset-state-icon"
import { RoutingLegend } from "./routing-legend"

export function RoutingDiagnosticsResult({
  diagnostics,
}: {
  diagnostics: RoutingTestResponse
}) {
  const { t } = useTranslation()
  const ruleDiagnostics = diagnostics.rule_diagnostics ?? []
  const ipRows = diagnostics.is_domain
    ? diagnostics.resolved_ips
    : [diagnostics.target]
  const routingResults = diagnostics.results

  const resultByIp = useMemo(() => {
    const map = new Map<
      string,
      (NonNullable<typeof routingResults>[number]) | undefined
    >()
    for (const entry of routingResults ?? []) {
      map.set(entry.ip, entry)
    }

    return map
  }, [routingResults])

  return (
    <div className="space-y-4">
      {(diagnostics.dns_error || diagnostics.no_matching_rule) && (
        <Alert className="border-amber-400/40 bg-amber-50 text-amber-900">
          <AlertDescription className="space-y-1 text-sm">
            {diagnostics.dns_error ? <div>{diagnostics.dns_error}</div> : null}
            {diagnostics.no_matching_rule ? (
              <div>{t("overview.routingDiagnostics.noMatchingRule")}</div>
            ) : null}
          </AlertDescription>
        </Alert>
      )}

      {ruleDiagnostics.length > 0 ? (
        <div className="overflow-x-auto rounded-md border">
          <Table className="min-w-[720px]">
            <TableHeader className="bg-muted/40">
              <TableRow>
                <TableHead className="font-semibold">
                  <div>
                    {t("overview.routingDiagnostics.hostLabel", {
                      target: diagnostics.target,
                    })}
                  </div>
                </TableHead>
                {ruleDiagnostics.map((rule) => (
                  <TableHead
                    className="text-center"
                    key={`rule-head-${rule.rule_index}`}
                  >
                    <div className="text-xs font-normal text-muted-foreground">
                      {t("overview.routingDiagnostics.ruleNumber", {
                        index: String(rule.rule_index + 1),
                      })}
                    </div>
                    <div>{rule.outbound}</div>
                    <div className="text-xs text-muted-foreground">
                      {rule.interface_name || t("common.noneShort")}
                    </div>
                    {rule.target_match?.list ? (
                      <div className="text-xs font-normal text-muted-foreground">
                        {t("overview.routingDiagnostics.matchedList", {
                          name: rule.target_match.list,
                        })}
                      </div>
                    ) : null}
                    {rule.target_match?.via ? (
                      <div className="inline-flex items-center justify-center gap-1 text-xs font-normal text-muted-foreground">
                        <Link className="h-3 w-3 shrink-0" />
                        <span>{rule.target_match.via}</span>
                      </div>
                    ) : null}
                  </TableHead>
                ))}
              </TableRow>
              <TableRow>
                <TableHead>
                  {t("overview.routingDiagnostics.inRuleLists")}
                </TableHead>
                {ruleDiagnostics.map((rule) => (
                  <TableHead
                    className="text-center"
                    key={`rule-list-${rule.rule_index}`}
                  >
                    {rule.target_in_lists ? (
                      <CircleCheckBig className="mx-auto h-5 w-5 text-green-600" />
                    ) : (
                      <CircleOff className="mx-auto h-5 w-5 text-gray-400" />
                    )}
                  </TableHead>
                ))}
              </TableRow>
            </TableHeader>
            <TableBody>
              {ipRows.map((ip) => {
                const ipResult = resultByIp.get(ip)

                return (
                  <TableRow key={ip}>
                    <TableCell className="max-w-[18rem] align-top font-mono text-sm">
                      <div>{ip}</div>
                      {ipResult?.list_match ? (
                        <div className="mt-1 break-words text-xs leading-snug text-muted-foreground">
                          {t(
                            "overview.routingDiagnostics.ipConfigurationMatch",
                            {
                              list: ipResult.list_match.list,
                              via: ipResult.list_match.via,
                              outbound: ipResult.expected_outbound,
                            },
                          )}
                        </div>
                      ) : null}
                    </TableCell>
                    {ruleDiagnostics.map((rule) => {
                      const ipDiag = rule.ip_rows.find((item) => item.ip === ip)

                      return (
                        <TableCell
                          className="text-center"
                          key={`cell-${rule.rule_index}-${ip}`}
                        >
                          <IpSetStateIcon
                            targetInLists={rule.target_in_lists}
                            inIpset={ipDiag?.in_ipset}
                          />
                        </TableCell>
                      )
                    })}
                  </TableRow>
                )
              })}
            </TableBody>
          </Table>
        </div>
      ) : null}

      <RoutingLegend />
      {ruleDiagnostics.length > 0 ? (
        <p className="text-xs text-muted-foreground">
          {t("overview.routingDiagnostics.winningRuleNote")}
        </p>
      ) : null}
    </div>
  )
}
