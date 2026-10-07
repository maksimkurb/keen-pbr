import type { Outbound } from "@/api/generated/model/outbound"
import type { OutboundGroup } from "@/api/generated/model/outboundGroup"
import {
  getOrderedOutboundGroups,
  getOutboundGroupMembers,
} from "@/pages/outbounds-utils"

export type StrictEnforcementOption = "default" | "enabled" | "disabled"
export type StrictActionOption = "default" | "unreachable" | "blackhole"
export type ConntrackOnSwitchOption = "preserve" | "delete"

export type OutboundDraft = {
  tag: string
  type: Outbound["type"]
  interfaceName: string
  gateway: string
  gateway6: string
  table: string
  /** Steps in priority order (first is tried first); legacy forms are normalized on load. */
  outboundGroups: OutboundGroupDraft[]
  strategy: NonNullable<Outbound["strategy"]>
  conntrackOnSwitch: ConntrackOnSwitchOption
  probeUrl: string
  interval: string
  tolerance: string
  count: string
  maxFailed: string
  packetInterval: string
  probeTimeout: string
  maxRtt: string
  retryAttempts: string
  retryInterval: string
  circuitBreakerFailures: string
  circuitBreakerSuccesses: string
  circuitBreakerTimeout: string
  circuitBreakerHalfOpen: string
  strictEnforcement: StrictEnforcementOption
  strictEnforcementAction: StrictActionOption
}

export type OutboundGroupDraft = {
  members: OutboundGroupMemberDraft[]
}

export type OutboundGroupMemberDraft = {
  outbound: string
  /** Ping target (icmptest only; ignored for urltest). */
  target: string
  /** Share of new connections in the balance strategy, "" = default (1). */
  weight: string
}

export const OUTBOUND_FIELD_NAMES = {
  tag: "tag",
  type: "type",
  interfaceName: "interfaceName",
  gateway: "gateway",
  gateway6: "gateway6",
  table: "table",
  outboundGroups: "outboundGroups",
  strategy: "strategy",
  conntrackOnSwitch: "conntrackOnSwitch",
  probeUrl: "probeUrl",
  interval: "interval",
  tolerance: "tolerance",
  count: "count",
  maxFailed: "maxFailed",
  packetInterval: "packetInterval",
  probeTimeout: "probeTimeout",
  maxRtt: "maxRtt",
  retryAttempts: "retryAttempts",
  retryInterval: "retryInterval",
  circuitBreakerFailures: "circuitBreakerFailures",
  circuitBreakerSuccesses: "circuitBreakerSuccesses",
  circuitBreakerTimeout: "circuitBreakerTimeout",
  circuitBreakerHalfOpen: "circuitBreakerHalfOpen",
  strictEnforcement: "strictEnforcement",
  strictEnforcementAction: "strictEnforcementAction",
} as const

/** Per-type defaults for fields whose default depends on urltest vs icmptest. */
export const TEST_GROUP_DEFAULTS = {
  urltest: {
    interval: "180000",
    tolerance: "100",
    probeTimeout: "5000",
    circuitBreakerTimeout: "30000",
  },
  icmptest: {
    interval: "60000",
    tolerance: "10",
    probeTimeout: "1000",
    circuitBreakerTimeout: "60000",
  },
} as const

export const sampleNewOutbound: OutboundDraft = {
  tag: "",
  type: "interface",
  interfaceName: "",
  gateway: "",
  gateway6: "",
  table: "",
  outboundGroups: [{ members: [] }],
  strategy: "priority",
  conntrackOnSwitch: "preserve",
  probeUrl: "https://www.gstatic.com/generate_204",
  interval: TEST_GROUP_DEFAULTS.urltest.interval,
  tolerance: TEST_GROUP_DEFAULTS.urltest.tolerance,
  count: "3",
  maxFailed: "0",
  packetInterval: "200",
  probeTimeout: TEST_GROUP_DEFAULTS.urltest.probeTimeout,
  maxRtt: "500",
  retryAttempts: "3",
  retryInterval: "1000",
  circuitBreakerFailures: "5",
  circuitBreakerSuccesses: "2",
  circuitBreakerTimeout: TEST_GROUP_DEFAULTS.urltest.circuitBreakerTimeout,
  circuitBreakerHalfOpen: "1",
  strictEnforcement: "default",
  strictEnforcementAction: "default",
}

