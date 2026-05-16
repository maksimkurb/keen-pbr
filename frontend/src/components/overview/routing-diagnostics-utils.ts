import type {
  RouteRule,
  RoutingTestRuleDiagnostic,
} from "@/api/generated/model"

export type RuleCondition = {
  key:
    | "lists"
    | "proto"
    | "sourceIp"
    | "destinationIp"
    | "sourcePort"
    | "destinationPort"
  value: string
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

  return rule.ip_rows.every((ipRow) => ipRow.in_ipset !== true)
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

  return conditions
}

function hasText(value: string | undefined): value is string {
  return value != null && typeof value === "string" && value.trim().length > 0
}
