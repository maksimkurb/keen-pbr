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

export function getRuleDetails(rule: RouteRule) {
  const pieces = [
    `src_addr: ${rule.src_addr || "-"}`,
    `dest_addr: ${rule.dest_addr || "-"}`,
    `src_port: ${rule.src_port || "-"}`,
    `dest_port: ${rule.dest_port || "-"}`,
  ]

  return pieces.join(" · ")
}

export function toRouteRuleDraft(rule: RouteRule): RouteRuleDraft {
  return {
    enabled: rule.enabled ?? true,
    list: rule.list,
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
