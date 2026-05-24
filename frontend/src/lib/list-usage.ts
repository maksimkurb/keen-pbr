export type ListUsageRef = {
  ruleIndex: number
  target: string
}

export function getRulesUsingList<T>(
  listName: string,
  rules: T[],
  getListNames: (rule: T) => string[] | undefined,
  getTarget: (rule: T) => string,
  excludeRuleIndex?: number
): ListUsageRef[] {
  return rules.flatMap((rule, ruleIndex) => {
    if (excludeRuleIndex !== undefined && ruleIndex === excludeRuleIndex) {
      return []
    }

    if (!getListNames(rule)?.includes(listName)) {
      return []
    }

    return [{ ruleIndex, target: getTarget(rule) }]
  })
}

export function buildListUsageByName<T>(
  rules: T[],
  getListNames: (rule: T) => string[] | undefined,
  getTarget: (rule: T) => string,
  excludeRuleIndex?: number
): Map<string, ListUsageRef[]> {
  const usageByName = new Map<string, ListUsageRef[]>()

  rules.forEach((rule, ruleIndex) => {
    if (excludeRuleIndex !== undefined && ruleIndex === excludeRuleIndex) {
      return
    }

    for (const listName of getListNames(rule) ?? []) {
      if (!listName) {
        continue
      }

      const usage = usageByName.get(listName) ?? []
      usage.push({ ruleIndex, target: getTarget(rule) })
      usageByName.set(listName, usage)
    }
  })

  return usageByName
}
