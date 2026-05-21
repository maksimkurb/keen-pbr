import type { ApiError } from "@/api/client"
import type { RouteRule } from "@/api/generated/model/routeRule"
import { getApiErrorMessage as getSharedApiErrorMessage } from "@/lib/api-errors"

export type RouteRuleDraft = {
  enabled: boolean
  list: string[]
  outbound: string
  proto: string
  src_port: string
  dest_port: string
  src_addr: string
  dest_addr: string
}

export const protoOptions = ["", "tcp", "udp", "tcp/udp"] as const

export const emptyRouteRuleDraft: RouteRuleDraft = {
  enabled: true,
  list: [],
  outbound: "",
  proto: "",
  src_port: "",
  dest_port: "",
  src_addr: "",
  dest_addr: "",
}

export type RuleDetailPiece = {
  /** Stable key matching the field name, used for React keys and i18n lookup. */
  key: "lists" | "proto" | "src_addr" | "dest_addr" | "src_port" | "dest_port"
  value: string
}

/**
 * Returns the non-empty match criteria for a rule as structured pieces so
 * callers can render them as chips, labelled lines, etc.
 * Only fields with a real value are included; omit-empty fields stay absent.
 */
export function getRuleDetailPieces(rule: RouteRule): RuleDetailPiece[] {
  const lists = rule.list?.filter(Boolean).join(", ") ?? ""
  const candidates: Array<RuleDetailPiece | undefined> = [
    lists.trim().length > 0
      ? { key: "lists", value: lists }
      : undefined,
    rule.proto && rule.proto.trim().length > 0
      ? { key: "proto", value: rule.proto }
      : undefined,
    rule.src_addr && rule.src_addr.trim().length > 0
      ? { key: "src_addr", value: rule.src_addr }
      : undefined,
    rule.dest_addr && rule.dest_addr.trim().length > 0
      ? { key: "dest_addr", value: rule.dest_addr }
      : undefined,
    rule.src_port && rule.src_port.trim().length > 0
      ? { key: "src_port", value: rule.src_port }
      : undefined,
    rule.dest_port && rule.dest_port.trim().length > 0
      ? { key: "dest_port", value: rule.dest_port }
      : undefined,
  ]
  return candidates.filter((piece): piece is RuleDetailPiece => piece !== undefined)
}

export function getRuleDetails(rule: RouteRule) {
  return getRuleDetailPieces(rule)
    .map((piece) => `${piece.key}: ${piece.value}`)
    .join(" · ")
}

export function toRouteRuleDraft(rule: RouteRule): RouteRuleDraft {
  return {
    enabled: rule.enabled ?? true,
    list: rule.list ?? [],
    outbound: rule.outbound,
    proto: rule.proto ?? "",
    src_port: rule.src_port ?? "",
    dest_port: rule.dest_port ?? "",
    src_addr: rule.src_addr ?? "",
    dest_addr: rule.dest_addr ?? "",
  }
}

export function normalizeRouteRuleDraft(draft: RouteRuleDraft): RouteRule {
  return {
    enabled: draft.enabled,
    list: draft.list,
    outbound: draft.outbound,
    proto: trimToUndefined(draft.proto),
    src_port: trimToUndefined(draft.src_port),
    dest_port: trimToUndefined(draft.dest_port),
    src_addr: trimToUndefined(draft.src_addr),
    dest_addr: trimToUndefined(draft.dest_addr),
  }
}

export function reorderRules(
  rules: RouteRule[],
  fromIndex: number,
  toIndex: number
) {
  const nextRules = [...rules]
  const [movedRule] = nextRules.splice(fromIndex, 1)
  nextRules.splice(toIndex, 0, movedRule)
  return nextRules
}

export function setRouteRuleEnabled(
  rules: RouteRule[],
  index: number,
  enabled: boolean
) {
  return rules.map((rule, ruleIndex) =>
    ruleIndex === index ? { ...rule, enabled } : rule
  )
}

export type RouteListUsageRef = {
  ruleIndex: number
  outbound: string
}

/** Builds a map of list name → other routing rules referencing that list. */
export function buildListUsageByRouteRules(
  rules: RouteRule[],
  excludeRuleIndex?: number
): Map<string, RouteListUsageRef[]> {
  const map = new Map<string, RouteListUsageRef[]>()
  rules.forEach((rule, index) => {
    if (
      excludeRuleIndex !== undefined &&
      index === excludeRuleIndex
    ) {
      return
    }

    const outbound = rule.outbound
    for (const listName of rule.list ?? []) {
      if (!listName) {
        continue
      }
      const prev = map.get(listName) ?? []
      prev.push({ ruleIndex: index, outbound })
      map.set(listName, prev)
    }
  })
  return map
}

export function describeRouteRuleRefForListUsage(
  ref: RouteListUsageRef,
): string {
  return `#${ref.ruleIndex + 1} → ${ref.outbound}`
}

/** Single-line subtitle for list pickers: other routing rules referencing the same list. */
export function formatRoutingListRefsUsageSummary(
  refs: RouteListUsageRef[],
): string {
  return refs.map((reference) => describeRouteRuleRefForListUsage(reference)).join(", ")
}

export function getFirstFieldError(errors: unknown[]) {
  const firstError = errors[0]
  return typeof firstError === "string" ? firstError : undefined
}

export function getApiErrorMessage(error: ApiError) {
  return getSharedApiErrorMessage(error)
}

function trimToUndefined(value: string) {
  const trimmed = value.trim()
  return trimmed.length > 0 ? trimmed : undefined
}
