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
import { Link } from "wouter"
import type {
  RoutingTestOutboundGroup,
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
  focusRoutingTestField,
  type RoutingTestField,
} from "./routing-test-focus"
import {
  getExpectedRoutingRule,
  getRuleConditions,
} from "./routing-diagnostics-utils"

type StepTone = "ok" | "warn" | "neutral"

const isIpv6 = (ip: string) => ip.includes(":")

/** "N of M addresses" with the group's IPs in a tooltip; a lone IP is shown as is. */
function IpGroupLabel({ ips, total }: { ips: string[]; total: number }) {
  const { t } = useTranslation()
  if (ips.length === 1) {
    return <code className="wrap-break-words font-mono text-xs">{ips[0]}</code>
  }
  const text = t("overview.routingDiagnostics.trace.ipGroup", {
    selected: ips.length,
    count: total,
  })
  return (
    <Tooltip>
      <TooltipTrigger
        render={
          <button
            type="button"
            aria-label={`${text}: ${ips.join(", ")}`}
            data-testid="ip-group"
            className="cursor-help underline decoration-dashed underline-offset-2"
          />
        }
      >
        {text}
      </TooltipTrigger>
      <TooltipContent>
        <ul className="font-mono text-xs">
          {ips.map((ip) => (
            <li key={ip}>{ip}</li>
          ))}
        </ul>
      </TooltipContent>
    </Tooltip>
  )
}

// Counter of matching routes; the tooltip marks each checked IP.
function IpResultsLabel({
  entries,
  children,
}: {
  entries: { result: { ip: string }; issue: string }[]
  children?: ReactNode
}) {
  return (
    <Tooltip>
      <TooltipTrigger
        render={
          <button
            type="button"
            aria-label={entries
              .map(
                ({ result, issue }) =>
                  `${result.ip} ${issue === "ok" ? "✓" : "⚠"}`
              )
              .join(", ")}
            data-testid="ip-results"
            className="cursor-help underline decoration-dashed underline-offset-2"
          />
        }
      >
        {children}
      </TooltipTrigger>
      <TooltipContent>
        <ul className="space-y-0.5 font-mono text-xs">
          {entries.map(({ result, issue }) => (
            <li key={result.ip} className="flex items-center gap-1.5">
              {issue === "ok" ? (
                <Check className="size-3.5" />
              ) : (
                <TriangleAlert className="size-3.5" />
              )}
              {result.ip}
            </li>
          ))}
        </ul>
      </TooltipContent>
    </Tooltip>
  )
}

/** Members of a urltest/icmptest outbound as the backend reported them. */
function OutboundGroupBreakdown({ group }: { group: RoutingTestOutboundGroup }) {
  const { t } = useTranslation()
  const balance = group.strategy === "balance"
  const selected = group.members.find((member) => member.in_use)
  return (
    <div className="space-y-1 text-xs" data-testid="outbound-group">
      <p>
        {balance
          ? t("overview.routingDiagnostics.trace.group.balance", {
              tag: group.tag,
            })
          : selected
            ? t("overview.routingDiagnostics.trace.group.priority", {
                tag: group.tag,
                member: selected.tag,
              })
            : t("overview.routingDiagnostics.trace.group.prioritySkipped", {
                tag: group.tag,
              })}
      </p>
      <ul className="space-y-0.5 pl-4">
        {group.members.map((member) => (
          <li key={member.tag} className="flex flex-wrap gap-x-2">
            <span className="font-medium text-foreground">{member.tag}</span>
            {member.interface ? (
              <code className="font-mono">{member.interface}</code>
            ) : null}
            {balance ? (
              <span>
                {t("overview.routingDiagnostics.trace.group.weight", {
                  weight: member.weight ?? 1,
                })}
                {" · "}
                {member.in_use
                  ? `${member.share_percent ?? 0}%`
                  : t("overview.routingDiagnostics.trace.group.notInUse")}
              </span>
            ) : !member.in_use ? (
              <span>{t("overview.routingDiagnostics.trace.group.standby")}</span>
            ) : null}
          </li>
        ))}
      </ul>
      {balance && !selected ? (
        <p>{t("overview.routingDiagnostics.trace.group.balanceNone")}</p>
      ) : null}
    </div>
  )
}

