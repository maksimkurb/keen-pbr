import { describe, expect, test } from "bun:test"

import { timeoutBadge, timingBreakdown } from "../src/lib/timeout-cause"

describe("timeoutBadge", () => {
  test("no badge without a timeout", () => {
    expect(
      timeoutBadge({ timed_out: false, timeout_cause: undefined, added: 1, errors: 0 })
    ).toBeNull()
  })

  test("maps every cause", () => {
    for (const cause of [
      "budget_spent_by_batch",
      "admission_blocked",
      "own_write_slow",
      "late_batch_full",
      "other",
    ] as const) {
      expect(
        timeoutBadge({ timed_out: true, timeout_cause: cause, added: 0, errors: 0 })?.cause
      ).toBe(cause)
    }
  })

  test("missing cause falls back to other", () => {
    expect(
      timeoutBadge({ timed_out: true, timeout_cause: undefined, added: 0, errors: 0 })
    ).toEqual({ cause: "other", tone: "neutral" })
  })

  test("danger when new elements or errors, neutral for refresh-only", () => {
    const base = { timed_out: true, timeout_cause: "own_write_slow" } as const
    expect(timeoutBadge({ ...base, added: 1, errors: 0 })?.tone).toBe("danger")
    expect(timeoutBadge({ ...base, added: 0, errors: 2 })?.tone).toBe("danger")
    expect(timeoutBadge({ ...base, added: 0, errors: 0 })?.tone).toBe("neutral")
  })
})

describe("timingBreakdown", () => {
  test("empty for events without timing", () => {
    expect(timingBreakdown({})).toEqual([])
  })

  test("full breakdown", () => {
    expect(
      timingBreakdown({
        batch_pos: 2,
        batch_size: 7,
        queue_wait_us: 58210,
        budget_left_us: -28210,
        admission_wait_us: 5,
        set_write_us: 120,
        write_elements: 2,
        late_batch_elements: 9,
        write_errno: 110,
      })
    ).toEqual([
      "batch 3/7",
      "queue wait 58210 µs",
      "budget left -28210 µs",
      "admission wait 5 µs",
      "write 120 µs (2 elements, late batch 9)",
      "errno 110",
    ])
  })

  test("batch size may be unknown", () => {
    expect(timingBreakdown({ batch_pos: 0 })).toEqual(["batch pos 0"])
  })
})
