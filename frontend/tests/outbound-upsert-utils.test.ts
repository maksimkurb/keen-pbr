import { describe, expect, test } from "bun:test"

import { getIn } from "../src/lib/draft-form-core"

import type { Outbound } from "../src/api/generated/model/outbound"
import {
  type OutboundDraft,
  buildOutboundPayload,
  getKillSwitchChoice,
  getKillSwitchFields,
  getMemberSharePercent,
  isValidMemberWeight,
  mapOutboundToDraft,
  synchronizeOutboundGroups,
} from "../src/pages/outbound-upsert-utils"

const roundTrip = (outbound: Outbound) =>
  buildOutboundPayload(mapOutboundToDraft(outbound))

// Strips keys whose value is undefined so toEqual compares what is sent.
const sent = (outbound: Outbound) => JSON.parse(JSON.stringify(outbound))

describe("outbound draft round trip", () => {
  test("interface keeps gateways and kill-switch overrides", () => {
    const outbound: Outbound = {
      type: "interface",
      tag: "vpn",
      interface: "wg0",
      gateway: "auto",
      gateway6: "fe80::1",
      strict_enforcement: false,
      strict_enforcement_action: "blackhole",
    }
    expect(sent(roundTrip(outbound))).toEqual(outbound)
  })

  test("interface without overrides does not invent them", () => {
    const outbound: Outbound = {
      type: "interface",
      tag: "wan",
      interface: "eth1",
    }
    expect(sent(roundTrip(outbound))).toEqual(outbound)
  })

  test("table, blackhole and ignore", () => {
    for (const outbound of [
      { type: "table", tag: "t", table: 254 },
      { type: "blackhole", tag: "block" },
      { type: "ignore", tag: "direct" },
    ] satisfies Outbound[]) {
      expect(sent(roundTrip(outbound))).toEqual(outbound)
    }
  })

  test("urltest keeps every tuning field", () => {
    const outbound: Outbound = {
      type: "urltest",
      tag: "auto",
      url: "https://example.com/204",
      interval_ms: 90000,
      probe_timeout_ms: 3000,
      tolerance_ms: 50,
      strategy: "balance",
      conntrack_on_switch: "delete",
      outbound_groups: [
        { members: [{ outbound: "a", weight: 7 }, { outbound: "b" }] },
        { members: [{ outbound: "c" }] },
      ],
      retry: { attempts: 2, interval_ms: 500 },
      circuit_breaker: {
        failure_threshold: 4,
        success_threshold: 3,
        timeout_ms: 20000,
        half_open_max_requests: 2,
      },
    }
    expect(sent(roundTrip(outbound))).toEqual(outbound)
  })

  test("icmptest keeps candidates, targets and probe settings", () => {
    const outbound: Outbound = {
      type: "icmptest",
      tag: "ping",
      count: 5,
      max_failed: 1,
      packet_interval_ms: 300,
      probe_timeout_ms: 800,
      max_rtt_ms: 400,
      interval_ms: 70000,
      tolerance_ms: 15,
      strategy: "priority",
      conntrack_on_switch: "preserve",
      outbound_groups: [
        { members: [{ outbound: "a", target: "1.1.1.1", weight: 3 }] },
        { members: [{ outbound: "b", target: "2001:db8::1" }] },
      ],
      circuit_breaker: {
        failure_threshold: 5,
        success_threshold: 2,
        timeout_ms: 70000,
        half_open_max_requests: 1,
      },
    }
    expect(sent(roundTrip(outbound))).toEqual(outbound)
  })

  test("legacy weighted groups are saved as members in priority order", () => {
    const payload = roundTrip({
      type: "urltest",
      tag: "auto",
      url: "https://example.com/204",
      outbound_groups: [
        { weight: 10, outbounds: ["backup"] },
        { weight: 1, outbounds: ["primary"] },
        { outbounds: ["secondary"] },
      ],
    })
    // weight 1 and the implicit default 1 keep config order, weight 10 is last.
    expect(payload.outbound_groups).toEqual([
      { members: [{ outbound: "primary" }] },
      { members: [{ outbound: "secondary" }] },
      { members: [{ outbound: "backup" }] },
    ])
  })

  test("legacy icmptest candidates are saved as members", () => {
    const payload = roundTrip({
      type: "icmptest",
      tag: "ping",
      outbound_groups: [
        { weight: 2, candidates: [{ outbound: "b", target: "9.9.9.9" }] },
        { candidates: [{ outbound: "a", target: "1.1.1.1" }] },
      ],
    })
    expect(payload.outbound_groups).toEqual([
      { members: [{ outbound: "a", target: "1.1.1.1" }] },
      { members: [{ outbound: "b", target: "9.9.9.9" }] },
    ])
  })

  test("urltest never sends a target, empty weights are omitted", () => {
    const draft = mapOutboundToDraft({
      type: "urltest",
      tag: "auto",
      outbound_groups: [{ members: [{ outbound: "a", weight: 5 }] }],
    })
    draft.outbound_groups[0].members[0].target = "1.1.1.1"
    expect(sent(buildOutboundPayload(draft)).outbound_groups).toEqual([
      { members: [{ outbound: "a", weight: 5 }] },
    ])
  })

  test("synchronizeOutboundGroups keeps target and weight per outbound", () => {
    const draft = mapOutboundToDraft({
      type: "icmptest",
      tag: "ping",
      outbound_groups: [
        { members: [{ outbound: "a", target: "1.1.1.1", weight: 4 }] },
      ],
    })
    expect(
      synchronizeOutboundGroups(draft.outbound_groups, [["b"], ["a"]])
    ).toEqual([
      { members: [{ outbound: "b", target: "", weight: "" }] },
      { members: [{ outbound: "a", target: "1.1.1.1", weight: "4" }] },
    ])
  })
})

