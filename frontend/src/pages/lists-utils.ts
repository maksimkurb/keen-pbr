import type { ConfigObject } from "@/api/generated/model/configObject"

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
        .filter((rule) => rule.list.length > 0),
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
