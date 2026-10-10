import type { ConfigObject } from "@/api/generated/model/configObject"
import type { Outbound } from "@/api/generated/model/outbound"
import type { OutboundGroup } from "@/api/generated/model/outboundGroup"
import type { OutboundGroupMember } from "@/api/generated/model/outboundGroupMember"

/**
 * Members of one group. Reads the canonical `members` and, for configs that
 * were not upgraded yet, the legacy `candidates` / `outbounds` fields.
 */
export function getOutboundGroupMembers(
  group: OutboundGroup
): OutboundGroupMember[] {
  if (group.members) {
    return group.members
  }
  if (group.candidates) {
    return group.candidates.map((candidate) => ({
      outbound: candidate.outbound,
      target: candidate.target,
    }))
  }
  return (group.outbounds ?? []).map((outbound) => ({ outbound }))
}

export function getOutboundGroupTags(group: OutboundGroup): string[] {
  return getOutboundGroupMembers(group).map((member) => member.outbound)
}

/**
 * Groups in effective priority order, each in the canonical `members` form.
 * Legacy configs ordered steps by the group `weight` (default 1, stable); the
 * daemon upgrades them on load, this keeps the UI correct before that.
 */
export function getOrderedOutboundGroups(outbound: Outbound): OutboundGroup[] {
  return (outbound.outbound_groups ?? [])
    .map((group, index) => ({
      members: getOutboundGroupMembers(group),
      index,
      weight: group.members ? 1 : (group.weight ?? 1),
    }))
    .sort(
      (left, right) => left.weight - right.weight || left.index - right.index
    )
    .map(({ members }) => ({ members }))
}

export type OutboundDeleteImpact = {
  deletedOutboundTags: string[]
  routeRuleIndexes: number[]
  dnsServerDetours: string[]
  urltestMemberships: Array<{
    outboundTag: string
    groupIndex: number
    removedTags: string[]
  }>
  removedUrltestGroups: Array<{
    outboundTag: string
    groupIndex: number
  }>
}

export function getOutboundDeleteImpact(
  config: ConfigObject,
  initialTags: Iterable<string>
): OutboundDeleteImpact {
  const deletedTags = new Set(initialTags)
  let changed = true

  while (changed) {
    changed = false

    for (const outbound of config.outbounds ?? []) {
      if (
        (outbound.type !== "urltest" && outbound.type !== "icmptest") ||
        deletedTags.has(outbound.tag)
      ) {
        continue
      }

      const remainingGroups = getOrderedOutboundGroups(outbound)
        .map((group) => cleanupGroupReferences(group, deletedTags))
        .filter((group) => getOutboundGroupTags(group).length > 0)

      if (remainingGroups.length === 0) {
        deletedTags.add(outbound.tag)
        changed = true
      }
    }
  }

  const deletedTagList = [...deletedTags]
  const routeRuleIndexes = (config.route?.rules ?? []).flatMap((rule, index) =>
    deletedTags.has(rule.outbound) ? [index] : []
  )
  const dnsServerDetours = (config.dns?.servers ?? []).flatMap((server) =>
    server.detour && deletedTags.has(server.detour) ? [server.tag] : []
  )
  const urltestMemberships: OutboundDeleteImpact["urltestMemberships"] = []
  const removedUrltestGroups: OutboundDeleteImpact["removedUrltestGroups"] = []

  for (const outbound of config.outbounds ?? []) {
    if (
      (outbound.type !== "urltest" && outbound.type !== "icmptest") ||
      deletedTags.has(outbound.tag)
    ) {
      continue
    }

    for (const [groupIndex, group] of getOrderedOutboundGroups(
      outbound
    ).entries()) {
      const groupTags = getOutboundGroupTags(group)
      const removedTags = groupTags.filter((tag) => deletedTags.has(tag))

      if (removedTags.length > 0) {
        urltestMemberships.push({
          outboundTag: outbound.tag,
          groupIndex,
          removedTags,
        })
      }

      if (
        removedTags.length > 0 &&
        groupTags.every((tag) => deletedTags.has(tag))
      ) {
        removedUrltestGroups.push({
          outboundTag: outbound.tag,
          groupIndex,
        })
      }
    }
  }

  return {
    deletedOutboundTags: deletedTagList,
    routeRuleIndexes,
    dnsServerDetours,
    urltestMemberships,
    removedUrltestGroups,
  }
}

export function buildUpdatedConfigForOutboundsDelete(
  config: ConfigObject,
  initialTags: Iterable<string>
): ConfigObject {
  const impact = getOutboundDeleteImpact(config, initialTags)
  const deletedTags = new Set(impact.deletedOutboundTags)

  return {
    ...config,
    outbounds: (config.outbounds ?? [])
      .filter((outbound) => !deletedTags.has(outbound.tag))
      .map((outbound) => cleanupOutboundReferences(outbound, deletedTags)),
    route: {
      ...config.route,
      rules: (config.route?.rules ?? []).filter(
        (rule) => !deletedTags.has(rule.outbound)
      ),
    },
    dns: {
      ...config.dns,
      servers: (config.dns?.servers ?? []).map((server) => {
        if (!server.detour || !deletedTags.has(server.detour)) {
          return server
        }

        const serverWithoutDetour = { ...server }
        delete serverWithoutDetour.detour
        return serverWithoutDetour
      }),
    },
  }
}

function cleanupOutboundReferences(
  outbound: Outbound,
  deletedTags: ReadonlySet<string>
): Outbound {
  if (outbound.type !== "urltest" && outbound.type !== "icmptest") {
    return outbound
  }

  return {
    ...outbound,
    outbound_groups: getOrderedOutboundGroups(outbound)
      .map((group) => cleanupGroupReferences(group, deletedTags))
      .filter((group) => getOutboundGroupTags(group).length > 0),
  }
}

function cleanupGroupReferences(
  group: OutboundGroup,
  deletedTags: ReadonlySet<string>
): OutboundGroup {
  return {
    members: getOutboundGroupMembers(group).filter(
      (member) => !deletedTags.has(member.outbound)
    ),
  }
}