const FIELD_LABEL_KEYS: Record<RoutingTestField, string> = {
  src_addr: "overview.routingDiagnostics.conditions.sourceIp",
  src_port: "pages.routingRuleUpsert.fields.sourcePort",
  proto: "overview.routingTest.protocol",
  dest_port: "overview.routingTest.port",
}

/** Request field names as links that reveal and focus the field in the form. */
function MissingFieldLinks({ fields }: { fields: RoutingTestField[] }) {
  const { t, i18n } = useTranslation()
  const names = fields.map((field) =>
    t("overview.routingDiagnostics.trace.undetermined.fieldName", {
      name: t(FIELD_LABEL_KEYS[field]),
    })
  )
  const parts = new Intl.ListFormat(i18n.language, {
    style: "long",
    type: "conjunction",
  }).formatToParts(names)
  return (
    <>
      {parts.map((part, index) => {
        if (part.type === "literal") return part.value
        const field = fields[names.indexOf(part.value)]
        return (
          <button
            key={index}
            type="button"
            data-testid="missing-field-link"
            className="font-medium text-foreground underline underline-offset-2"
            onClick={() => focusRoutingTestField(field)}
          >
            {part.value}
          </button>
        )
      })}
    </>
  )
}

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
  // Explains why the rule choice stopped at a rule that could not be
  // evaluated; null for older servers that do not report the rule.
  const undeterminedDetails = (
    result: RoutingTestResponse["results"][number]
  ) => {
    if (result.undetermined_rule_index === undefined) return null
    const rule = diagnostics.rule_diagnostics.find(
      (item) => item.rule_index === result.undetermined_rule_index
    )
    const params = {
      rule: result.undetermined_rule_index + 1,
      outbound: label(rule?.outbound ?? "(unknown)"),
    }
    const fields = (result.missing_criteria ?? []) as RoutingTestField[]
    if (
      result.undetermined_reason === "missing_criteria" &&
      fields.length > 0
    ) {
      const components = { fields: <MissingFieldLinks fields={fields} /> }
      return {
        key: JSON.stringify([params.rule, fields]),
        explainFor: null,
        fill: (
          <Trans
            i18nKey="overview.routingDiagnostics.trace.undetermined.fillFields"
            count={fields.length}
            components={components}
          />
        ),
        explain: (
          <Trans
            i18nKey="overview.routingDiagnostics.trace.undetermined.ruleChecks"
            count={fields.length}
            values={params}
            components={components}
          />
        ),
      }
    }
    if (result.undetermined_reason === "default_gateway_directness") {
      return {
        key: JSON.stringify([params.rule, "gateway"]),
        fill: null,
        explain: null,
        explainFor: (count: number, ip: string) =>
          t("overview.routingDiagnostics.trace.undetermined.gateway", {
            ...params,
            count,
            ip,
          }),
      }
    }
    return null
  }
  // How the default_gateway rule treated the address, as one sentence.
  const gatewaySkipText = (result: RoutingTestResponse["results"][number]) => {
    const gateway = result.default_gateway
    if (!gateway || gateway.uses_default_gateway !== false || !gateway.bypass) {
      return null
    }
    const { bypass } = gateway
    const key =
      bypass.source === "route"
        ? bypass.interface
          ? "skippedRoute"
          : "skippedRouteNoInterface"
        : bypass.source === "address"
          ? "skippedAddress"
          : "skippedReserved"
    return t(`overview.routingDiagnostics.trace.gateway.${key}`, {
      rule: gateway.rule_index + 1,
      ip: result.ip,
      cidr: bypass.cidr,
      interface: bypass.interface ?? "",
    })
  }
  const dnsMissing =
    diagnostics.is_domain && diagnostics.resolved_ips.length === 0
  const ipv6Off = diagnostics.ipv6_enabled === false
  const skippedIpv6 = ipv6Off
    ? (diagnostics.is_domain
        ? diagnostics.resolved_ips
        : [diagnostics.target]
      ).filter(isIpv6)
    : []
  const entries = diagnostics.results
    .filter((result) => !(ipv6Off && isIpv6(result.ip)))
    .map((result) => {
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
      // The daemon decides the verdict; the UI only words it.
      const issue = result.diagnosis
      return {
        result,
        rule,
        listMatch,
        issue,
        undetermined: undeterminedDetails(result),
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
    // The firewall verdict says nothing about which rule was chosen, so it
    // stays out of the key: IPv4 and IPv6 hitting one rule share a block.
    const ruleKey = JSON.stringify([
      entry.rule?.rule_index,
      entry.result.expected_outbound,
      entry.undetermined?.key,
      entry.result.default_gateway?.uses_default_gateway,
      entry.result.default_gateway?.bypass,
      // A skipped rule names the address in its sentence.
      entry.result.default_gateway?.uses_default_gateway === false
        ? entry.result.ip
        : null,
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
  // Both leave the rule undecided: missing request fields, or unreadable routes.
  const ruleUndecided = (entry: (typeof entries)[number]) =>
    entry.issue === "criteria" || entry.issue === "network_state"
  const ruleUnknown = entries.some(ruleUndecided)
  const nothingChecked = ipv6Off && entries.length === 0 && !dnsMissing
  const heading = nothingChecked
    ? t("overview.routingDiagnostics.trace.ipv6Disabled.title")
    : allOk
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
        <code
          key={ip}
          className={cn(
            "wrap-break-words font-mono text-xs",
            ipv6Off && isIpv6(ip) && "opacity-60"
          )}
        >
          {ip}
        </code>
      ))}
    </span>
  )
  const ipv6Note =
    skippedIpv6.length > 0 ? (
      <p
        className="text-xs text-muted-foreground"
        data-testid="ipv6-disabled-note"
      >
        <Trans
          i18nKey="overview.routingDiagnostics.trace.ipv6Disabled.note"
          components={{
            settings: (
              <Link
                className="underline underline-offset-2"
                href="/general#ipv6-enabled"
              />
            ),
          }}
        />
      </p>
    ) : null
  const firewallGroups = new Map<string, typeof entries>()
  for (const entry of entries) {
    const key = JSON.stringify([
      entry.result.expected_outbound,
      entry.result.actual_outbound,
      entry.issue,
      entry.result.outbound_group,
      entry.result.default_gateway?.applied_uses_default_gateway,
    ])
    firewallGroups.set(key, [...(firewallGroups.get(key) ?? []), entry])
  }
  const steps: { title: string; tone: StepTone; content: ReactNode }[] = [
    {
      title: t("overview.routingDiagnostics.trace.dns"),
      tone: dnsMissing ? "warn" : diagnostics.is_domain ? "ok" : "neutral",
      content: dnsMissing ? (
        <div className="space-y-1">
          <p>{t("overview.routingDiagnostics.trace.dnsEmpty")}</p>
          <p className="text-xs text-muted-foreground">
            {t("overview.routingDiagnostics.trace.dnsClientNote")}
          </p>
        </div>
      ) : diagnostics.is_domain ? (
        <div className="space-y-1">
          {codes(diagnostics.resolved_ips)}
          {ipv6Note}
          {diagnostics.dns?.resolver === "dnsmasq" ? (
            <p className="text-xs text-muted-foreground">
              {t("overview.routingDiagnostics.trace.dnsResolverDnsmasq", {
                endpoint: diagnostics.dns.endpoint ?? "",
                instance: diagnostics.dns.instance ?? "",
              })}
            </p>
          ) : diagnostics.dns?.resolver === "system" ? (
            <p className="text-xs text-muted-foreground">
              {t("overview.routingDiagnostics.trace.dnsResolverSystem")}
            </p>
          ) : null}
          <p className="text-xs text-muted-foreground">
            {t("overview.routingDiagnostics.trace.dnsClientNote")}
          </p>
        </div>
      ) : (
        <div className="space-y-1">
          <p className={cn(skippedIpv6.length > 0 && "opacity-60")}>
            {t("overview.routingDiagnostics.trace.literalIp", {
              ip: diagnostics.target,
            })}
          </p>
          {ipv6Note}
        </div>
      ),
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
            const { rule, result, undetermined } = group[0]
            return (
              <div key={index}>
                {/* Group label on top, like the firewall step's groups. */}
                {group.length < entries.length ? (
                  <p className="mb-1 text-xs text-muted-foreground">
                    <IpGroupLabel
                      ips={group.map(({ result }) => result.ip)}
                      total={entries.length}
                    />
                  </p>
                ) : null}
                {!undetermined && gatewaySkipText(result) ? (
                  <p className="mb-1" data-testid="gateway-skipped">
                    {gatewaySkipText(result)}
                  </p>
                ) : null}
                {undetermined ? (
                  <div className="space-y-1">
                    {undetermined.explainFor ? (
                      <p>{undetermined.explainFor(group.length, result.ip)}</p>
                    ) : (
                      <>
                        <p>{undetermined.fill}</p>
                        <p className="text-xs">{undetermined.explain}</p>
                      </>
                    )}
                  </div>
                ) : rule &&
                  result.default_gateway?.uses_default_gateway === true &&
                  result.default_gateway.rule_index === rule.rule_index ? (
                  <p data-testid="gateway-uses">
                    {t("overview.routingDiagnostics.trace.gateway.uses", {
                      rule: rule.rule_index + 1,
                      outbound: label(result.expected_outbound),
                      ip: result.ip,
                      count: group.length,
                    })}
                  </p>
                ) : (
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
                )}
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
        : dnsMissing || entries.every(ruleUndecided)
          ? "neutral"
          : "warn",
      content: (
        <div className="space-y-3">
          {!dnsMissing ? (
            <>
              <p>
                <Trans
                  i18nKey="overview.routingDiagnostics.trace.ipResults"
                  values={{
                    count: entries.filter((entry) => entry.issue === "ok")
                      .length,
                    total: entries.length,
                  }}
                  components={{
                    ips: <IpResultsLabel entries={entries} />,
                  }}
                />
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
                        {group.length < entries.length ? (
                          <span className="text-xs text-muted-foreground">
                            <IpGroupLabel
                              ips={group.map(({ result }) => result.ip)}
                              total={entries.length}
                            />
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
                      {result.outbound_group ? (
                        <OutboundGroupBreakdown group={result.outbound_group} />
                      ) : null}
                      {result.default_gateway?.uses_default_gateway != null &&
                      result.default_gateway.applied_uses_default_gateway !=
                        null &&
                      result.default_gateway.uses_default_gateway !==
                        result.default_gateway.applied_uses_default_gateway ? (
                        <p className="text-xs" data-testid="gateway-applied">
                          {t(
                            result.default_gateway.applied_uses_default_gateway
                              ? "overview.routingDiagnostics.trace.gateway.appliedStill"
                              : "overview.routingDiagnostics.trace.gateway.appliedNot"
                          )}
                        </p>
                      ) : null}
                      {issue !== "ok" && group.some(({ row }) => row) ? (
                        <ul
                          className="list-disc space-y-0.5 pl-4 text-xs"
                          data-testid="set-write-evidence"
                        >
                          {group.some(({ row }) => row?.in_ipset === false) ? (
                            <li>
                              {t(
                                "overview.routingDiagnostics.trace.expectedSetMissing"
                              )}
                            </li>
                          ) : null}
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
                            <li key={index}>
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
                            </li>
                          ))}
                        </ul>
                      ) : null}
                    </li>
                  )
                })}
              </ul>
            </>
          ) : null}
          {Array.from(issueGroups.entries()).map(([issue, group]) => {
            // DNS advice only fits addresses learned through a domain, not
            // entries that a static IP list put into the set.
            const learnedFromDomain =
              issue === "missing_ipset" &&
              diagnostics.is_domain &&
              group.some(
                (entry) =>
                  entry.listMatch && entry.listMatch.via !== entry.result.ip
              )
            // The rule step already explains this once.
            if (issue === "network_state" && issueGroups.size === 1) {
              return null
            }
            return (
              <div key={issue} className="space-y-1">
                {issueGroups.size > 1 ? (
                  <p className="font-medium text-foreground">
                    {t(
                      `overview.routingDiagnostics.trace.issues.${issue}.title`
                    )}
                  </p>
                ) : null}
                {group.length < entries.length ? (
                  <p className="text-xs text-muted-foreground">
                    <IpGroupLabel
                      ips={group.map(({ result }) => result.ip)}
                      total={entries.length}
                    />
                  </p>
                ) : null}
                {issue === "network_state" ? null : issue === "criteria" &&
                  group[0].undetermined ? (
                  <div className="space-y-1">
                    <p>{group[0].undetermined.explain}</p>
                    <p>{group[0].undetermined.fill}</p>
                  </div>
                ) : (
                  <>
                    <p>
                      {t(
                        learnedFromDomain
                          ? "overview.routingDiagnostics.trace.issues.missing_ipset.reasonDomain"
                          : `overview.routingDiagnostics.trace.issues.${issue}.reason`
                      )}
                    </p>
                    {learnedFromDomain ? (
                      <ol className="list-decimal space-y-1 pl-5">
                        <li>
                          {t(
                            "overview.routingDiagnostics.trace.issues.missing_ipset.steps.open"
                          )}
                        </li>
                        <li>
                          {t(
                            "overview.routingDiagnostics.trace.issues.missing_ipset.steps.flush"
                          )}
                        </li>
                        <li>
                          <Trans
                            i18nKey="overview.routingDiagnostics.trace.issues.missing_ipset.steps.dns"
                            components={{
                              log: (
                                <Link
                                  className="font-medium text-foreground underline underline-offset-2"
                                  href="/requests-log"
                                />
                              ),
                            }}
                          />
                        </li>
                      </ol>
                    ) : issue !== "other_ipset" ? (
                      <p>
                        {t(
                          `overview.routingDiagnostics.trace.issues.${issue}.advice`
                        )}
                      </p>
                    ) : null}
                  </>
                )}
              </div>
            )
          })}
          {allOk ? (
            <p className="text-xs">
              {t("overview.routingDiagnostics.trace.notConnectivityTest")}
            </p>
          ) : null}
        </div>
      ),
    },
  ]
  // Only IPv6 addresses and IPv6 is off: later steps have nothing to show.
  const shownSteps = nothingChecked
    ? steps.map((step, index) =>
        index === 0
          ? step
          : {
              ...step,
              tone: "neutral" as StepTone,
              content: t("overview.routingDiagnostics.trace.notChecked"),
            }
      )
    : steps
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
        {shownSteps.map((step, index) => (
          <li
            key={step.title}
            data-step={index + 1}
            data-tone={step.tone}
            className="relative grid grid-cols-[28px_minmax(0,1fr)] gap-x-3 last:[&_.trace-content]:pb-0"
          >
            {index < shownSteps.length - 1 ? (
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
              <h4 className="text-sm font-semibold">{step.title}</h4>
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
            const active = state.interfaces.filter(
              (item) => item.status === "active"
            )
            // A plain interface outbound names its interface; a group names
            // the members it currently uses.
            const direct = active.every(
              (item) => item.outbound_tag === state.tag
            )
            const interfaces = active
              .map((item) => item.interface_name)
              .filter(Boolean)
              .join(", ")
            const members = active
              .map((item) =>
                item.interface_name
                  ? t("overview.routingDiagnostics.trace.pathMember", {
                      outbound: item.outbound_tag,
                      interface: item.interface_name,
                    })
                  : item.outbound_tag
              )
              .join(", ")
            return (
              <div key={state.tag} className="text-xs text-muted-foreground">
                {active.length > 0 && (!direct || interfaces) ? (
                  <p className="wrap-break-words">
                    {direct
                      ? t("overview.routingDiagnostics.trace.pathInterface", {
                          outbound: state.tag,
                          interfaces,
                        })
                      : t("overview.routingDiagnostics.trace.pathGroup", {
                          outbound: state.tag,
                          members,
                        })}
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