export function mapOutboundToDraft(outbound: Outbound): OutboundDraft {
  const isIcmp = outbound.type === "icmptest"
  const defaults = isIcmp
    ? TEST_GROUP_DEFAULTS.icmptest
    : TEST_GROUP_DEFAULTS.urltest
  const defaultBreakerTimeout = isIcmp
    ? Math.max(60000, outbound.interval_ms ?? 60000).toString()
    : defaults.circuitBreakerTimeout

  // Legacy configs ordered steps by a group weight; this also reads them, so
  // the array order is the effective priority and saving writes `members`.
  const orderedGroups = getOrderedOutboundGroups(outbound)

  return {
    tag: outbound.tag,
    type: outbound.type,
    interfaceName: outbound.interface ?? "",
    gateway: outbound.gateway ?? "",
    gateway6: outbound.gateway6 ?? "",
    table: outbound.table?.toString() ?? "",
    outboundGroups: orderedGroups.length
      ? orderedGroups.map((group) => ({
          members: getOutboundGroupMembers(group).map((member) => ({
            outbound: member.outbound,
            target: member.target ?? "",
            weight: member.weight?.toString() ?? "",
          })),
        }))
      : sampleNewOutbound.outboundGroups,
    probeUrl: outbound.url ?? sampleNewOutbound.probeUrl,
    strategy: outbound.strategy ?? sampleNewOutbound.strategy,
    conntrackOnSwitch:
      outbound.conntrack_on_switch ?? sampleNewOutbound.conntrackOnSwitch,
    interval: outbound.interval_ms?.toString() ?? defaults.interval,
    tolerance: outbound.tolerance_ms?.toString() ?? defaults.tolerance,
    count: outbound.count?.toString() ?? sampleNewOutbound.count,
    maxFailed: outbound.max_failed?.toString() ?? sampleNewOutbound.maxFailed,
    packetInterval:
      outbound.packet_interval_ms?.toString() ??
      sampleNewOutbound.packetInterval,
    probeTimeout:
      outbound.probe_timeout_ms?.toString() ?? defaults.probeTimeout,
    maxRtt: outbound.max_rtt_ms?.toString() ?? sampleNewOutbound.maxRtt,
    retryAttempts:
      outbound.retry?.attempts?.toString() ?? sampleNewOutbound.retryAttempts,
    retryInterval:
      outbound.retry?.interval_ms?.toString() ??
      sampleNewOutbound.retryInterval,
    circuitBreakerFailures:
      outbound.circuit_breaker?.failure_threshold?.toString() ??
      sampleNewOutbound.circuitBreakerFailures,
    circuitBreakerSuccesses:
      outbound.circuit_breaker?.success_threshold?.toString() ??
      sampleNewOutbound.circuitBreakerSuccesses,
    circuitBreakerTimeout:
      outbound.circuit_breaker?.timeout_ms?.toString() ?? defaultBreakerTimeout,
    circuitBreakerHalfOpen:
      outbound.circuit_breaker?.half_open_max_requests?.toString() ??
      sampleNewOutbound.circuitBreakerHalfOpen,
    strictEnforcement: mapStrictEnforcementToOption(
      outbound.strict_enforcement
    ),
    strictEnforcementAction: outbound.strict_enforcement_action ?? "default",
  }
}

