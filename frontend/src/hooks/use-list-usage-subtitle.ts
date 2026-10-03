import { useCallback, useMemo } from "react"
import { useTranslation } from "react-i18next"

import type { RouteRule } from "@/api/generated/model/routeRule"
import { buildListUsageByName } from "@/lib/list-usage"

export function useListUsageSubtitle(
  rules: RouteRule[],
  excludeRuleIndex?: number
): (listName: string) => string | undefined
export function useListUsageSubtitle(
  rules: RouteRule[],
  excludeRuleIndex?: number
) {
  const { t } = useTranslation()
  const usageByName = useMemo(() => {
    return buildListUsageByName(
      rules,
      (rule) => rule.list,
      (rule) => rule.outbound,
      excludeRuleIndex
    )
  }, [excludeRuleIndex, rules])

  return useCallback(
    (listName: string) => {
      const usages = usageByName.get(listName)
      if (!usages?.length) {
        return undefined
      }

      const summary = usages
        .map((usage) => `#${usage.ruleIndex + 1} → ${usage.target}`)
        .join(", ")

      return t("common.listUsage.usedElsewhere", { summary })
    },
    [t, usageByName]
  )
}
