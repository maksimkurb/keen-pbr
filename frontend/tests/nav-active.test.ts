import { describe, expect, test } from "bun:test"

import { matchesNavHref } from "../src/lib/nav-active"

describe("matchesNavHref", () => {
  test("root matches only exactly", () => {
    expect(matchesNavHref("/", "/")).toBe(true)
    expect(matchesNavHref("", "/")).toBe(true)
    expect(matchesNavHref("/lists", "/")).toBe(false)
    expect(matchesNavHref("/lists/create", "/")).toBe(false)
  })

  test("section matches self and children", () => {
    expect(matchesNavHref("/lists", "/lists")).toBe(true)
    expect(matchesNavHref("/lists/create", "/lists")).toBe(true)
    expect(matchesNavHref("/lists/foo/edit", "/lists")).toBe(true)
    expect(matchesNavHref("/routing-rules", "/lists")).toBe(false)
  })

  test("does not match sibling prefixes", () => {
    expect(matchesNavHref("/lists-backup", "/lists")).toBe(false)
  })
})