export function buildOutboundPayload(draft: OutboundDraft): Outbound {
  const tag = draft.tag.trim()
  const circuitBreaker = {
    failure_threshold: parseNumber(draft.circuitBreakerFailures),
    success_threshold: parseNumber(draft.circuitBreakerSuccesses),
    timeout_ms: parseNumber(draft.circuitBreakerTimeout),
    half_open_max_requests: parseNumber(draft.circuitBreakerHalfOpen),
  }

  if (draft.type === "interface") {
    return {
      type: "interface",
      tag,
      interface: draft.interfaceName.trim() || undefined,
      gateway: draft.gateway.trim() || undefined,
      gateway6: draft.gateway6.trim() || undefined,
      strict_enforcement: mapStrictEnforcementToBoolean(
        draft.strictEnforcement
      ),
      strict_enforcement_action:
        draft.strictEnforcementAction === "default"
          ? undefined
          : draft.strictEnforcementAction,
    }
  }

  if (draft.type === "table") {
    return {
      type: "table",
      tag,
      table: parseNumber(draft.table),
    }
  }

  if (draft.type === "urltest") {
    return {
      type: "urltest",
      tag,
      url: draft.probeUrl.trim() || undefined,
      interval_ms: parseNumber(draft.interval),
      probe_timeout_ms: parseNumber(draft.probeTimeout),
      tolerance_ms: parseNumber(draft.tolerance),
      strategy: draft.strategy,
      conntrack_on_switch: draft.conntrackOnSwitch,
      outbound_groups: buildGroupPayload(draft.outboundGroups, false),
      retry: {
        attempts: parseNumber(draft.retryAttempts),
        interval_ms: parseNumber(draft.retryInterval),
      },
      circuit_breaker: circuitBreaker,
    }
  }

  if (draft.type === "icmptest") {
    return {
      type: "icmptest",
      tag,
      count: parseNumber(draft.count),
      max_failed: parseNumber(draft.maxFailed),
      packet_interval_ms: parseNumber(draft.packetInterval),
      probe_timeout_ms: parseNumber(draft.probeTimeout),
      max_rtt_ms: parseNumber(draft.maxRtt),
      interval_ms: parseNumber(draft.interval),
      tolerance_ms: parseNumber(draft.tolerance),
      strategy: draft.strategy,
      conntrack_on_switch: draft.conntrackOnSwitch,
      outbound_groups: buildGroupPayload(draft.outboundGroups, true),
      circuit_breaker: circuitBreaker,
    }
  }

  return {
    type: draft.type,
    tag,
  }
}

export function normalizeOutboundGroups(groups: string[][]) {
  if (!groups.length) {
    return [[]]
  }

  return groups.map((group) =>
    group.map((value) => value.trim()).filter(Boolean)
  )
}

/** Canonical `members` payload; empty steps are dropped like the daemon expects. */
function buildGroupPayload(
  groups: OutboundGroupDraft[],
  isIcmptest: boolean
): OutboundGroup[] {
  return groups.map((group) => ({
    members: group.members
      .filter((member) => member.outbound.trim())
      .map((member) => {
        // The daemon validates the weight (1..100); the input only allows
        // digits, so a non-empty value is always a number.
        const weight = /^\d+$/.test(member.weight.trim())
          ? Number(member.weight.trim())
          : undefined
        return {
          outbound: member.outbound.trim(),
          ...(isIcmptest ? { target: member.target.trim() } : {}),
          ...(weight === undefined ? {} : { weight }),
        }
      }),
  }))
}

export function getOutboundGroupTags(groups: OutboundGroupDraft[]): string[][] {
  return normalizeOutboundGroups(
    groups.map((group) => group.members.map((member) => member.outbound))
  )
}

/**
 * Rebuilds groups from tag lists while keeping each member's ping target and
 * balance weight (matched by outbound tag).
 */
export function synchronizeOutboundGroups(
  currentGroups: OutboundGroupDraft[],
  nextGroups: string[][]
): OutboundGroupDraft[] {
  const previous = new Map<string, OutboundGroupMemberDraft>()

  for (const group of currentGroups) {
    for (const member of group.members) {
      previous.set(member.outbound, member)
    }
  }

  return nextGroups.map((outbounds) => ({
    members: outbounds.map((outbound) => ({
      outbound,
      target: previous.get(outbound)?.target ?? "",
      weight: previous.get(outbound)?.weight ?? "",
    })),
  }))
}

