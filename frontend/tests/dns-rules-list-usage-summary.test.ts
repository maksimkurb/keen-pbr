import { describe, expect, test } from "bun:test"

import {
  buildListUsageByDnsRules,
  describeDnsRuleRefForListUsage,
  formatDnsListRefsUsageSummary,
} from "../src/pages/dns-rules-utils"

describe("DNS list usage helpers", () => {
  test("buildListUsageByDnsRules aggregates lists and excludes index", () => {
    const rules = [
      { server: "upstream", lists: ["a", "b"] },
      { server: "backup", list: ["a"] },
    ]

    const all = buildListUsageByDnsRules(rules)
    expect(all.get("a")?.length).toBe(2)
    expect(all.get("b")?.length).toBe(1)

    const edit0 = buildListUsageByDnsRules(rules, 0)
    expect(edit0.get("a")?.length).toBe(1)
    expect(edit0.get("a")?.[0]?.server).toBe("backup")
    expect(edit0.get("b")).toBeUndefined()
  })

  test("formatDnsListRefsUsageSummary includes rule indices and server", () => {
    const rules = [
      { server: "upstream", lists: ["shared"] },
      { server: "backup", lists: ["shared", "other"] },
    ]
    const refs = buildListUsageByDnsRules(rules).get("shared") ?? []
    const summary = formatDnsListRefsUsageSummary(refs, rules)

    expect(summary).toContain("#1 → upstream")
    expect(summary).toContain("#2 → backup")
    expect(summary).toContain("lists: shared")
    expect(summary).toContain(" • ")
  })

  test("describeDnsRuleRefForListUsage falls back without rule payload", () => {
    expect(
      describeDnsRuleRefForListUsage(
        { ruleIndex: 1, server: "upstream" },
        undefined,
      ),
    ).toBe("#2 → upstream")
  })
})
