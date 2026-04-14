import { describe, expect, test } from "bun:test"

import { getDnsmasqBadgeState } from "../src/components/overview/dnsmasq-status"

describe("getDnsmasqBadgeState", () => {
  test("maps healthy and converged to healthy badge", () => {
    expect(getDnsmasqBadgeState("healthy", "converged")).toEqual({
      labelKey: "overview.runtime.dnsmasqHealthy",
      tone: "healthy",
    })
  })

  test("maps healthy and converging to waiting badge", () => {
    expect(getDnsmasqBadgeState("healthy", "converging")).toEqual({
      labelKey: "overview.runtime.dnsmasqWaiting",
      tone: "warning",
    })
  })

  test("maps healthy and stale to restart-required badge", () => {
    expect(getDnsmasqBadgeState("healthy", "stale")).toEqual({
      labelKey: "overview.runtime.dnsmasqStale",
      tone: "warning",
    })
  })

  test("never reports healthy when live status is unavailable", () => {
    expect(getDnsmasqBadgeState("unavailable", "converged")).toEqual({
      labelKey: "overview.runtime.dnsmasqUnavailable",
      tone: "degraded",
    })
  })

  test("returns unknown badge when live status is missing", () => {
    expect(getDnsmasqBadgeState(undefined, undefined)).toEqual({
      labelKey: "overview.runtime.dnsmasqUnknown",
      tone: "warning",
    })
  })
})
