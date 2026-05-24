import { useCallback, useMemo } from "react"
import { useTranslation } from "react-i18next"

import type { DnsRule } from "@/api/generated/model/dnsRule"
import type { RouteRule } from "@/api/generated/model/routeRule"
import { buildListUsageByName } from "@/lib/list-usage"

type ListUsageRuleType = "routing" | "dns"

export function useListUsageSubtitle(
  rules: RouteRule[],
  ruleType: "routing",
  excludeRuleIndex?: number
): (listName: string) => string | undefined
export function useListUsageSubtitle(
  rules: DnsRule[],
  ruleType: "dns",
  excludeRuleIndex?: number
): (listName: string) => string | undefined
export function useListUsageSubtitle(
  rules: RouteRule[] | DnsRule[],
  ruleType: ListUsageRuleType,
  excludeRuleIndex?: number
) {
  const { t } = useTranslation()
  const usageByName = useMemo(() => {
    if (ruleType === "routing") {
      return buildListUsageByName(
        rules as RouteRule[],
        (rule) => rule.list,
        (rule) => rule.outbound,
        excludeRuleIndex
      )
    }

    return buildListUsageByName(
      rules as DnsRule[],
      (rule) => rule.list,
      (rule) => rule.server,
      excludeRuleIndex
    )
  }, [excludeRuleIndex, ruleType, rules])

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
