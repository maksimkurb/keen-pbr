import { describe, expect, test } from "bun:test"

import {
  allRequestMethods,
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
    expect(filterRequests(rows, emptyRequestFilters)).toBe(rows)
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
  test("invalid IP filter is ignored", () => {
    expect(
      filterRequests(rows, { ...emptyRequestFilters, ip: "10.0.0.0/99" }).length
    ).toBe(3)
  })
})
