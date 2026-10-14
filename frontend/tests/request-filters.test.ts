import { describe, expect, test } from "bun:test"

import {
  allAnswerKinds,
  allRequestMethods,
  answerKind,
  hasActiveFilters,
  isUnlearnableIp,
  emptyRequestFilters,
  filterRequests,
  ipMatches,
  parseIpFilter,
  wildcardMatch,
} from "../src/lib/request-filters"

const ip = (filter: string, value: string) =>
  ipMatches(parseIpFilter(filter), value)

describe("wildcardMatch", () => {
  test("star and question mark", () => {
    expect(wildcardMatch("*.example.com", "a.b.example.com")).toBe(true)
    expect(wildcardMatch("*.example.com", "example.com")).toBe(false)
    expect(wildcardMatch("a?c", "abc")).toBe(true)
    expect(wildcardMatch("a?c", "ac")).toBe(false)
  })
  test("substring without wildcards, case-insensitive", () => {
    expect(wildcardMatch("EXAMPLE", "www.example.org")).toBe(true)
    expect(wildcardMatch("nope", "www.example.org")).toBe(false)
    expect(wildcardMatch("", "anything")).toBe(true)
  })
  test("regex metacharacters are literal", () => {
    expect(wildcardMatch("a.c", "abc")).toBe(false)
    expect(wildcardMatch("a.c", "a.c")).toBe(true)
    expect(wildcardMatch("a+*", "a+b")).toBe(true)
    expect(wildcardMatch("a+*", "aab")).toBe(false)
    expect(wildcardMatch("(x)*", "(x)y")).toBe(true)
    expect(wildcardMatch("(x", "a(x")).toBe(true)
  })
})

describe("IP filter", () => {
  test("IPv4 CIDR", () => {
    expect(ip("0.0.0.0/0", "8.8.8.8")).toBe(true)
    expect(ip("10.0.0.0/8", "10.200.1.1")).toBe(true)
    expect(ip("10.0.0.0/8", "11.0.0.1")).toBe(false)
    expect(ip("1.2.3.4/32", "1.2.3.4")).toBe(true)
    expect(ip("1.2.3.4/32", "1.2.3.5")).toBe(false)
    expect(ip("10.1.2.3/8", "10.9.9.9")).toBe(true)
  })
  test("IPv6 CIDR", () => {
    expect(ip("2001:db8::/32", "2001:db8:1::5")).toBe(true)
    expect(ip("2001:db8::/32", "2001:db9::1")).toBe(false)
    expect(ip("::1/128", "::1")).toBe(true)
    expect(ip("::1/128", "::2")).toBe(false)
    expect(ip("::/0", "fe80::1")).toBe(true)
    expect(ip("2001:db8::/32", "2001:0db8:0:0:0:0:0:1")).toBe(true)
  })
  test("versions never cross-match", () => {
    expect(ip("0.0.0.0/0", "::1")).toBe(false)
    expect(ip("::/0", "1.2.3.4")).toBe(false)
  })
  test("invalid CIDR", () => {
    expect(parseIpFilter("10.0.0.0/33").kind).toBe("invalid")
    expect(parseIpFilter("garbage/8").kind).toBe("invalid")
    expect(parseIpFilter("10.0.0.0/").kind).toBe("invalid")
    expect(parseIpFilter("::1/129").kind).toBe("invalid")
    expect(parseIpFilter("1:2:3::4::5/64").kind).toBe("invalid")
    expect(ip("10.0.0.0/33", "1.1.1.1")).toBe(true)
  })
  test("wildcard and exact", () => {
    expect(ip("10.0.*", "10.0.5.5")).toBe(true)
    expect(ip("10.0.*", "10.1.5.5")).toBe(false)
    expect(ip("*::1", "2001:db8::1")).toBe(true)
    expect(ip("192.168.1.1", "192.168.1.1")).toBe(true)
  })
})

