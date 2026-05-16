import { describe, expect, test } from "bun:test"

import type { RoutingTestRuleDiagnostic } from "../src/api/generated/model/routingTestRuleDiagnostic"
import {
  getRuleConditions,
  getVisibleRuleDiagnostics,
  isGrayRuleDiagnostic,
} from "../src/components/overview/routing-diagnostics-utils"

describe("routing diagnostics helpers", () => {
  test("hides fully gray rules by default", () => {
    const rules = [
      buildRuleDiagnostic(0, { inIpset: false }),
      buildRuleDiagnostic(1, { inIpset: null }),
    ]

    expect(getVisibleRuleDiagnostics(rules, false)).toEqual([])
  })

  test("keeps rule with target match", () => {
    const rule = buildRuleDiagnostic(0, {
      targetMatch: { list: "work", via: "example.com" },
    })

    expect(isGrayRuleDiagnostic(rule)).toBe(false)
    expect(getVisibleRuleDiagnostics([rule], false)).toEqual([rule])
  })

  test("keeps rule with stale ipset membership", () => {
    const rule = buildRuleDiagnostic(0, { inIpset: true })

    expect(isGrayRuleDiagnostic(rule)).toBe(false)
    expect(getVisibleRuleDiagnostics([rule], false)).toEqual([rule])
  })

  test("showAllRules keeps every rule", () => {
    const rules = [
      buildRuleDiagnostic(0, { inIpset: false }),
      buildRuleDiagnostic(1, { inIpset: true }),
    ]

    expect(getVisibleRuleDiagnostics(rules, true)).toEqual(rules)
  })

  test("formats only present rule conditions", () => {
    expect(
      getRuleConditions({
        list: ["work", "media"],
        outbound: "vpn",
        proto: "tcp",
        dest_port: "443",
      })
    ).toEqual([
      { key: "lists", value: "work, media" },
      { key: "proto", value: "tcp" },
      { key: "destinationPort", value: "443" },
    ])
  })
})

function buildRuleDiagnostic(
  ruleIndex: number,
  options: {
    inIpset?: boolean | null
    targetMatch?: RoutingTestRuleDiagnostic["target_match"]
  } = {}
): RoutingTestRuleDiagnostic {
  return {
    rule_index: ruleIndex,
    rule: {
      list: ["work"],
      outbound: "vpn",
    },
    outbound: "vpn",
    interface_name: "ppp0",
    target_in_lists: Boolean(options.targetMatch),
    target_match: options.targetMatch,
    ip_rows: [
      {
        ip: "8.8.8.8",
        in_ipset: options.inIpset,
      },
    ],
  }
}
