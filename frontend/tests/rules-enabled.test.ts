import { describe, expect, test } from "bun:test"

import type { ConfigObject } from "../src/api/generated/model/configObject"
import {
  emptyRouteRuleDraft,
  getDragInsertionIndex,
  getReorderTargetIndex,
  normalizeRouteRuleDraft,
  reorderRules,
  setRouteRuleEnabled,
  toRouteRuleDraft,
} from "../src/pages/routing-rules-utils"
import { type DnsRuleDraft } from "../src/pages/dns-rules-utils"

let dnsRuleUtilsPromise:
  | Promise<typeof import("../src/pages/dns-rules-utils")>
  | undefined

function loadDnsRuleUtils() {
  if (!globalThis.navigator) {
    Object.defineProperty(globalThis, "navigator", {
      configurable: true,
      value: { language: "en-US", languages: ["en-US"] },
    })
  } else {
    Object.defineProperty(globalThis.navigator, "language", {
      configurable: true,
      value: "en-US",
    })
    Object.defineProperty(globalThis.navigator, "languages", {
      configurable: true,
      value: ["en-US"],
    })
  }

  dnsRuleUtilsPromise ??= import("../src/pages/dns-rules-utils")
  return dnsRuleUtilsPromise
}

describe("routing rule enabled helpers", () => {
  test("new route rule drafts default to enabled", () => {
    expect(emptyRouteRuleDraft.enabled).toBe(true)
    expect(normalizeRouteRuleDraft(emptyRouteRuleDraft).enabled).toBe(true)
  })

  test("route rule draft preserves disabled state and defaults missing state to enabled", () => {
    expect(
      toRouteRuleDraft({
        enabled: false,
        list: ["ads"],
        dscp: 46,
        outbound: "vpn",
      }).enabled
    ).toBe(false)

    expect(
      toRouteRuleDraft({
        list: ["ads"],
        outbound: "vpn",
      }).enabled
    ).toBe(true)
  })

  test("route rule draft preserves dscp", () => {
    const draft = toRouteRuleDraft({
      list: ["ads"],
      dscp: 46,
      outbound: "vpn",
    })

    expect(draft.dscp).toBe("46")
    expect(normalizeRouteRuleDraft(draft).dscp).toBe(46)
  })

  test("setRouteRuleEnabled updates only the targeted rule", () => {
    const rules = [
      { list: ["one"], outbound: "vpn" },
      { enabled: false, list: ["two"], outbound: "wan" },
    ]

    expect(setRouteRuleEnabled(rules, 1, true)).toEqual([
      { list: ["one"], outbound: "vpn" },
      { enabled: true, list: ["two"], outbound: "wan" },
    ])
  })
})

describe("routing rule reordering helpers", () => {
  const rules = [
    { outbound: "one" },
    { outbound: "two" },
    { outbound: "three" },
    { outbound: "four" },
  ]

  test("converts a later insertion gap after removing the dragged rule", () => {
    const targetIndex = getReorderTargetIndex(0, 3)

    expect(targetIndex).toBe(2)
    expect(reorderRules(rules, 0, targetIndex)).toEqual([
      { outbound: "two" },
      { outbound: "three" },
      { outbound: "one" },
      { outbound: "four" },
    ])
  })

  test("keeps an earlier insertion gap unchanged", () => {
    const targetIndex = getReorderTargetIndex(3, 1)

    expect(targetIndex).toBe(1)
    expect(reorderRules(rules, 3, targetIndex)).toEqual([
      { outbound: "one" },
      { outbound: "four" },
      { outbound: "two" },
      { outbound: "three" },
    ])
  })

  test("recognizes either adjacent gap as the current position", () => {
    expect(getReorderTargetIndex(2, 2)).toBe(2)
    expect(getReorderTargetIndex(2, 3)).toBe(2)
  })

  test("moves the second rule to the penultimate and final positions", () => {
    const penultimateInsertion = getDragInsertionIndex(1, rules.length - 2)
    const finalInsertion = getDragInsertionIndex(1, rules.length - 1)

    expect(penultimateInsertion).toBe(rules.length - 1)
    expect(finalInsertion).toBe(rules.length)

    const penultimateIndex = getReorderTargetIndex(1, penultimateInsertion!)
    const finalIndex = getReorderTargetIndex(1, finalInsertion!)

    expect(reorderRules(rules, 1, penultimateIndex)).toEqual([
      { outbound: "one" },
      { outbound: "three" },
      { outbound: "two" },
      { outbound: "four" },
    ])
    expect(reorderRules(rules, 1, finalIndex)).toEqual([
      { outbound: "one" },
      { outbound: "three" },
      { outbound: "four" },
      { outbound: "two" },
    ])
  })

  test("uses the whole hovered row as a directional drop target", () => {
    expect(getDragInsertionIndex(2, 0)).toBe(0)
    expect(getDragInsertionIndex(2, 1)).toBe(1)
    expect(getDragInsertionIndex(2, 2)).toBeNull()
    expect(getDragInsertionIndex(2, 3)).toBe(4)
  })
})

describe("dns rule enabled helpers", () => {
  test("dns rule drafts default to enabled and preserve disabled state", async () => {
    const { getRuleDraft } = await loadDnsRuleUtils()

    expect(getRuleDraft().enabled).toBe(true)
    expect(
      getRuleDraft({
        enabled: false,
        list: ["ads"],
        server: "vpn_dns",
      }).enabled
    ).toBe(false)
  })

  test("buildUpdatedConfigWithRules persists enabled into config payload", async () => {
    const { buildUpdatedConfigWithRules } = await loadDnsRuleUtils()

    const config: ConfigObject = {
      dns: {
        fallback: ["vpn_dns"],
        rules: [],
      },
    }

    expect(
      buildUpdatedConfigWithRules(
        config,
        ["vpn_dns"],
        [
          {
            enabled: false,
            server: "vpn_dns",
            lists: ["ads"],
            allowDomainRebinding: true,
          },
        ]
      )
    ).toEqual({
      dns: {
        fallback: ["vpn_dns"],
        rules: [
          {
            enabled: false,
            server: "vpn_dns",
            list: ["ads"],
            allow_domain_rebinding: true,
          },
        ],
      },
    })
  })

  test("setDnsRuleEnabled updates only the targeted draft rule", async () => {
    const { setDnsRuleEnabled } = await loadDnsRuleUtils()

    const rules: DnsRuleDraft[] = [
      {
        enabled: true,
        server: "vpn_dns",
        lists: ["ads"],
        allowDomainRebinding: false,
      },
      {
        enabled: false,
        server: "wan_dns",
        lists: ["work"],
        allowDomainRebinding: true,
      },
    ]

    expect(setDnsRuleEnabled(rules, 0, false)).toEqual([
      {
        enabled: false,
        server: "vpn_dns",
        lists: ["ads"],
        allowDomainRebinding: false,
      },
      {
        enabled: false,
        server: "wan_dns",
        lists: ["work"],
        allowDomainRebinding: true,
      },
    ])
  })

  test("validateRules ignores disabled rules", async () => {
    const { validateRules } = await loadDnsRuleUtils()

    expect(
      validateRules(
        [
          {
            enabled: false,
            server: "",
            lists: [],
            allowDomainRebinding: false,
          },
          {
            enabled: true,
            server: "vpn_dns",
            lists: ["ads"],
            allowDomainRebinding: false,
          },
        ],
        ["vpn_dns"],
        ["ads"]
      )
    ).toEqual({})
  })
})
