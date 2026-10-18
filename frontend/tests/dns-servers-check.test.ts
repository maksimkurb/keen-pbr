import { describe, expect, test } from "bun:test"

import type { DnsCheckResponse } from "@/api/generated/model/dnsCheckResponse"
import type { DnsCheckResult } from "@/api/generated/model/dnsCheckResult"
import type { DnsCheckServer } from "@/api/generated/model/dnsCheckServer"
import {
  buildCheckRequest,
  CHECK_STORAGE_KEY,
  defaultSelection,
  isValidDomain,
  loadSelection,
  saveSelection,
  selectionProblem,
  serverLevel,
  summarizeCheck,
} from "@/pages/dns-servers-check-utils"

function result(partial: Partial<DnsCheckResult>): DnsCheckResult {
  return {
    domain: "yandex.ru",
    status: "ok",
    severity: "ok",
    addresses: [],
    duration_ms: 10,
    ...partial,
  }
}

function server(
  tag: string,
  results: DnsCheckResult[],
  extra: Partial<DnsCheckServer["endpoints"][number]> = {}
): DnsCheckServer {
  return {
    tag,
    type: "static",
    endpoints: [{ address: "8.8.8.8", status: "ok", results, ...extra }],
  }
}

describe("check selection", () => {
  test("starts with yandex.ru and telegram.org", () => {
    expect(defaultSelection().presets).toEqual(["yandex.ru", "telegram.org"])
    expect(buildCheckRequest(defaultSelection())).toEqual({
      domains: ["yandex.ru", "telegram.org"],
    })
  })

  test("adds a custom domain, lowercased, unless it is a checked preset", () => {
    expect(
      buildCheckRequest({ presets: ["yandex.ru"], custom: " Example.ORG " })
    ).toEqual({ domains: ["yandex.ru"], custom_domains: ["example.org"] })
    expect(
      buildCheckRequest({ presets: ["yandex.ru"], custom: "yandex.ru" })
    ).toEqual({ domains: ["yandex.ru"] })
  })

  test("orders presets as the menu does", () => {
    expect(
      buildCheckRequest({ presets: ["chatgpt.com", "yandex.ru"], custom: "" })
        .domains
    ).toEqual(["yandex.ru", "chatgpt.com"])
  })

  test("tells what is wrong with a choice", () => {
    expect(selectionProblem(defaultSelection())).toBeNull()
    expect(selectionProblem({ presets: [], custom: "" })).toBe("empty")
    expect(selectionProblem({ presets: [], custom: "example.org" })).toBeNull()
    expect(selectionProblem({ presets: [], custom: "not a domain" })).toBe(
      "invalidCustom"
    )
    expect(
      selectionProblem({
        presets: [
          "yandex.ru",
          "telegram.org",
          "youtube.com",
          "google.com",
          "instagram.com",
        ],
        custom: "example.org",
      })
    ).toBe("tooMany")
  })

  test("accepts host names only", () => {
    expect(isValidDomain("xn--80ak6aa92e.com")).toBe(true)
    expect(isValidDomain("a-b.example")).toBe(true)
    expect(isValidDomain("")).toBe(false)
    expect(isValidDomain("a..b")).toBe(false)
    expect(isValidDomain("-a.com")).toBe(false)
    expect(isValidDomain("a_b.com")).toBe(false)
    expect(isValidDomain("http://a.com")).toBe(false)
  })
})

describe("remembering the choice", () => {
  test("round-trips through storage and ignores what it does not know", () => {
    const data = new Map<string, string>()
    const storage = {
      getItem: (key: string) => data.get(key) ?? null,
      setItem: (key: string, value: string) => void data.set(key, value),
    }
    saveSelection(
      { presets: ["google.com", "bogus.example"], custom: "a.b" },
      storage
    )
    expect(loadSelection(storage)).toEqual({
      presets: ["google.com"],
      custom: "a.b",
    })
  })

  test("falls back to the default when storage is empty, broken or throws", () => {
    expect(loadSelection({ getItem: () => null })).toEqual(defaultSelection())
    expect(loadSelection({ getItem: () => "{not json" })).toEqual(
      defaultSelection()
    )
    expect(loadSelection({ getItem: () => '{"presets":"x"}' })).toEqual(
      defaultSelection()
    )
    expect(
      loadSelection({
        getItem: () => {
          throw new Error("blocked")
        },
      })
    ).toEqual(defaultSelection())
    expect(() =>
      saveSelection(defaultSelection(), {
        setItem: () => {
          throw new Error("quota")
        },
      })
    ).not.toThrow()
    expect(CHECK_STORAGE_KEY.length).toBeGreaterThan(0)
  })
})

