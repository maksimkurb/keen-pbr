import type { ApiError } from "@/api/client"
import type { RouteRule } from "@/api/generated/model/routeRule"

export type RouteRuleDraft = {
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

export function getFirstFieldError(errors: unknown[]) {
  const firstError = errors[0]
  return typeof firstError === "string" ? firstError : undefined
}

export function getApiErrorMessage(error: ApiError) {
  const details = error.details
    ? ` Details: ${JSON.stringify(error.details)}`
    : ""
  return `${error.message}.${details}`
}

export function getPortSpecError(value: string) {
  const normalized = value.trim()
  if (!normalized) {
    return null
  }

  const content = normalized.startsWith("!") ? normalized.slice(1) : normalized
  if (!content || content.startsWith(",") || content.endsWith(",")) {
    return "Use comma-separated ports or ranges."
  }

  const tokens = content.split(",")
  for (const token of tokens) {
    const part = token.trim()
    if (!part) {
      return "Use comma-separated ports or ranges."
    }

    if (part.includes("-")) {
      const [start, end, extra] = part.split("-")
      if (extra !== undefined || !isValidPort(start) || !isValidPort(end)) {
        return "Port ranges must use valid ports such as 8000-9000."
      }

      if (Number(start) > Number(end)) {
        return "Port range start must be less than or equal to end."
      }

      continue
    }

    if (!isValidPort(part)) {
      return "Ports must be integers between 1 and 65535."
    }
  }

  return null
}

export function getAddressSpecError(value: string) {
  const normalized = value.trim()
  if (!normalized) {
    return null
  }

  const content = normalized.startsWith("!") ? normalized.slice(1) : normalized
  if (!content || content.startsWith(",") || content.endsWith(",")) {
    return "Use comma-separated IP addresses or CIDRs."
  }

  const tokens = content.split(",")
  for (const token of tokens) {
    const address = token.trim()
    if (!address || !isValidAddressSpec(address)) {
      return "Addresses must be valid IPv4 or IPv6 hosts or CIDR ranges, for example 10.0.0.1, 10.0.0.0/8, or 2001:db8::/32."
    }
  }

  return null
}

function trimToUndefined(value: string) {
  const trimmed = value.trim()
  return trimmed.length > 0 ? trimmed : undefined
}

function isValidPort(value: string) {
  if (!/^\d+$/.test(value)) {
    return false
  }

  const port = Number(value)
  return port >= 1 && port <= 65535
}

function isValidAddressSpec(value: string) {
  if (!value.includes("/")) {
    return isValidIpv4(value) || isValidIpv6(value)
  }

  const parts = value.split("/")
  if (parts.length !== 2) {
    return false
  }

  const [ip, prefix] = parts
  if (!/^\d+$/.test(prefix)) {
    return false
  }

  if (isValidIpv4(ip)) {
    const prefixNumber = Number(prefix)
    return prefixNumber >= 0 && prefixNumber <= 32
  }

  if (isValidIpv6(ip)) {
    const prefixNumber = Number(prefix)
    return prefixNumber >= 0 && prefixNumber <= 128
  }

  return false
}

function isValidIpv4(value: string) {
  const octets = value.split(".")
  if (octets.length !== 4) {
    return false
  }

  return octets.every((octet) => {
    if (!/^\d+$/.test(octet)) {
      return false
    }

    const number = Number(octet)
    return number >= 0 && number <= 255
  })
}

function isValidIpv6(value: string) {
  if (!/^[0-9A-Fa-f:]+$/.test(value)) {
    return false
  }

  const blocks = value.split("::")
  if (blocks.length > 2) {
    return false
  }

  if (blocks.length === 2) {
    const left = blocks[0] ? blocks[0].split(":") : []
    const right = blocks[1] ? blocks[1].split(":") : []
    if (
      !left.every(isValidIpv6Block) ||
      !right.every(isValidIpv6Block) ||
      left.length + right.length >= 8
    ) {
      return false
    }

    return true
  }

  const parts = value.split(":")
  return parts.length === 8 && parts.every(isValidIpv6Block)
}

function isValidIpv6Block(part: string) {
  return /^[0-9A-Fa-f]{1,4}$/.test(part)
}
