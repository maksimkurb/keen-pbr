import {
  Check,
  CircleCheck,
  HelpCircle,
  Minus,
  RefreshCw,
  TriangleAlert,
} from "lucide-react"
import type { ReactNode } from "react"
import { Trans, useTranslation } from "react-i18next"
import type {
  RoutingTestResponse,
  RuntimeOutboundState,
} from "@/api/generated/model"
import { cn } from "@/lib/utils"
import { Button } from "@/components/ui/button"
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip"
import {
  getExpectedRoutingRule,
  getRuleConditions,
  getRoutingDiagnosticIssue,
} from "./routing-diagnostics-utils"

type StepTone = "ok" | "warn" | "neutral"

export function RoutingRouteSummary({
  diagnostics,
  onRefresh,
  isRefreshing = false,
  runtimeOutbounds = [],
}: {
  diagnostics: RoutingTestResponse
  onRefresh?: () => void
  isRefreshing?: boolean
  runtimeOutbounds?: RuntimeOutboundState[]
}) {
  const { t } = useTranslation()
  const label = (tag: string) =>
    tag === "(default)"
      ? t("overview.routingDiagnostics.defaultRoute")
      : tag === "(unknown)"
        ? t("overview.routingDiagnostics.unknownRoute")
        : tag
  const dnsMissing =
    diagnostics.is_domain && diagnostics.resolved_ips.length === 0
  const entries = diagnostics.results.map((result) => {
    const rule = getExpectedRoutingRule(diagnostics.rule_diagnostics, result)
    const listMatch =
      diagnostics.rule_diagnostics.find(
        (item) =>
          item.rule.enabled !== false &&
          item.outbound === result.expected_outbound
      )?.target_match ??
      result.list_match ??
      diagnostics.rule_diagnostics
        .filter((item) => item.rule.enabled !== false)
        .flatMap((item) => item.ip_rows)
        .find((row) => row.ip === result.ip && row.in_lists)?.list_match
    return {
      result,
      rule,
      listMatch,
      issue: getRoutingDiagnosticIssue(diagnostics, result),
      row: rule?.ip_rows.find((row) => row.ip === result.ip),
    }
  })
  const listGroups = new Map<string, typeof entries>()
  const ruleGroups = new Map<string, typeof entries>()
  const issueGroups = new Map<string, typeof entries>()
  for (const entry of entries) {
    const listKey = entry.listMatch
      ? JSON.stringify([entry.listMatch.list, entry.listMatch.via])
      : entry.rule && !entry.rule.rule.list?.length
        ? "criteria"
        : "none"
    const ruleKey = JSON.stringify([
      entry.rule?.rule_index,
      entry.result.expected_outbound,
      entry.issue,
    ])
    listGroups.set(listKey, [...(listGroups.get(listKey) ?? []), entry])
    ruleGroups.set(ruleKey, [...(ruleGroups.get(ruleKey) ?? []), entry])
    if (entry.issue !== "ok")
      issueGroups.set(entry.issue, [
        ...(issueGroups.get(entry.issue) ?? []),
        entry,
      ])
  }
  const allOk =
    entries.length > 0 && entries.every((entry) => entry.issue === "ok")
  const ruleUnknown = entries.some((entry) => entry.issue === "criteria")
  const heading = allOk
    ? t(
        entries.length > 1
          ? "overview.routingDiagnostics.trace.allRoutesMatch"
          : "overview.routingDiagnostics.trace.issues.ok.title"
      )
    : issueGroups.size === 1
      ? t(
          `overview.routingDiagnostics.trace.issues.${issueGroups.keys().next().value}.title`
        )
      : t("overview.routingDiagnostics.trace.routeProblems")
  const codes = (ips: string[]) => (
    <span className="inline-flex min-w-0 flex-wrap gap-x-2 gap-y-1">
      {ips.map((ip) => (
        <code key={ip} className="wrap-break-words font-mono text-xs">
          {ip}
        </code>
      ))}
    </span>
  )
  const firewallGroups = new Map<string, typeof entries>()
  for (const entry of entries) {
    const key = JSON.stringify([
      entry.result.expected_outbound,
      entry.result.actual_outbound,
      entry.issue,
    ])
    firewallGroups.set(key, [...(firewallGroups.get(key) ?? []), entry])
  }
  const steps: { title: string; tone: StepTone; content: ReactNode }[] = [
    {
      title: t("overview.routingDiagnostics.trace.dns"),
      tone: dnsMissing ? "warn" : diagnostics.is_domain ? "ok" : "neutral",
      content: dnsMissing
        ? t("overview.routingDiagnostics.trace.dnsEmpty")
        : diagnostics.is_domain
          ? codes(diagnostics.resolved_ips)
          : t("overview.routingDiagnostics.trace.literalIp", {
              ip: diagnostics.target,
            }),
    },
    {
      title: t("overview.routingDiagnostics.trace.list"),
      tone: entries.some((entry) => entry.listMatch) ? "ok" : "neutral",
      content: (
        <div className="space-y-2">
          {Array.from(listGroups.values()).map((group, index) => {
            const entry = group[0]
            return (
              <div key={index}>
                {entry.listMatch ? (
                  <Trans
                    i18nKey="overview.routingDiagnostics.resultListMatchVia"
                    values={{ ...entry.listMatch }}
                    components={{
                      code: (
                        <code className="rounded bg-muted px-1 font-mono text-xs text-foreground" />
                      ),
                    }}
                  />
                ) : entry.rule && !entry.rule.rule.list?.length ? (
                  t("overview.routingDiagnostics.trace.noListNeeded")
                ) : (
                  t("overview.routingDiagnostics.noListMatch")
                )}
              </div>
            )
          })}
        </div>
      ),
    },
    {
      title: t("overview.routingDiagnostics.trace.rule"),
      tone: ruleUnknown
        ? "warn"
        : entries.some((entry) => entry.rule)
          ? "ok"
          : "neutral",
      content: (
        <div className="space-y-3">
          {Array.from(ruleGroups.values()).map((group, index) => {
            const { rule, result } = group[0]
            return (
              <div key={index}>
                <p>
                  {rule
                    ? t("overview.routingDiagnostics.trace.selectedRule", {
                        rule: rule.rule_index + 1,
                        outbound: label(result.expected_outbound),
                      })
                    : t(
                        result.expected_outbound === "(default)"
                          ? "overview.routingDiagnostics.trace.systemRule"
                          : "overview.routingDiagnostics.trace.unknownRule"
                      )}
                </p>
                {rule ? (
                  <p className="mt-1 text-xs">
                    {getRuleConditions(rule.rule)
                      .map(
                        (condition) =>
                          `${t(`overview.routingDiagnostics.conditions.${condition.key}`)}: ${condition.value}`
                      )
                      .join(" · ") ||
                      t("overview.routingDiagnostics.noConditions")}
                  </p>
                ) : null}
                {group.length > 1 ? (
                  <p className="mt-1 text-xs text-muted-foreground">
                    {t("overview.routingDiagnostics.trace.ipResults", {
                      count: group.length,
                      total: group.length,
                    })}
                  </p>
                ) : null}
              </div>
            )
          })}
        </div>
      ),
    },
    {
      title: t("overview.routingDiagnostics.trace.firewall"),
      tone: allOk
        ? "ok"
        : dnsMissing || entries.every((entry) => entry.issue === "criteria")
          ? "neutral"
          : "warn",
      content: (
        <div className="space-y-3">
          {!dnsMissing ? (
            <>
              <p>
                {t("overview.routingDiagnostics.trace.ipResults", {
                  count: entries.filter((entry) => entry.issue === "ok").length,
                  total: entries.length,
                })}
              </p>
              <ul
                className={cn(
                  "divide-y rounded-lg border px-3",
                  allOk
                    ? "border-success/25 bg-success/5"
                    : "border-warning/30 bg-warning/5"
                )}
              >
                {Array.from(firewallGroups.values()).map((group, index) => {
                  const { result, issue } = group[0]
                  return (
                    <li key={index} className="space-y-1.5 py-3">
                      <div className="flex flex-wrap items-center justify-between gap-2">
                        {group.length > 1 ? (
                          <span className="text-xs text-muted-foreground">
                            {t("overview.routingDiagnostics.trace.ipResults", {
                              count: group.length,
                              total: entries.length,
                            })}
                          </span>
                        ) : null}
                        <span
                          className={cn(
                            "inline-flex items-center gap-1 text-xs font-medium",
                            issue === "ok"
                              ? "text-success"
                              : "text-warning-foreground"
                          )}
                        >
                          {issue === "ok" ? (
                            <Check className="size-3.5" />
                          ) : (
                            <TriangleAlert className="size-3.5" />
                          )}
                          {t(
                            issue === "ok"
                              ? "overview.routingDiagnostics.trace.matches"
                              : result.actual_outbound === "(unknown)" ||
                                  result.expected_outbound === "(unknown)"
                                ? "overview.routingDiagnostics.unknownRoute"
                                : "overview.routingDiagnostics.trace.differs"
                          )}
                        </span>
                      </div>
                      <dl className="flex flex-wrap gap-x-4 gap-y-1 text-xs">
                        <div className="flex flex-wrap gap-x-1">
                          <dt>
                            {t("overview.routingDiagnostics.expectedOutbound")}:
                          </dt>
                          <dd className="font-medium text-foreground">
                            {label(result.expected_outbound)}
                          </dd>
                        </div>
                        <div className="flex flex-wrap gap-x-1">
                          <dt>
                            {t("overview.routingDiagnostics.actualOutbound")}:
                          </dt>
                          <dd className="font-medium text-foreground">
                            {label(result.actual_outbound)}
                          </dd>
                        </div>
                      </dl>
                      {issue === "firewall" &&
                      group.some(({ row }) => row?.in_ipset === false) ? (
                        <p className="text-xs">
                          {t(
                            "overview.routingDiagnostics.trace.expectedSetMissing"
                          )}
                        </p>
                      ) : null}
                      {issue !== "ok" && group.some(({ row }) => row) ? (
                        <div
                          className="space-y-1 text-xs"
                          data-testid="set-write-evidence"
                        >
                          <p className="font-medium text-foreground">
                            {t(
                              "overview.routingDiagnostics.trace.writeEvidence.title"
                            )}
                          </p>
                          {Array.from(
                            new Map(
                              group
                                .filter(({ row }) => row)
                                .map(({ row }) => [
                                  JSON.stringify([
                                    row?.set_write_evidence?.status ??
                                      "unavailable",
                                    row?.set_write_evidence?.age_seconds,
                                    row?.in_ipset,
                                  ]),
                                  row,
                                ])
                            ).values()
                          ).map((row, index) => (
                            <p key={index}>
                              {t(
                                `overview.routingDiagnostics.trace.writeEvidence.${row?.set_write_evidence?.status ?? "unavailable"}`
                              )}
                              {row?.set_write_evidence?.status === "recorded" &&
                              row.set_write_evidence.age_seconds != null
                                ? ` ${t("overview.routingDiagnostics.trace.writeEvidence.age", { age: row.set_write_evidence.age_seconds })}`
                                : ""}
                              {row?.set_write_evidence?.status === "recorded" &&
                              row.in_ipset === false
                                ? ` ${t("overview.routingDiagnostics.trace.writeEvidence.nowMissing")}`
                                : ""}
                            </p>
                          ))}
                        </div>
                      ) : null}
                    </li>
                  )
                })}
              </ul>
            </>
          ) : null}
          {Array.from(issueGroups.entries()).map(([issue, group]) => (
            <div key={issue} className="space-y-1">
              {issueGroups.size > 1 ? (
                <p className="font-medium text-foreground">
                  {t(`overview.routingDiagnostics.trace.issues.${issue}.title`)}
                </p>
              ) : null}
              {group.length > 1 ? (
                <p className="text-xs text-muted-foreground">
                  {t("overview.routingDiagnostics.trace.ipResults", {
                    count: group.length,
                    total: entries.length,
                  })}
                </p>
              ) : null}
              <p>
                {t(`overview.routingDiagnostics.trace.issues.${issue}.reason`)}
              </p>
              {issue !== "missing_ipset" && issue !== "other_ipset" ? (
                <p>
                  {t(
                    `overview.routingDiagnostics.trace.issues.${issue}.advice`
                  )}
                </p>
              ) : null}
            </div>
          ))}
          {entries.some((entry) => entry.issue !== "ok" && entry.row) ? (
            <p className="text-xs">
              {t("overview.routingDiagnostics.trace.writeEvidence.scope")}
            </p>
          ) : null}
          {allOk ? (
            <p className="text-xs">
              {t("overview.routingDiagnostics.trace.notConnectivityTest")}
            </p>
          ) : null}
        </div>
      ),
    },
  ]
  const routeTags = new Set(
    entries.flatMap((entry) => [
      entry.result.expected_outbound,
      entry.result.actual_outbound,
    ])
  )
  return (
    <section
      className="rounded-xl border bg-card p-4"
      aria-live="polite"
      data-testid="routing-trace"
    >
      <div className="flex items-center gap-3 border-b pb-4">
        <div
          className={cn(
            "flex size-10 shrink-0 items-center justify-center rounded-xl",
            allOk ? "bg-success/10 text-success" : "bg-warning/15 text-warning"
          )}
        >
          {allOk ? (
            <CircleCheck className="size-5" />
          ) : (
            <TriangleAlert className="size-5" />
          )}
        </div>
        <div className="min-w-0 flex-1">
          <p className="text-xs font-medium text-muted-foreground"></p>
          <h3 className="wrap-break-words text-lg leading-tight font-semibold">
            {heading}
          </h3>
          <p className="wrap-break-words mt-1 text-sm text-muted-foreground">
            {diagnostics.target}
          </p>
        </div>
        {onRefresh ? (
          <Tooltip>
            <TooltipTrigger
              render={
                <Button
                  aria-label={t("overview.routingDiagnostics.recheck")}
                  variant="ghost"
                  size="icon"
                  disabled={isRefreshing}
                  onClick={onRefresh}
                >
                  <RefreshCw
                    className={isRefreshing ? "animate-spin" : undefined}
                  />
                </Button>
              }
            />
            <TooltipContent>
              {t("overview.routingDiagnostics.recheck")}
            </TooltipContent>
          </Tooltip>
        ) : null}
      </div>
      <ol className="pt-4">
        {steps.map((step, index) => (
          <li
            key={step.title}
            data-step={index + 1}
            data-tone={step.tone}
            className="relative grid grid-cols-[28px_minmax(0,1fr)] gap-x-3 last:[&_.trace-content]:pb-0"
          >
            {index < steps.length - 1 ? (
              <span
                aria-hidden
                className="absolute top-7 bottom-0 left-[13px] w-0.5 bg-border"
              />
            ) : null}
            <span
              aria-hidden
              className={cn(
                "relative z-10 flex size-7 items-center justify-center rounded-full border-2 bg-card",
                step.tone === "ok"
                  ? "border-success/40 text-success"
                  : step.tone === "warn"
                    ? "border-warning/50 text-warning"
                    : "border-border text-muted-foreground"
              )}
            >
              {step.tone === "ok" ? (
                <Check className="size-3.5" />
              ) : step.tone === "warn" ? (
                <HelpCircle className="size-3.5" />
              ) : (
                <Minus className="size-3.5" />
              )}
            </span>
            <div className="trace-content min-w-0 pb-5">
              <div className="flex flex-wrap items-baseline gap-x-2">
                <h4 className="text-sm font-semibold">{step.title}</h4>
                <span className="text-xs text-muted-foreground">
                  {t("overview.routingDiagnostics.trace.step", {
                    step: index + 1,
                    total: steps.length,
                  })}
                </span>
              </div>
              <div className="wrap-break-words mt-1 text-sm text-muted-foreground">
                {step.content}
              </div>
            </div>
          </li>
        ))}
      </ol>
      <div className="mt-3 ml-10 space-y-2">
        {runtimeOutbounds
          .filter((state) => routeTags.has(state.tag))
          .map((state) => {
            const paths = state.interfaces
              .filter((item) => item.status === "active")
              .map((item) =>
                item.interface_name
                  ? `${item.outbound_tag} → ${item.interface_name}`
                  : item.outbound_tag
              )
              .join(", ")
            return (
              <div key={state.tag} className="text-xs text-muted-foreground">
                {paths ? (
                  <p className="wrap-break-words">
                    {t("overview.routingDiagnostics.trace.pathFor", {
                      outbound: state.tag,
                    })}
                    : {paths}
                  </p>
                ) : null}
                {state.status === "unavailable" ? (
                  <p className="text-sm text-warning-foreground">
                    {t(
                      "overview.routingDiagnostics.trace.outboundUnavailable",
                      { outbound: state.tag }
                    )}
                  </p>
                ) : null}
              </div>
            )
          })}
      </div>
    </section>
  )
}
