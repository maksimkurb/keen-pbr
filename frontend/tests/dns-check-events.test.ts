import { describe, expect, test } from "bun:test"

import { parseDnsCheckEvent } from "../src/lib/dns-check-events"

describe("parseDnsCheckEvent", () => {
  test("parses HELLO events", () => {
    expect(parseDnsCheckEvent('{"type":"HELLO"}')).toEqual({ type: "HELLO" })
  })

  test("parses DNS events with domain metadata", () => {
    expect(
      parseDnsCheckEvent(
        '{"type":"DNS","domain":"abc.check.keen.pbr","source_ip":"192.168.1.1"}',
      ),
    ).toEqual({
      type: "DNS",
      domain: "abc.check.keen.pbr",
      source_ip: "192.168.1.1",
    })
  })

  test("rejects empty payloads", () => {
    expect(parseDnsCheckEvent("")).toBeNull()
    expect(parseDnsCheckEvent("   ")).toBeNull()
  })

  test("rejects malformed JSON", () => {
    expect(parseDnsCheckEvent("{not-json")).toBeNull()
  })

  test("rejects objects without type", () => {
    expect(parseDnsCheckEvent('{"domain":"x"}')).toBeNull()
  })
})
