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

  test("formatDnsListRefsUsageSummary shows rule number and server only, joined by comma", () => {
    const rules = [
      { server: "upstream", lists: ["shared"] },
      { server: "backup", lists: ["shared", "other"] },
    ]
    const refs = buildListUsageByDnsRules(rules).get("shared") ?? []
    const summary = formatDnsListRefsUsageSummary(refs)

    expect(summary).toContain("#1 → upstream")
    expect(summary).toContain("#2 → backup")
    expect(summary).toContain(", ")
    expect(summary).not.toContain("lists:")
    expect(summary).not.toContain(" • ")
  })

  test("describeDnsRuleRefForListUsage returns index and server", () => {
    expect(
      describeDnsRuleRefForListUsage({ ruleIndex: 1, server: "upstream" }),
    ).toBe("#2 → upstream")
  })
})