describe("summarizeCheck", () => {
  test("is ok when every server answers sensibly", () => {
    const response: DnsCheckResponse = {
      overall: "ok",
      servers: [server("a", [result({})])],
    }
    const summary = summarizeCheck(response)
    expect(summary.level).toBe("ok")
    expect(summary.errors).toEqual([])
    expect(summary.warnings).toEqual([])
    expect(serverLevel(response.servers[0])).toBe("ok")
  })

  test("a server that answers nothing is one error, not one per domain", () => {
    const dead = result({
      status: "timeout",
      severity: "error",
      error: "no answer",
    })
    const response: DnsCheckResponse = {
      overall: "error",
      servers: [server("a", [dead, { ...dead, domain: "youtube.com" }])],
    }
    const summary = summarizeCheck(response)
    expect(summary.level).toBe("error")
    expect(summary.errors).toHaveLength(1)
    expect(summary.errors[0]).toMatchObject({ tag: "a", status: "timeout" })
    expect(serverLevel(response.servers[0])).toBe("error")
  })

  test("lists tampering as warnings with the domain", () => {
    const response: DnsCheckResponse = {
      overall: "warning",
      servers: [
        server("a", [
          result({}),
          result({
            domain: "youtube.com",
            status: "nxdomain",
            severity: "warning",
            rcode: 3,
          }),
          result({
            domain: "telegram.org",
            status: "stub_address",
            severity: "warning",
            rcode: 0,
            addresses: ["0.0.0.0"],
          }),
        ]),
      ],
    }
    const summary = summarizeCheck(response)
    expect(summary.level).toBe("warning")
    expect(summary.errors).toEqual([])
    expect(summary.warnings.map((item) => [item.status, item.domain])).toEqual([
      ["nxdomain", "youtube.com"],
      ["stub_address", "telegram.org"],
    ])
    expect(serverLevel(response.servers[0])).toBe("warning")
  })

  test("a site reported missing at several addresses of a server is told once", () => {
    const missing = result({
      domain: "telegram.org",
      status: "nxdomain",
      severity: "warning",
      rcode: 3,
    })
    const quad9: DnsCheckServer = {
      tag: "quad9",
      type: "static",
      endpoints: [
        { address: "9.9.9.9", status: "ok", results: [missing] },
        { address: "149.112.112.112", status: "ok", results: [missing] },
      ],
    }
    const summary = summarizeCheck({ overall: "warning", servers: [quad9] })
    expect(summary.level).toBe("warning")
    expect(summary.warnings).toHaveLength(1)
    expect(summary.warnings[0]).toMatchObject({
      tag: "quad9",
      status: "nxdomain",
      domain: "telegram.org",
    })
  })

  test("errors win over warnings; answers with an error code are per domain", () => {
    const response: DnsCheckResponse = {
      overall: "error",
      servers: [
        server("a", [
          result({}),
          result({
            domain: "youtube.com",
            status: "servfail",
            severity: "error",
            rcode: 2,
          }),
        ]),
        server("b", [
          result({ status: "nxdomain", severity: "warning", rcode: 3 }),
        ]),
      ],
    }
    const summary = summarizeCheck(response)
    expect(summary.level).toBe("error")
    expect(summary.errors.map((item) => [item.tag, item.domain])).toEqual([
      ["a", "youtube.com"],
    ])
    expect(summary.warnings.map((item) => item.tag)).toEqual(["b"])
  })

  test("unavailable endpoints and server errors are errors, skipped ones are neutral", () => {
    const response: DnsCheckResponse = {
      overall: "error",
      servers: [
        server("doh", [], {
          status: "unavailable",
          reason_code: "doh_no_certificates",
          reason: "no CA certificates",
        }),
        {
          tag: "k",
          type: "keenetic",
          error: "RCI is down",
          error_code: "keenetic_dns_unavailable",
          endpoints: [],
        },
        server("v6", [], { status: "skipped", reason_code: "ipv6_disabled" }),
      ],
    }
    const summary = summarizeCheck(response)
    expect(summary.errors.map((item) => [item.tag, item.status])).toEqual([
      ["doh", "doh_no_certificates"],
      ["k", "keenetic_dns_unavailable"],
    ])
    expect(serverLevel(response.servers[0])).toBe("error")
    expect(serverLevel(response.servers[1])).toBe("error")
    expect(serverLevel(response.servers[2])).toBe("skipped")
  })
})
