import type { ConfigObject } from "@/api/generated/model/configObject"
import i18n from "@/i18n"

export type DnsRuleDraft = {
  enabled: boolean
  servers: string[]
  list: string[]
  allow_domain_rebinding: boolean
}

export type RuleErrors = {
  servers?: string
  lists?: string
  duplicate?: string
}

export function getRuleDraft(rule?: {
  enabled?: boolean | null
  servers?: string[]
  list?: string[]
  allow_domain_rebinding?: boolean
}): DnsRuleDraft {
  return {
    enabled: rule?.enabled ?? true,
    servers: rule?.servers ?? [],
    list: rule?.list ?? [],
    allow_domain_rebinding: rule?.allow_domain_rebinding ?? false,
  }
}

export function buildUpdatedConfigWithRules(
  config: ConfigObject,
  fallback: string[],
  rules: DnsRuleDraft[]
): ConfigObject {
  return {
    ...config,
    dns: {
      ...config.dns,
      fallback,
      rules: rules.map((rule) => ({
        enabled: rule.enabled,
        servers: rule.servers,
        list: rule.list,
        allow_domain_rebinding: rule.allow_domain_rebinding,
      })),
    },
  }
}

export function setDnsRuleEnabled(
  rules: DnsRuleDraft[],
  index: number,
  enabled: boolean
) {
  return rules.map((rule, ruleIndex) =>
    ruleIndex === index ? { ...rule, enabled } : rule
  )
}

export function validateRules(
  rules: DnsRuleDraft[],
  serverTags: string[],
  listOptions: string[]
): Record<number, RuleErrors> {
  const t = i18n.t.bind(i18n)
  const errors: Record<number, RuleErrors> = {}
  const serverTagSet = new Set(serverTags)
  const listOptionSet = new Set(listOptions)
  const seenRules = new Set<string>()

  for (const [index, rule] of rules.entries()) {
    if (!rule.enabled) {
      continue
    }

    const nextRuleErrors: RuleErrors = {}
    const parsedLists = rule.list

    if (
      rule.servers.length === 0 ||
      rule.servers.some((serverTag) => !serverTagSet.has(serverTag))
    ) {
      nextRuleErrors.servers = t(
        "pages.dnsRuleUpsert.validation.serverRequired"
      )
    }

    if (parsedLists.length === 0) {
      nextRuleErrors.lists = t("common.validation.required")
    }

    const missingLists = parsedLists.filter(
      (listName) => !listOptionSet.has(listName)
    )
    if (missingLists.length > 0) {
      nextRuleErrors.lists = t("pages.dnsRuleUpsert.validation.unknownLists", {
        lists: missingLists.join(", "),
      })
    }

    const dedupeKey = `${[...rule.servers].sort().join("|")}::${[...parsedLists].sort().join("|")}`
    if (seenRules.has(dedupeKey)) {
      nextRuleErrors.duplicate = t("pages.dnsRuleUpsert.validation.duplicate")
    }

    seenRules.add(dedupeKey)

    if (Object.keys(nextRuleErrors).length > 0) {
      errors[index] = nextRuleErrors
    }
  }

  return errors
}
