import { describe, expect, test } from "bun:test"

import { isConfigMutationPending } from "../src/api/mutations"

describe("isConfigMutationPending", () => {
  test("returns false when no config mutations are in flight", () => {
    expect(isConfigMutationPending(0, 0)).toBe(false)
  })

  test("returns true when draft save is in flight", () => {
    expect(isConfigMutationPending(1, 0)).toBe(true)
  })

  test("returns true when apply save is in flight", () => {
    expect(isConfigMutationPending(0, 2)).toBe(true)
  })
})
