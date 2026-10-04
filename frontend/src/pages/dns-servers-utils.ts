import type { ConfigObject } from "@/api/generated/model/configObject"

export type DnsServerDeleteImpact = {
  matchingRuleIndexes: number[]
  usesFallback: boolean
}

export function getDnsServerDeleteReferenceInfo(
  config: ConfigObject,
  serverTags: Iterable<string>
): DnsServerDeleteImpact & {
  matchingRulesCount: number
} {
  const impact = getDnsServerDeleteImpact(config, serverTags)

  return {
    ...impact,
    matchingRulesCount: impact.matchingRuleIndexes.length,
  }
}

export function getDnsServerDeleteImpact(
  config: ConfigObject,
  serverTags: Iterable<string>
): DnsServerDeleteImpact {
  const tagSet = new Set(serverTags)
  const rules = config.dns?.rules ?? []
  const fallback = config.dns?.fallback ?? []

  return {
    matchingRuleIndexes: rules.flatMap((rule, index) =>
      tagSet.has(rule.server) ? [index] : []
    ),
    usesFallback: fallback.some((tag) => tagSet.has(tag)),
  }
}

export function buildUpdatedConfigForDnsServersDelete(
  config: ConfigObject,
  serverTags: Iterable<string>,
  cleanupReferences: boolean
): ConfigObject {
  const tagSet = new Set(serverTags)
  const dnsConfig = config.dns
  const rules = dnsConfig?.rules ?? []
  const fallback = dnsConfig?.fallback ?? []

  return {
    ...config,
    dns: {
      ...(dnsConfig ?? {}),
      servers: (dnsConfig?.servers ?? []).filter(
        (server) => !tagSet.has(server.tag)
      ),
      rules: cleanupReferences
        ? rules.filter((rule) => !tagSet.has(rule.server))
        : rules,
      fallback: cleanupReferences
        ? fallback.filter((tag) => !tagSet.has(tag))
        : fallback,
    },
  }
}
