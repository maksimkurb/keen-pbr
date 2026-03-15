import type { ConfigObject } from "@/api/generated/model/configObject"

export type DnsRuleDraft = {
  server: string
  lists: string[]
}

export type RuleErrors = {
  server?: string
  lists?: string
  duplicate?: string
}

export function getRuleDraft(rule?: {
  server?: string
  list?: string[]
}): DnsRuleDraft {
  return {
    server: rule?.server ?? "",
    lists: rule?.list ?? [],
  }
}

export function buildUpdatedConfigWithRules(
  config: ConfigObject,
  fallback: string,
  rules: DnsRuleDraft[]
): ConfigObject {
  return {
    ...config,
    dns: {
      ...config.dns,
      fallback,
      rules: rules.map((rule) => ({
        server: rule.server,
        list: rule.lists,
      })),
    },
  }
}

export function validateRules(
  rules: DnsRuleDraft[],
  serverTags: string[],
  listOptions: string[]
): Record<number, RuleErrors> {
  const errors: Record<number, RuleErrors> = {}
  const serverTagSet = new Set(serverTags)
  const listOptionSet = new Set(listOptions)
  const seenRules = new Set<string>()

  for (const [index, rule] of rules.entries()) {
    const nextRuleErrors: RuleErrors = {}
    const parsedLists = rule.lists

    if (!rule.server || !serverTagSet.has(rule.server)) {
      nextRuleErrors.server = "Rule must reference an existing DNS server tag."
    }

    if (parsedLists.length === 0) {
      nextRuleErrors.lists = "Rule must include at least one list name."
    }

    const missingLists = parsedLists.filter(
      (listName) => !listOptionSet.has(listName)
    )
    if (missingLists.length > 0) {
      nextRuleErrors.lists = `Unknown list names: ${missingLists.join(", ")}`
    }

    const dedupeKey = `${rule.server}::${[...parsedLists].sort().join("|")}`
    if (seenRules.has(dedupeKey)) {
      nextRuleErrors.duplicate = "Duplicate rule entry."
    }

    seenRules.add(dedupeKey)

    if (Object.keys(nextRuleErrors).length > 0) {
      errors[index] = nextRuleErrors
    }
  }

  return errors
}
