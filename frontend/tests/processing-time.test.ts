import { describe, expect, test } from "bun:test"

import { formatProcessingTime } from "../src/lib/processing-time"

describe("formatProcessingTime", () => {
  test("microseconds below 1000", () => {
    expect(formatProcessingTime(0)).toEqual({ text: "0 µs", tone: "normal" })
    expect(formatProcessingTime(999)).toEqual({
      text: "999 µs",
      tone: "normal",
    })
  })

  test("exactly 1000 µs switches to yellow ms", () => {
    expect(formatProcessingTime(1000)).toEqual({
      text: "1.0 ms",
      tone: "warning",
    })
    expect(formatProcessingTime(1250)).toEqual({
      text: "1.2 ms",
      tone: "warning",
    })
    expect(formatProcessingTime(12_000)).toEqual({
      text: "12 ms",
      tone: "warning",
    })
  })

  test("just below 1 s stays in ms", () => {
    expect(formatProcessingTime(999_900)).toEqual({
      text: "999 ms",
      tone: "warning",
    })
    expect(formatProcessingTime(999_999)).toEqual({
      text: "999 ms",
      tone: "warning",
    })
  })

  test("1 s and above is red seconds", () => {
    expect(formatProcessingTime(1_000_000)).toEqual({
      text: "1.00 s",
      tone: "danger",
    })
    expect(formatProcessingTime(1_250_000)).toEqual({
      text: "1.25 s",
      tone: "danger",
    })
    expect(formatProcessingTime(12_340_000)).toEqual({
      text: "12.3 s",
      tone: "danger",
    })
    expect(formatProcessingTime(250_000_000)).toEqual({
      text: "250 s",
      tone: "danger",
    })
  })

  test("missing or invalid values", () => {
    expect(formatProcessingTime(undefined)).toEqual({
      text: "—",
      tone: "normal",
    })
    expect(formatProcessingTime(null)).toEqual({ text: "—", tone: "normal" })
    expect(formatProcessingTime(Number.NaN)).toEqual({
      text: "—",
      tone: "normal",
    })
    expect(formatProcessingTime(-5)).toEqual({ text: "—", tone: "normal" })
  })
})