export function moveGroup<T>(groups: T[], fromIndex: number, toIndex: number) {
  const next = [...groups]
  const [moved] = next.splice(fromIndex, 1)
  next.splice(toIndex, 0, moved)
  return next
}

export function parseNumber(value: string): number | undefined {
  const trimmed = value.trim()

  if (!trimmed) {
    return undefined
  }

  const parsed = Number(trimmed)
  return Number.isFinite(parsed) ? parsed : undefined
}

function mapStrictEnforcementToOption(
  value: boolean | undefined
): StrictEnforcementOption {
  if (value === undefined) {
    return "default"
  }

  return value ? "enabled" : "disabled"
}

function mapStrictEnforcementToBoolean(
  value: StrictEnforcementOption
): boolean | undefined {
  if (value === "default") {
    return undefined
  }

  return value === "enabled"
}

export function resolveOutboundFieldPath(
  path: string,
  tag: string
): string | undefined {
  const normalizedTag = tag.trim()
  if (path === "outbounds") {
    return OUTBOUND_FIELD_NAMES.tag
  }

  if (!normalizedTag) {
    return undefined
  }

  const prefix = `outbounds.${normalizedTag}`
  if (path === prefix || path === `${prefix}.tag`) {
    return OUTBOUND_FIELD_NAMES.tag
  }

  const simpleFields: Record<string, string> = {
    type: OUTBOUND_FIELD_NAMES.type,
    interface: OUTBOUND_FIELD_NAMES.interfaceName,
    gateway: OUTBOUND_FIELD_NAMES.gateway,
    gateway6: OUTBOUND_FIELD_NAMES.gateway6,
    table: OUTBOUND_FIELD_NAMES.table,
    url: OUTBOUND_FIELD_NAMES.probeUrl,
    interval_ms: OUTBOUND_FIELD_NAMES.interval,
    tolerance_ms: OUTBOUND_FIELD_NAMES.tolerance,
    strategy: OUTBOUND_FIELD_NAMES.strategy,
    conntrack_on_switch: OUTBOUND_FIELD_NAMES.conntrackOnSwitch,
    count: OUTBOUND_FIELD_NAMES.count,
    max_failed: OUTBOUND_FIELD_NAMES.maxFailed,
    packet_interval_ms: OUTBOUND_FIELD_NAMES.packetInterval,
    probe_timeout_ms: OUTBOUND_FIELD_NAMES.probeTimeout,
    max_rtt_ms: OUTBOUND_FIELD_NAMES.maxRtt,
    "retry.attempts": OUTBOUND_FIELD_NAMES.retryAttempts,
    "retry.interval_ms": OUTBOUND_FIELD_NAMES.retryInterval,
    "circuit_breaker.failure_threshold":
      OUTBOUND_FIELD_NAMES.circuitBreakerFailures,
    "circuit_breaker.success_threshold":
      OUTBOUND_FIELD_NAMES.circuitBreakerSuccesses,
    "circuit_breaker.timeout_ms": OUTBOUND_FIELD_NAMES.circuitBreakerTimeout,
    "circuit_breaker.half_open_max_requests":
      OUTBOUND_FIELD_NAMES.circuitBreakerHalfOpen,
    strict_enforcement: OUTBOUND_FIELD_NAMES.strictEnforcement,
    strict_enforcement_action: OUTBOUND_FIELD_NAMES.strictEnforcementAction,
  }
  if (path.startsWith(`${prefix}.`)) {
    const mapped = simpleFields[path.slice(prefix.length + 1)]
    if (mapped) {
      return mapped
    }
  }

  const groupsPrefix = `${prefix}.outbound_groups`
  if (path === groupsPrefix) {
    return OUTBOUND_FIELD_NAMES.outboundGroups
  }
  if (path.startsWith(groupsPrefix)) {
    const fieldPath = path.replace(
      groupsPrefix,
      OUTBOUND_FIELD_NAMES.outboundGroups
    )
    if (fieldPath.endsWith(".target")) {
      return fieldPath.replace(".candidates[", ".members[")
    }
    if (/\.members\[\d+\]\.weight$/.test(fieldPath)) {
      return fieldPath
    }

    // outbound_groups[N].members[M].outbound (or the legacy
    // .outbounds / .candidates paths) → the step itself.
    const memberPathIndex = fieldPath.search(/\.(members|candidates|outbounds)/)
    return memberPathIndex === -1
      ? fieldPath
      : fieldPath.slice(0, memberPathIndex)
  }

  return undefined
}

