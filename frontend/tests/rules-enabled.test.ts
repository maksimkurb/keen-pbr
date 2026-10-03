import { describe, expect, test } from "bun:test"

import {
  emptyRouteRuleDraft,
  getDragInsertionIndex,
  getReorderTargetIndex,
  normalizeRouteRuleDraft,
  reorderRules,
  setRouteRuleEnabled,
  toRouteRuleDraft,
} from "../src/pages/routing-rules-utils"

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

  test("route rule draft serializes default gateway mode without conditions", () => {
    const draft = toRouteRuleDraft({
      default_gateway: "ipv6",
      list: ["ignored"],
      proto: "tcp",
      outbound: "vpn",
    })

    expect(draft.mode).toBe("ipv6")
    expect(normalizeRouteRuleDraft(draft)).toEqual({
      enabled: true,
      outbound: "vpn",
      default_gateway: "ipv6",
    })
  })

  test("normal route rule mode omits default gateway", () => {
    expect(normalizeRouteRuleDraft(emptyRouteRuleDraft)).not.toHaveProperty(
      "default_gateway"
    )
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
