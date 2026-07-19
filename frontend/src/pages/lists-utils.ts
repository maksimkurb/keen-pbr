import type { ConfigObject } from "@/api/generated/model/configObject"
import type { RouteRule } from "@/api/generated/model/routeRule"

export type ListDeleteImpact = {
  dnsRuleIndexes: number[]
  routeRuleIndexes: number[]
  removedDnsRuleIndexes: number[]
  removedRouteRuleIndexes: number[]
}

function hasRouteMatchConditionExceptLists(rule: RouteRule): boolean {
  return Boolean(
    rule.proto ||
    rule.dscp !== undefined ||
    rule.src_port ||
    rule.dest_port ||
    rule.src_addr ||
    rule.dest_addr
  )
}

export function getListDeleteImpact(
  config: ConfigObject,
  listIds: Iterable<string>
): ListDeleteImpact {
  const listIdSet = new Set(listIds)
  const dnsRuleIndexes: number[] = []
  const routeRuleIndexes: number[] = []
  const removedDnsRuleIndexes: number[] = []
  const removedRouteRuleIndexes: number[] = []

  for (const [index, rule] of (config.route?.rules ?? []).entries()) {
    const beforeLists = rule.list ?? []
    const afterLists = beforeLists.filter((name) => !listIdSet.has(name))

    if (afterLists.length !== beforeLists.length) {
      routeRuleIndexes.push(index)
    }

    if (
      beforeLists.length > 0 &&
      afterLists.length === 0 &&
      !hasRouteMatchConditionExceptLists(rule)
    ) {
      removedRouteRuleIndexes.push(index)
    }
  }

  for (const [index, rule] of (config.dns?.rules ?? []).entries()) {
    const afterLists = rule.list.filter((name) => !listIdSet.has(name))

    if (afterLists.length !== rule.list.length) {
      dnsRuleIndexes.push(index)
    }

    if (rule.list.length > 0 && afterLists.length === 0) {
      removedDnsRuleIndexes.push(index)
    }
  }

  return {
    dnsRuleIndexes,
    routeRuleIndexes,
    removedDnsRuleIndexes,
    removedRouteRuleIndexes,
  }
}

export function buildUpdatedConfigForListsDelete(
  config: ConfigObject,
  listIds: string[]
): ConfigObject {
  return listIds.reduce(
    (nextConfig, listId) => buildUpdatedConfigForListDelete(nextConfig, listId),
    config
  )
}

export function listDeletesAltersRoutingOrDnsRefs(
  before: ConfigObject,
  after: ConfigObject
) {
  return (
    JSON.stringify(before.route?.rules ?? []) !==
      JSON.stringify(after.route?.rules ?? []) ||
    JSON.stringify(before.dns?.rules ?? []) !==
      JSON.stringify(after.dns?.rules ?? [])
  )
}

export function buildUpdatedConfigForListDelete(
  config: ConfigObject,
  listId: string
): ConfigObject {
  const nextLists = { ...(config.lists ?? {}) }
  delete nextLists[listId]

  return {
    ...config,
    lists: nextLists,
    route: {
      ...config.route,
      rules: (config.route?.rules ?? [])
        .map((rule) => ({
          ...rule,
          list: (rule.list ?? []).filter((name) => name !== listId),
        }))
        .filter(
          (rule) =>
            rule.list.length > 0 || hasRouteMatchConditionExceptLists(rule)
        ),
    },
    dns: {
      ...config.dns,
      rules: (config.dns?.rules ?? [])
        .map((rule) => ({
          ...rule,
          list: rule.list.filter((name) => name !== listId),
        }))
        .filter((rule) => rule.list.length > 0),
    },
  }
}
