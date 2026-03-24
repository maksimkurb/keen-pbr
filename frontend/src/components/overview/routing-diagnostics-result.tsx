import { CircleCheckBig, CircleOff, Link } from "lucide-react"
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
                  <TableHead key={`rule-head-${rule.rule_index}`} className="text-center">
                    <div>{rule.outbound}</div>
                    <div className="text-xs text-muted-foreground">
                      {rule.interface_name || t("common.noneShort")}
                    </div>
                    {rule.target_match?.via ? (
                      <div className="inline-flex items-center justify-center gap-1 text-xs font-normal text-muted-foreground">
                        <Link className="h-3 w-3" />
                        <span>{rule.target_match.via}</span>
                      </div>
                    ) : null}
                  </TableHead>
                ))}
              </TableRow>
              <TableRow>
                <TableHead>{t("overview.routingDiagnostics.inRuleLists")}</TableHead>
                {ruleDiagnostics.map((rule) => (
                  <TableHead key={`rule-list-${rule.rule_index}`} className="text-center">
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
              {ipRows.map((ip) => (
                <TableRow key={ip}>
                  <TableCell className="font-mono text-sm">{ip}</TableCell>
                  {ruleDiagnostics.map((rule) => {
                    const ipDiag = rule.ip_rows.find((item) => item.ip === ip)
                    return (
                      <TableCell key={`cell-${rule.rule_index}-${ip}`} className="text-center">
                        <IpSetStateIcon
                          targetInLists={rule.target_in_lists}
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
      ) : null}

      <RoutingLegend />
    </div>
  )
}
