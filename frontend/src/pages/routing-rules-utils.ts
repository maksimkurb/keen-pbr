import type { ApiError } from "@/api/client"
import type { RouteRule } from "@/api/generated/model/routeRule"
import { getApiErrorMessage as getSharedApiErrorMessage } from "@/lib/api-errors"

export type RouteRuleDraft = {
  /** Mirrors the API `default_gateway`; "normal" means the field is unset. */
  default_gateway: RouteRuleMode
  enabled: boolean
  list: string[]
  outbound: string
  proto: string
  dscp: string
  src_port: string
  dest_port: string
  src_addr: string
  dest_addr: string
}

export type RouteRuleMode = "normal" | "ipv4" | "ipv6"

export const protoOptions = ["", "tcp", "udp", "tcp/udp"] as const

export const emptyRouteRuleDraft: RouteRuleDraft = {
  default_gateway: "normal",
  enabled: true,
  list: [],
  outbound: "",
  proto: "",
  dscp: "",
  src_port: "",
  dest_port: "",
  src_addr: "",
  dest_addr: "",
}

export function getRuleDetails(rule: RouteRule) {
  const pieces = [
    `src_addr: ${rule.src_addr || "-"}`,
    `dest_addr: ${rule.dest_addr || "-"}`,
    `dscp: ${rule.dscp ?? "-"}`,
    `src_port: ${rule.src_port || "-"}`,
    `dest_port: ${rule.dest_port || "-"}`,
  ]

  return pieces.join(" · ")
}

export function toRouteRuleDraft(rule: RouteRule): RouteRuleDraft {
  return {
    default_gateway: rule.default_gateway ?? "normal",
    enabled: rule.enabled ?? true,
    list: rule.list ?? [],
    outbound: rule.outbound,
    proto: rule.proto ?? "",
    dscp: rule.dscp?.toString() ?? "",
    src_port: rule.src_port ?? "",
    dest_port: rule.dest_port ?? "",
    src_addr: rule.src_addr ?? "",
    dest_addr: rule.dest_addr ?? "",
  }
}

export function normalizeRouteRuleDraft(draft: RouteRuleDraft): RouteRule {
  if (draft.default_gateway !== "normal") {
    return {
      enabled: draft.enabled,
      outbound: draft.outbound,
      default_gateway: draft.default_gateway,
    }
  }

  return {
    enabled: draft.enabled,
    list: draft.list,
    outbound: draft.outbound,
    proto: trimToUndefined(draft.proto),
    dscp: parseOptionalDscp(draft.dscp),
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

export function getReorderTargetIndex(
  fromIndex: number,
  insertionIndex: number
) {
  return insertionIndex > fromIndex ? insertionIndex - 1 : insertionIndex
}

export function getDragInsertionIndex(
  fromIndex: number,
  hoveredRowIndex: number
) {
  if (hoveredRowIndex === fromIndex) {
    return null
  }

  return hoveredRowIndex < fromIndex ? hoveredRowIndex : hoveredRowIndex + 1
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

export function getApiErrorMessage(error: ApiError) {
  return getSharedApiErrorMessage(error)
}

function trimToUndefined(value: string) {
  const trimmed = value.trim()
  return trimmed.length > 0 ? trimmed : undefined
}

function parseOptionalDscp(value: string) {
  const trimmed = value.trim()
  if (trimmed.length === 0) {
    return undefined
  }
  return Number(trimmed)
}
