import { describe, expect, test } from "bun:test"

import {
  findCrossListUsage,
  findExactDuplicates,
  findRedundantSubdomains,
  isCoveredBy,
} from "../src/pages/list-duplicates-utils"

import type { ListConfig } from "../src/api/generated/model/listConfig"

// ---------------------------------------------------------------------------
// isCoveredBy
// ---------------------------------------------------------------------------
describe("isCoveredBy", () => {
  test("identical domains are covered by each other", () => {
    expect(isCoveredBy("te.ru", "te.ru")).toBe(true)
  })

  test("subdomain is covered by parent", () => {
    expect(isCoveredBy("test.te.ru", "te.ru")).toBe(true)
  })

  test("deep subdomain is covered by parent", () => {
    expect(isCoveredBy("a.b.example.com", "example.com")).toBe(true)
  })

  test("parent is NOT covered by subdomain", () => {
    expect(isCoveredBy("te.ru", "test.te.ru")).toBe(false)
  })

  test("unrelated domain is not covered", () => {
    expect(isCoveredBy("other.com", "example.com")).toBe(false)
  })

  test("partial suffix match without dot boundary is rejected", () => {
    // "notexample.com" should NOT be covered by "example.com"
    expect(isCoveredBy("notexample.com", "example.com")).toBe(false)
  })
})

// ---------------------------------------------------------------------------
// findExactDuplicates
// ---------------------------------------------------------------------------
describe("findExactDuplicates", () => {
  test("returns empty array when no duplicates", () => {
    expect(findExactDuplicates(["a.com", "b.com", "c.com"])).toEqual([])
  })

  test("detects an exact duplicate", () => {
    const result = findExactDuplicates(["te.ru", "example.com", "te.ru"])
    expect(result).toContain("te.ru")
    expect(result.length).toBe(1)
  })

  test("is case-insensitive", () => {
    const result = findExactDuplicates(["Example.COM", "example.com"])
    expect(result).toContain("example.com")
  })

  test("only reports each duplicate once even if it appears three times", () => {
    const result = findExactDuplicates(["x.ru", "x.ru", "x.ru"])
    expect(result.length).toBe(1)
    expect(result[0]).toBe("x.ru")
  })

  test("does not report entries that appear exactly once", () => {
    expect(findExactDuplicates(["a.com"])).toEqual([])
  })
})

// ---------------------------------------------------------------------------
// findRedundantSubdomains
// ---------------------------------------------------------------------------
describe("findRedundantSubdomains", () => {
  test("te.ru covers test.te.ru — test.te.ru is redundant", () => {
    const result = findRedundantSubdomains(["te.ru", "test.te.ru"])
    expect(result.length).toBe(1)
    expect(result[0]?.redundant).toBe("test.te.ru")
    expect(result[0]?.coveredBy).toBe("te.ru")
  })

  test("returns empty when no redundancy", () => {
    expect(
      findRedundantSubdomains(["example.com", "other.org"])
    ).toEqual([])
  })

  test("handles multiple redundant subdomains under same parent", () => {
    const result = findRedundantSubdomains([
      "te.ru",
      "sub1.te.ru",
      "sub2.te.ru",
      "unrelated.com",
    ])
    const redundants = result.map((r) => r.redundant)
    expect(redundants).toContain("sub1.te.ru")
    expect(redundants).toContain("sub2.te.ru")
    expect(redundants).not.toContain("te.ru")
    expect(redundants).not.toContain("unrelated.com")
  })

  test("exact duplicates produce redundancy entries too", () => {
    // When "a.com" appears twice, the second occurrence is covered by the first
    const result = findRedundantSubdomains(["a.com", "a.com"])
    // Both entries are identical — each is "covered by" the other;
    // our impl skips equal indices but finds the other occurrence
    expect(result.length).toBeGreaterThan(0)
  })
})

// ---------------------------------------------------------------------------
// findCrossListUsage
// ---------------------------------------------------------------------------
describe("findCrossListUsage", () => {
  function list(partial: Partial<ListConfig>): ListConfig {
    return partial
  }

  test("detects exact domain match in another list", () => {
    const allLists: Record<string, ListConfig> = {
      current: list({ domains: ["example.com"] }),
      other: list({ domains: ["example.com"] }),
    }
    const result = findCrossListUsage("current", ["example.com"], allLists)
    expect(result.length).toBe(1)
    expect(result[0]?.otherList).toBe("other")
    expect(result[0]?.entry).toBe("example.com")
  })

  test("does not report the current list as cross-list usage", () => {
    const allLists: Record<string, ListConfig> = {
      current: list({ domains: ["te.ru", "sub.te.ru"] }),
    }
    const result = findCrossListUsage("current", ["te.ru"], allLists)
    expect(result).toEqual([])
  })

  test("detects parent domain coverage from another list", () => {
    const allLists: Record<string, ListConfig> = {
      current: list({ domains: ["sub.te.ru"] }),
      parent_list: list({ domains: ["te.ru"] }),
    }
    const result = findCrossListUsage("current", ["sub.te.ru"], allLists)
    expect(result.length).toBe(1)
    expect(result[0]?.otherList).toBe("parent_list")
  })

  test("detects exact IP/CIDR match in another list", () => {
    const allLists: Record<string, ListConfig> = {
      current: list({ ip_cidrs: ["10.0.0.0/8"] }),
      other: list({ ip_cidrs: ["10.0.0.0/8"] }),
    }
    const result = findCrossListUsage("current", ["10.0.0.0/8"], allLists)
    expect(result.length).toBe(1)
    expect(result[0]?.otherList).toBe("other")
  })

  test("returns empty when no cross-list usage", () => {
    const allLists: Record<string, ListConfig> = {
      current: list({ domains: ["only.here"] }),
      other: list({ domains: ["totally.different.com"] }),
    }
    const result = findCrossListUsage("current", ["only.here"], allLists)
    expect(result).toEqual([])
  })
})