describe("filterRequests", () => {
  const rows = [
    {
      type: "DNS",
      client_ip: "192.168.1.10",
      source: "dns",
      domain: "a.example.com",
      ips: ["10.0.0.1"],
    },
    {
      type: "DNS",
      client_ip: "192.168.1.20",
      source: "sni",
      domain: "b.test.org",
      ips: ["2001:db8::1", "1.1.1.1"],
    },
    { type: "GAP" },
  ]
  test("no filters returns all", () => {
    expect(filterRequests(rows, emptyRequestFilters)).toEqual(rows)
  })
  test("methods multi-select", () => {
    const out = filterRequests(rows, {
      ...emptyRequestFilters,
      methods: ["sni", "quic"],
    })
    expect(out.map((r) => r.client_ip)).toEqual(["192.168.1.20", undefined])
    expect(
      filterRequests(rows, { ...emptyRequestFilters, methods: [] }).length
    ).toBe(1)
    expect(allRequestMethods.length).toBe(5)
  })
  test("combined AND, gap rows kept", () => {
    const out = filterRequests(rows, {
      ...emptyRequestFilters,
      device: "1.20",
      domain: "*.org",
      ip: "2001:db8::/32",
    })
    expect(out.length).toBe(2)
    expect(out[0].domain).toBe("b.test.org")
    expect(
      filterRequests(rows, {
        ...emptyRequestFilters,
        device: "1.10",
        domain: "*.org",
      }).length
    ).toBe(1)
  })
  test("device accepts a CIDR subnet", () => {
    const devices = (device: string) =>
      filterRequests(rows, { ...emptyRequestFilters, device }).map(
        (r) => r.client_ip
      )
    expect(devices("192.168.1.16/28")).toEqual(["192.168.1.20", undefined])
    expect(devices("192.168.1.0/24")).toEqual([
      "192.168.1.10",
      "192.168.1.20",
      undefined,
    ])
    expect(devices("10.0.0.0/8")).toEqual([undefined])
    expect(devices("192.168.1.0/33").length).toBe(3)
  })
  test("invalid IP filter is ignored", () => {
    expect(
      filterRequests(rows, { ...emptyRequestFilters, ip: "10.0.0.0/99" }).length
    ).toBe(3)
  })
})

describe("isUnlearnableIp", () => {
  test("matches dns_wire::is_learnable_address", () => {
    for (const ip of ["0.0.0.0", "127.0.0.1", "127.255.0.9", "::", "::1"]) {
      expect(isUnlearnableIp(ip)).toBe(true)
    }
    for (const ip of ["1.1.1.1", "0.0.0.1", "128.0.0.1", "::2", "fe80::1"]) {
      expect(isUnlearnableIp(ip)).toBe(false)
    }
    expect(isUnlearnableIp("garbage")).toBe(false)
  })
})

describe("answer kinds and actions", () => {
  const dns = (extra: Record<string, unknown>) => ({
    type: "DNS",
    source: "dns",
    ips: [] as string[],
    lists: [] as string[],
    added: 0,
    refreshed: 0,
    errors: 0,
    ...extra,
  })
  const rows = [
    dns({ domain: "routed", ips: ["1.2.3.4"], lists: ["vpn"], added: 1 }),
    dns({
      domain: "refreshed",
      ips: ["1.2.3.5"],
      lists: ["vpn"],
      refreshed: 1,
    }),
    dns({ domain: "plain", ips: ["1.2.3.6"] }),
    dns({ domain: "blocked", ips: ["0.0.0.0", "::"] }),
    dns({ domain: "mixed", ips: ["0.0.0.0", "5.5.5.5"] }),
    dns({ domain: "empty" }),
    dns({ domain: "nx", rcode: 3 }),
    dns({ domain: "fail", rcode: 2 }),
    dns({ domain: "late", ips: ["1.2.3.7"], timed_out: true }),
    { type: "DNS", source: "sni", domain: "l7", ips: [] as string[] },
  ]
  const domains = (filters: Partial<typeof emptyRequestFilters>) =>
    filterRequests(rows, { ...emptyRequestFilters, ...filters }).map(
      (r) => r.domain
    )

  test("classifies DNS replies", () => {
    expect(rows.map(answerKind)).toEqual([
      "addresses",
      "addresses",
      "addresses",
      "blocked",
      "addresses",
      "empty",
      "nxdomain",
      "error",
      "addresses",
      null,
    ])
  })
  test("empty answers start hidden; L7 events ignore answer kinds", () => {
    expect(domains({})).not.toContain("empty")
    expect(domains({ answers: allAnswerKinds })).toContain("empty")
    expect(domains({ answers: [] })).toEqual(["l7"])
    expect(domains({ answers: ["blocked"] })).toEqual(["blocked", "l7"])
  })
  test("actions show rows with any checked action", () => {
    expect(domains({})).toContain("plain")
    expect(
      domains({ actions: ["listed", "added", "refreshed", "problem"] })
    ).toEqual(["routed", "refreshed", "late"])
    expect(domains({ actions: ["listed"] })).toEqual(["routed", "refreshed"])
    expect(domains({ actions: ["refreshed", "problem"] })).toEqual([
      "refreshed",
      "late",
    ])
    expect(domains({ actions: ["none"] })).not.toContain("routed")
    expect(domains({ actions: ["none"] })).toContain("plain")
    expect(domains({ actions: [] })).toEqual([])
  })
  test("active filters", () => {
    expect(hasActiveFilters(emptyRequestFilters)).toBe(false)
    expect(
      hasActiveFilters({ ...emptyRequestFilters, actions: ["listed"] })
    ).toBe(true)
    expect(
      hasActiveFilters({ ...emptyRequestFilters, answers: allAnswerKinds })
    ).toBe(true)
  })
})
