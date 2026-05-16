import { describe, expect, test } from "bun:test"

import type { RouteRule } from "../src/api/generated/model/routeRule"
import {
  buildListUsageByRouteRules,
  describeRouteRuleRefForListUsage,
  formatRoutingListRefsUsageSummary,
} from "../src/pages/routing-rules-utils"

function rule(partial: RouteRule): RouteRule {
  return partial
}

describe("routing list usage helpers", () => {
  test("buildListUsageByRouteRules aggregates lists and excludes index", () => {
    const rules: RouteRule[] = [
      rule({ outbound: "wan", list: ["a", "b"] }),
      rule({ outbound: "vpn", list: ["a"] }),
    ]

    const all = buildListUsageByRouteRules(rules)
    expect(all.get("a")?.length).toBe(2)
    expect(all.get("b")?.length).toBe(1)

    const edit0 = buildListUsageByRouteRules(rules, 0)
    expect(edit0.get("a")?.length).toBe(1)
    expect(edit0.get("a")?.[0]?.outbound).toBe("vpn")
    expect(edit0.get("b")).toBeUndefined()
  })

  test("formatRoutingListRefsUsageSummary includes rule indices, outbound, and criteria", () => {
    const rules: RouteRule[] = [
      rule({
        outbound: "wan",
        list: ["shared"],
        dest_addr: "10.0.0.0/24",
      }),
      rule({
        outbound: "vpn",
        list: ["shared"],
        proto: "udp",
      }),
    ]
    const refs = buildListUsageByRouteRules(rules)
    const sharedRefs = refs.get("shared") ?? []
    const summary = formatRoutingListRefsUsageSummary(sharedRefs, rules)

    expect(summary).toContain("#1 → wan")
    expect(summary).toContain("dest_addr: 10.0.0.0/24")
    expect(summary).toContain("#2 → vpn")
    expect(summary).toContain("proto: udp")
    expect(summary).toContain(" • ")
  })

  test("describeRouteRuleRefForListUsage falls back to index and outbound without rule payload", () => {
    expect(
      describeRouteRuleRefForListUsage(
        { ruleIndex: 2, outbound: "x" },
        undefined,
      ),
    ).toBe("#3 → x")
  })
})
