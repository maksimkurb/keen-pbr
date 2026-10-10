import type { Outbound } from "@/api/generated/model/outbound"
import type { OutboundGroup } from "@/api/generated/model/outboundGroup"
import {
  getOrderedOutboundGroups,
  getOutboundGroupMembers,
} from "@/pages/outbounds-utils"

export type StrictEnforcementOption = "default" | "enabled" | "disabled"
export type StrictActionOption = "default" | "unreachable" | "blackhole"
export type ConntrackOnSwitchOption = "preserve" | "delete"

/**
 * The outbound editor's draft. Names and nesting mirror the API `Outbound`
 * object, so a draft path is the API path relative to the outbound
 * (`retry.attempts`, `outbound_groups[0].members[1].weight`). Values are
 * strings for text inputs; the kill-switch fields hold UI options.
 */
export type OutboundDraft = {
  tag: string
  type: Outbound["type"] | ""
  interface: string
  gateway: string
  gateway6: string
  table: string
  /** Steps in priority order (first is tried first); legacy forms are normalized on load. */
  outbound_groups: OutboundGroupDraft[]
  strategy: NonNullable<Outbound["strategy"]>
  conntrack_on_switch: ConntrackOnSwitchOption
  url: string
  interval_ms: string
  tolerance_ms: string
  count: string
  max_failed: string
  packet_interval_ms: string
  probe_timeout_ms: string
  max_rtt_ms: string
  retry: {
    attempts: string
    interval_ms: string
  }
  circuit_breaker: {
    failure_threshold: string
    success_threshold: string
    timeout_ms: string
    half_open_max_requests: string
  }
  strict_enforcement: StrictEnforcementOption
  strict_enforcement_action: StrictActionOption
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

/**
 * Per-type defaults for fields whose default depends on urltest vs icmptest,
 * keyed by draft path.
 */
export const TEST_GROUP_DEFAULTS = {
  urltest: {
    interval_ms: "180000",
    tolerance_ms: "100",
    probe_timeout_ms: "5000",
    "circuit_breaker.timeout_ms": "30000",
  },
  icmptest: {
    interval_ms: "60000",
    tolerance_ms: "10",
    probe_timeout_ms: "1000",
    "circuit_breaker.timeout_ms": "60000",
  },
} as const

export const sampleNewOutbound: OutboundDraft = {
  tag: "",
  type: "",
  interface: "",
  gateway: "",
  gateway6: "",
  table: "",
  outbound_groups: [{ members: [] }],
  strategy: "priority",
  conntrack_on_switch: "preserve",
  url: "https://www.gstatic.com/generate_204",
  interval_ms: TEST_GROUP_DEFAULTS.urltest.interval_ms,
  tolerance_ms: TEST_GROUP_DEFAULTS.urltest.tolerance_ms,
  count: "3",
  max_failed: "0",
  packet_interval_ms: "200",
  probe_timeout_ms: TEST_GROUP_DEFAULTS.urltest.probe_timeout_ms,
  max_rtt_ms: "500",
  retry: { attempts: "3", interval_ms: "1000" },
  circuit_breaker: {
    failure_threshold: "5",
    success_threshold: "2",
    timeout_ms: TEST_GROUP_DEFAULTS.urltest["circuit_breaker.timeout_ms"],
    half_open_max_requests: "1",
  },
  strict_enforcement: "default",
  strict_enforcement_action: "default",
}

export function mapOutboundToDraft(outbound: Outbound): OutboundDraft {
  const isIcmp = outbound.type === "icmptest"
  const defaults = isIcmp
    ? TEST_GROUP_DEFAULTS.icmptest
    : TEST_GROUP_DEFAULTS.urltest
  const defaultBreakerTimeout = isIcmp
    ? Math.max(60000, outbound.interval_ms ?? 60000).toString()
    : defaults["circuit_breaker.timeout_ms"]

  // Legacy configs ordered steps by a group weight; this also reads them, so
  // the array order is the effective priority and saving writes `members`.
  const orderedGroups = getOrderedOutboundGroups(outbound)

  return {
    tag: outbound.tag,
    type: outbound.type,
    interface: outbound.interface ?? "",
    gateway: outbound.gateway ?? "",
    gateway6: outbound.gateway6 ?? "",
    table: outbound.table?.toString() ?? "",
    outbound_groups: orderedGroups.length
      ? orderedGroups.map((group) => ({
          members: getOutboundGroupMembers(group).map((member) => ({
            outbound: member.outbound,
            target: member.target ?? "",
            weight: member.weight?.toString() ?? "",
          })),
        }))
      : sampleNewOutbound.outbound_groups,
    url: outbound.url ?? sampleNewOutbound.url,
    strategy: outbound.strategy ?? sampleNewOutbound.strategy,
    conntrack_on_switch:
      outbound.conntrack_on_switch ?? sampleNewOutbound.conntrack_on_switch,
    interval_ms: outbound.interval_ms?.toString() ?? defaults.interval_ms,
    tolerance_ms: outbound.tolerance_ms?.toString() ?? defaults.tolerance_ms,
    count: outbound.count?.toString() ?? sampleNewOutbound.count,
    max_failed: outbound.max_failed?.toString() ?? sampleNewOutbound.max_failed,
    packet_interval_ms:
      outbound.packet_interval_ms?.toString() ??
      sampleNewOutbound.packet_interval_ms,
    probe_timeout_ms:
      outbound.probe_timeout_ms?.toString() ?? defaults.probe_timeout_ms,
    max_rtt_ms: outbound.max_rtt_ms?.toString() ?? sampleNewOutbound.max_rtt_ms,
    retry: {
      attempts:
        outbound.retry?.attempts?.toString() ??
        sampleNewOutbound.retry.attempts,
      interval_ms:
        outbound.retry?.interval_ms?.toString() ??
        sampleNewOutbound.retry.interval_ms,
    },
    circuit_breaker: {
      failure_threshold:
        outbound.circuit_breaker?.failure_threshold?.toString() ??
        sampleNewOutbound.circuit_breaker.failure_threshold,
      success_threshold:
        outbound.circuit_breaker?.success_threshold?.toString() ??
        sampleNewOutbound.circuit_breaker.success_threshold,
      timeout_ms:
        outbound.circuit_breaker?.timeout_ms?.toString() ??
        defaultBreakerTimeout,
      half_open_max_requests:
        outbound.circuit_breaker?.half_open_max_requests?.toString() ??
        sampleNewOutbound.circuit_breaker.half_open_max_requests,
    },
    strict_enforcement: mapStrictEnforcementToOption(
      outbound.strict_enforcement
    ),
    strict_enforcement_action: outbound.strict_enforcement_action ?? "default",
  }
}

export function buildOutboundPayload(draft: OutboundDraft): Outbound {
  if (!draft.type) throw new Error("Outbound type is required")
  const tag = draft.tag.trim()
  const circuitBreaker = {
    failure_threshold: parseNumber(draft.circuit_breaker.failure_threshold),
    success_threshold: parseNumber(draft.circuit_breaker.success_threshold),
    timeout_ms: parseNumber(draft.circuit_breaker.timeout_ms),
    half_open_max_requests: parseNumber(
      draft.circuit_breaker.half_open_max_requests
    ),
  }

  if (draft.type === "interface") {
    return {
      type: "interface",
      tag,
      interface: draft.interface.trim() || undefined,
      gateway: draft.gateway.trim() || undefined,
      gateway6: draft.gateway6.trim() || undefined,
      strict_enforcement: mapStrictEnforcementToBoolean(
        draft.strict_enforcement
      ),
      strict_enforcement_action:
        draft.strict_enforcement_action === "default"
          ? undefined
          : draft.strict_enforcement_action,
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
      url: draft.url.trim() || undefined,
      interval_ms: parseNumber(draft.interval_ms),
      probe_timeout_ms: parseNumber(draft.probe_timeout_ms),
      tolerance_ms: parseNumber(draft.tolerance_ms),
      strategy: draft.strategy,
      conntrack_on_switch: draft.conntrack_on_switch,
      outbound_groups: buildGroupPayload(draft.outbound_groups, false),
      retry: {
        attempts: parseNumber(draft.retry.attempts),
        interval_ms: parseNumber(draft.retry.interval_ms),
      },
      circuit_breaker: circuitBreaker,
    }
  }

  if (draft.type === "icmptest") {
    return {
      type: "icmptest",
      tag,
      count: parseNumber(draft.count),
      max_failed: parseNumber(draft.max_failed),
      packet_interval_ms: parseNumber(draft.packet_interval_ms),
      probe_timeout_ms: parseNumber(draft.probe_timeout_ms),
      max_rtt_ms: parseNumber(draft.max_rtt_ms),
      interval_ms: parseNumber(draft.interval_ms),
      tolerance_ms: parseNumber(draft.tolerance_ms),
      strategy: draft.strategy,
      conntrack_on_switch: draft.conntrack_on_switch,
      outbound_groups: buildGroupPayload(draft.outbound_groups, true),
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
  strict_enforcement: StrictEnforcementOption
  strict_enforcement_action: StrictActionOption
} {
  switch (choice) {
    case "inherit":
      return {
        strict_enforcement: "default",
        strict_enforcement_action: "default",
      }
    case "off":
      return {
        strict_enforcement: "disabled",
        strict_enforcement_action: "default",
      }
    case "reject":
      return {
        strict_enforcement: "enabled",
        strict_enforcement_action: "unreachable",
      }
    case "drop":
      return {
        strict_enforcement: "enabled",
        strict_enforcement_action: "blackhole",
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
