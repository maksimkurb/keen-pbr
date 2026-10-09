import type {
  RouteRule,
  RoutingTestRuleDiagnostic,
  RoutingTestEntry,
  RoutingTestResponse,
} from "@/api/generated/model"

export type RuleCondition = {
  key:
    | "lists"
    | "proto"
    | "sourceIp"
    | "destinationIp"
    | "sourcePort"
    | "destinationPort"
    | "dscp"
  value: string
}

export function getRoutingDiagnosticIssue(
  diagnostics: RoutingTestResponse,
  result: RoutingTestEntry
) {
  if (diagnostics.is_domain && diagnostics.resolved_ips.length === 0)
    return "dns"
  if (result.expected_outbound === "(unknown)") return "criteria"
  const rule = getExpectedRoutingRule(diagnostics.rule_diagnostics, result)
  const row = rule?.ip_rows.find((row) => row.ip === result.ip)
  const otherSetRules = diagnostics.rule_diagnostics.filter(
    (item) =>
      item.rule.enabled !== false &&
      item.rule.list?.length &&
      item.rule_index !== rule?.rule_index &&
      item.outbound === result.actual_outbound &&
      item.ip_rows.some(
        (row) =>
          row.ip === result.ip &&
          row.in_ipset === true &&
          row.criteria_match !== false
      )
  )
  if (result.actual_outbound === "(unknown)") return "firewall"
  if (result.ok) return "ok"
  if (row?.in_lists && row.in_ipset === false) {
    return otherSetRules.length ? "other_ipset" : "missing_ipset"
  }
  if (row?.in_ipset === true && otherSetRules.length)
    return "conflicting_ipsets"
  if (
    result.expected_outbound === "(default)" &&
    otherSetRules.some((rule) =>
      rule.ip_rows.some((row) => row.ip === result.ip && !row.in_lists)
    )
  )
    return "stale_ipset"
  return "mismatch"
}

export function getExpectedRoutingRule(
  rules: RoutingTestRuleDiagnostic[],
  result: RoutingTestEntry
) {
  if (result.matched_rule_index != null) {
    return rules.find((rule) => rule.rule_index === result.matched_rule_index)
  }
  const rule = rules
    .filter((rule) => rule.rule.enabled !== false)
    .sort((a, b) => a.rule_index - b.rule_index)
    .find((rule) =>
      rule.ip_rows.some(
        (row) =>
          row.ip === result.ip && row.in_lists && row.criteria_match !== false
      )
    )
  return rule?.outbound === result.expected_outbound ? rule : undefined
}

export function getVisibleRuleDiagnostics(
  ruleDiagnostics: RoutingTestRuleDiagnostic[],
  showAllRules: boolean
) {
  if (showAllRules) {
    return ruleDiagnostics
  }

  return ruleDiagnostics.filter((rule) => !isGrayRuleDiagnostic(rule))
}

export function isGrayRuleDiagnostic(rule: RoutingTestRuleDiagnostic) {
  if (rule.target_in_lists || rule.target_match) {
    return false
  }

  return rule.ip_rows.every(
    (ipRow) => !ipRow.in_lists && ipRow.in_ipset !== true
  )
}

export function getRuleConditions(rule: RouteRule): RuleCondition[] {
  const conditions: RuleCondition[] = []

  if (rule.list && rule.list.length > 0) {
    conditions.push({ key: "lists", value: rule.list.join(", ") })
  }
  if (hasText(rule.proto)) {
    conditions.push({ key: "proto", value: rule.proto })
  }
  if (hasText(rule.src_addr)) {
    conditions.push({ key: "sourceIp", value: rule.src_addr })
  }
  if (hasText(rule.dest_addr)) {
    conditions.push({ key: "destinationIp", value: rule.dest_addr })
  }
  if (hasText(rule.src_port)) {
    conditions.push({ key: "sourcePort", value: rule.src_port })
  }
  if (hasText(rule.dest_port)) {
    conditions.push({ key: "destinationPort", value: rule.dest_port })
  }
  if (rule.dscp != null) {
    conditions.push({ key: "dscp", value: String(rule.dscp) })
  }

  return conditions
}

function hasText(value: string | undefined): value is string {
  return value != null && typeof value === "string" && value.trim().length > 0
}
