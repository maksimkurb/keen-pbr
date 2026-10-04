import { describe, expect, test } from "bun:test"

import {
  createDnsEventHub,
  dnsEventStreamUrl,
} from "../src/api/dns-event-hub"
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
          client_ip: "192.168.1.10",
          domain: "example.com",
          lists: ["streaming"],
          ips: ["203.0.113.7"],
          added: 1,
          refreshed: 2,
          errors: 0,
          hold_us: 180,
          timed_out: false,
          parse_us: 12,
          set_write_us: 95,
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
      client_ip: "192.168.1.10",
      parse_us: 12,
      set_write_us: 95,
    })
  })

  test("preserves GAP and client/timing metadata for the full stream", () => {
    expect(
      parseDnsCheckEvent(
        JSON.stringify({ type: "GAP", from_seq: 3, to_seq: 7 })
      )
    ).toEqual({ type: "GAP", from_seq: 3, to_seq: 7 })
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

  test("accepts L7 interception sources for the full stream", () => {
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

  test("builds isolated marker stream URLs", () => {
    expect(dnsEventStreamUrl("keen-pbr", " Foo.Check.Keen.PBR... ")).toBe(
      "/api/dns/test?show=keen-pbr&domain=foo.check.keen.pbr"
    )
    expect(dnsEventStreamUrl("full")).toBe("/api/dns/test?show=full")
  })

  test("disconnectNow aborts a completed check stream immediately", () => {
    let aborted = false
    const hub = createDnsEventHub({
      consume: async (_url, signal, _onMessage, onOpen) => {
        onOpen?.()
        await new Promise<void>((resolve) => {
          signal.addEventListener(
            "abort",
            () => {
              aborted = true
              resolve()
            },
            { once: true }
          )
        })
      },
    })
    const unsubscribe = hub.subscribe(() => {})
    unsubscribe()
    hub.disconnectNow()
    expect(aborted).toBe(true)
    expect(hub.getStatus()).toBe("idle")
  })
})