/**
 * Single user-facing kill-switch choice. The config keeps two fields
 * (`strict_enforcement` and `strict_enforcement_action`); this collapses them
 * into what actually happens when the interface is down.
 */
export type KillSwitchChoice = "inherit" | "off" | "reject" | "drop"

export function getKillSwitchChoice(
  strictEnforcement: StrictEnforcementOption,
  action: StrictActionOption,
  globalAction: "unreachable" | "blackhole"
): KillSwitchChoice {
  if (strictEnforcement === "default") {
    return "inherit"
  }
  if (strictEnforcement === "disabled") {
    return "off"
  }
  const effective = action === "default" ? globalAction : action
  return effective === "blackhole" ? "drop" : "reject"
}

export function getKillSwitchFields(choice: KillSwitchChoice): {
  strictEnforcement: StrictEnforcementOption
  strictEnforcementAction: StrictActionOption
} {
  switch (choice) {
    case "inherit":
      return {
        strictEnforcement: "default",
        strictEnforcementAction: "default",
      }
    case "off":
      return {
        strictEnforcement: "disabled",
        strictEnforcementAction: "default",
      }
    case "reject":
      return {
        strictEnforcement: "enabled",
        strictEnforcementAction: "unreachable",
      }
    case "drop":
      return {
        strictEnforcement: "enabled",
        strictEnforcementAction: "blackhole",
      }
  }
}

export const MIN_MEMBER_WEIGHT = 1
export const MAX_MEMBER_WEIGHT = 100

/** Whether a weight input is empty (default 1) or an integer in 1..100. */
export function isValidMemberWeight(value: string): boolean {
  const trimmed = value.trim()
  if (!trimmed) return true
  if (!/^\d+$/.test(trimmed)) return false
  const parsed = Number(trimmed)
  return parsed >= MIN_MEMBER_WEIGHT && parsed <= MAX_MEMBER_WEIGHT
}

/**
 * Share (0..100, rounded so a step adds up to 100) of new connections a member
 * gets when every member of its step is usable. Empty or invalid weights count as 1, like the daemon
 * default.
 */
export function getMemberSharePercent(
  group: OutboundGroupDraft,
  memberIndex: number
): number {
  const weights = group.members.map((member) =>
    isValidMemberWeight(member.weight) && member.weight.trim()
      ? Number(member.weight.trim())
      : 1
  )
  const total = weights.reduce((sum, weight) => sum + weight, 0)
  if (total === 0) return 0
  // Largest remainder rounding: the shown shares of a step add up to 100.
  const exact = weights.map((weight) => (weight * 100) / total)
  const shares = exact.map(Math.floor)
  let missing = 100 - shares.reduce((sum, share) => sum + share, 0)
  const byRemainder = exact
    .map((value, index) => ({ index, remainder: value - Math.floor(value) }))
    .sort((a, b) => b.remainder - a.remainder || a.index - b.index)
  for (const { index } of byRemainder) {
    if (missing <= 0) break
    shares[index] += 1
    missing -= 1
  }
  return shares[memberIndex]
}
