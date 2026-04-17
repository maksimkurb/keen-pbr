import type { ConfigObject } from "@/api/generated/model/configObject"
import i18n from "@/i18n"

export type DnsRuleDraft = {
  enabled: boolean
  server: string
  lists: string[]
  allowDomainRebinding: boolean
}

export type RuleErrors = {
  server?: string
  lists?: string
  duplicate?: string
}

export function getRuleDraft(rule?: {
  enabled?: boolean | null
  server?: string
  list?: string[]
  allow_domain_rebinding?: boolean
}): DnsRuleDraft {
  return {
    enabled: rule?.enabled ?? true,
    server: rule?.server ?? "",
    lists: rule?.list ?? [],
    allowDomainRebinding: rule?.allow_domain_rebinding ?? false,
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
        server: rule.server,
        list: rule.lists,
        allow_domain_rebinding: rule.allowDomainRebinding,
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
    const parsedLists = rule.lists

    if (!rule.server || !serverTagSet.has(rule.server)) {
      nextRuleErrors.server = t("pages.dnsRuleUpsert.validation.serverRequired")
    }

    if (parsedLists.length === 0) {
      nextRuleErrors.lists = t("pages.dnsRuleUpsert.validation.listsRequired")
    }

    const missingLists = parsedLists.filter(
      (listName) => !listOptionSet.has(listName)
    )
    if (missingLists.length > 0) {
      nextRuleErrors.lists = t("pages.dnsRuleUpsert.validation.unknownLists", {
        lists: missingLists.join(", "),
      })
    }

    const dedupeKey = `${rule.server}::${[...parsedLists].sort().join("|")}`
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