describe("kill-switch choice", () => {
  test("collapses the two config fields into one choice", () => {
    expect(getKillSwitchChoice("default", "default", "unreachable")).toBe(
      "inherit"
    )
    expect(getKillSwitchChoice("disabled", "blackhole", "unreachable")).toBe(
      "off"
    )
    expect(getKillSwitchChoice("enabled", "blackhole", "unreachable")).toBe(
      "drop"
    )
    // Enabled without its own action follows the global action.
    expect(getKillSwitchChoice("enabled", "default", "blackhole")).toBe("drop")
    expect(getKillSwitchChoice("enabled", "default", "unreachable")).toBe(
      "reject"
    )
  })

  test("each choice round-trips through the config fields", () => {
    for (const choice of ["inherit", "off", "reject", "drop"] as const) {
      const fields = getKillSwitchFields(choice)
      expect(
        getKillSwitchChoice(
          fields.strict_enforcement,
          fields.strict_enforcement_action,
          "unreachable"
        )
      ).toBe(choice)
    }
  })
})

describe("member weights", () => {
  const group = (weights: string[]) => ({
    members: weights.map((weight, index) => ({
      outbound: `o${index}`,
      target: "",
      weight,
    })),
  })

  test("validates the 1..100 range and allows empty", () => {
    for (const value of ["", " ", "1", "7", "100"]) {
      expect(isValidMemberWeight(value)).toBe(true)
    }
    for (const value of ["0", "101", "1.5", "-2", "abc"]) {
      expect(isValidMemberWeight(value)).toBe(false)
    }
  })

  test("shares follow the weights, empty and invalid count as 1", () => {
    expect(getMemberSharePercent(group(["7", "3"]), 0)).toBe(70)
    expect(getMemberSharePercent(group(["7", "3"]), 1)).toBe(30)
    expect(getMemberSharePercent(group(["", "", ""]), 0)).toBe(34)
    expect(getMemberSharePercent(group(["", "", ""]), 2)).toBe(33)
    // 7/1 = 87.5% / 12.5%: rounded shares still add up to 100.
    const sevenToOne = group(["7", "1"])
    expect(
      getMemberSharePercent(sevenToOne, 0) +
        getMemberSharePercent(sevenToOne, 1)
    ).toBe(100)
    expect(getMemberSharePercent(group(["3", "abc"]), 1)).toBe(25)
  })
})

describe("member payload", () => {
  const draft = (overrides: Partial<OutboundDraft>): OutboundDraft => ({
    ...mapOutboundToDraft({ type: "icmptest", tag: "ping" }),
    ...overrides,
  })
  const members = (
    ...items: Array<{ outbound: string; target?: string; weight?: string }>
  ) => [
    {
      members: items.map((item) => ({
        outbound: item.outbound,
        target: item.target ?? "",
        weight: item.weight ?? "",
      })),
    },
  ]

  test("values go to the daemon unvalidated; it reports errors by path", () => {
    const payload = buildOutboundPayload(
      draft({
        strategy: "balance",
        outbound_groups: members(
          { outbound: "a", target: "", weight: "0" },
          { outbound: "b", target: "1.1.1", weight: "250" },
          { outbound: "c", target: "8.8.8.8", weight: "" }
        ),
      })
    )
    expect(payload.outbound_groups).toEqual([
      {
        members: [
          { outbound: "a", target: "", weight: 0 },
          { outbound: "b", target: "1.1.1", weight: 250 },
          { outbound: "c", target: "8.8.8.8" },
        ],
      },
    ])
  })
})

describe("draft paths mirror the API", () => {
  test("every draft path is the path of that value in the Outbound", () => {
    const outbound: Outbound = {
      type: "icmptest",
      tag: "ping",
      count: 5,
      max_failed: 1,
      packet_interval_ms: 300,
      probe_timeout_ms: 800,
      max_rtt_ms: 400,
      interval_ms: 70000,
      tolerance_ms: 15,
      strategy: "balance",
      conntrack_on_switch: "delete",
      outbound_groups: [
        { members: [{ outbound: "a", target: "1.1.1.1", weight: 3 }] },
      ],
      retry: { attempts: 2, interval_ms: 500 },
      circuit_breaker: {
        failure_threshold: 4,
        success_threshold: 3,
        timeout_ms: 20000,
        half_open_max_requests: 2,
      },
    }
    const draft = mapOutboundToDraft(outbound)
    for (const path of [
      "count",
      "max_failed",
      "packet_interval_ms",
      "probe_timeout_ms",
      "max_rtt_ms",
      "interval_ms",
      "tolerance_ms",
      "retry.attempts",
      "retry.interval_ms",
      "circuit_breaker.failure_threshold",
      "circuit_breaker.success_threshold",
      "circuit_breaker.timeout_ms",
      "circuit_breaker.half_open_max_requests",
      "outbound_groups[0].members[0].target",
      "outbound_groups[0].members[0].weight",
    ]) {
      expect(String(getIn(draft, path))).toBe(String(getIn(outbound, path)))
    }
    expect(getIn(draft, "conntrack_on_switch")).toBe("delete")
    expect(getIn(draft, "strategy")).toBe("balance")
  })

  test("interface fields use the API names", () => {
    const draft = mapOutboundToDraft({
      type: "interface",
      tag: "wan",
      interface: "eth1",
      gateway: "auto",
      strict_enforcement: true,
      strict_enforcement_action: "blackhole",
    })
    expect(draft.interface).toBe("eth1")
    expect(draft.strict_enforcement).toBe("enabled")
    expect(draft.strict_enforcement_action).toBe("blackhole")
  })
})
