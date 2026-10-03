import { describe, expect, test } from "bun:test"

import {
  normalizeDnsMarkerDomain,
  parseDnsCheckEvent,
} from "../src/hooks/use-dns-check"

describe("DNS interception SSE events", () => {
  test("parses the current INTERCEPT payload fields", () => {
    expect(
      parseDnsCheckEvent(
        JSON.stringify({
          type: "INTERCEPT",
          seq: 42,
          ts_ms: 1712345678123,
          source: "dns",
          domain: "example.com",
          lists: ["streaming"],
          ips: ["203.0.113.7"],
          added: 1,
          refreshed: 2,
          errors: 0,
          hold_us: 180,
          timed_out: false,
        })
      )
    ).toMatchObject({
      type: "INTERCEPT",
      source: "dns",
      domain: "example.com",
      lists: ["streaming"],
      ips: ["203.0.113.7"],
      hold_us: 180,
      timed_out: false,
    })
  })

  test("does not treat removed DNS probe events as interceptions", () => {
    expect(
      parseDnsCheckEvent(JSON.stringify({ type: "DNS", domain: "example.com" }))
    ).toBeNull()
  })

  test("rejects unknown interception sources", () => {
    expect(
      parseDnsCheckEvent(
        JSON.stringify({
          type: "INTERCEPT",
          source: "probe",
          domain: "example.com",
        })
      )
    ).toBeNull()
  })

  test("accepts L7 interception sources for the live monitor", () => {
    for (const source of ["sni", "http", "quic"] as const) {
      expect(
        parseDnsCheckEvent(
          JSON.stringify({
            type: "INTERCEPT",
            source,
            domain: "example.com",
          })
        )?.source
      ).toBe(source)
    }
  })

  test("normalizes configured marker domains for generated checks", () => {
    expect(normalizeDnsMarkerDomain(" Check.Keen.PBR... ")).toBe(
      "check.keen.pbr"
    )
  })
})
