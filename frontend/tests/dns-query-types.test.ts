import { describe, expect, it } from "bun:test"
import {
  noAddressReasonKey,
  noAddressTooltipKey,
  noAddressReason,
  noAddressTooltip,
  queryTypeToString,
} from "@/lib/dns-query-types"

const mockT = (key: string, options?: Record<string, unknown>) => {
  // Simple mock translator that combines key and substituted variables
  if (!options) {
    return key
  }
  let result = key
  for (const v of Object.values(options)) {
    result += ` ${v}`
  }
  return result
}

describe("queryTypeToString", () => {
  it("returns known query types", () => {
    expect(queryTypeToString(1)).toBe("A")
    expect(queryTypeToString(28)).toBe("AAAA")
    expect(queryTypeToString(5)).toBe("CNAME")
    expect(queryTypeToString(65)).toBe("HTTPS")
  })

  it("returns TYPE<n> for unknown types (RFC 3597)", () => {
    expect(queryTypeToString(99)).toBe("TYPE99")
    expect(queryTypeToString(999)).toBe("TYPE999")
  })
})

describe("noAddressReasonKey", () => {
  it("returns nxdomain key for rcode 3", () => {
    const result = noAddressReasonKey(3, 1)
    expect(result?.key).toBe("requestsLog.dnsReasons.nxdomain")
  })

  it("returns servfail key for rcode 2", () => {
    const result = noAddressReasonKey(2, 1)
    expect(result?.key).toBe("requestsLog.dnsReasons.servfail")
  })

  it("returns refused key for rcode 5", () => {
    const result = noAddressReasonKey(5, 1)
    expect(result?.key).toBe("requestsLog.dnsReasons.refused")
  })

  it("returns rcode key for other non-zero rcodes", () => {
    const result = noAddressReasonKey(7, 1)
    expect(result?.key).toBe("requestsLog.dnsReasons.rcode")
    expect(result?.options?.code).toBe(7)
  })

  it("returns nodata key for rcode 0 with A/AAAA", () => {
    const resultA = noAddressReasonKey(0, 1)
    expect(resultA?.key).toBe("requestsLog.dnsReasons.nodata")
    expect(resultA?.options?.type).toBe("A")

    const resultAAAA = noAddressReasonKey(0, 28)
    expect(resultAAAA?.key).toBe("requestsLog.dnsReasons.nodata")
    expect(resultAAAA?.options?.type).toBe("AAAA")
  })

  it("returns nodataOther key for rcode 0 with other types", () => {
    const resultCNAME = noAddressReasonKey(0, 5)
    expect(resultCNAME?.key).toBe("requestsLog.dnsReasons.nodataOther")
    expect(resultCNAME?.options?.type).toBe("CNAME")

    const resultHTTPS = noAddressReasonKey(0, 65)
    expect(resultHTTPS?.key).toBe("requestsLog.dnsReasons.nodataOther")
    expect(resultHTTPS?.options?.type).toBe("HTTPS")
  })
})

describe("noAddressTooltipKey", () => {
  it("returns nxdomain tooltip key for rcode 3", () => {
    const result = noAddressTooltipKey(3, 1)
    expect(result?.key).toBe("requestsLog.dnsTooltips.nxdomain")
  })

  it("returns rcode tooltip key with code option for other errors", () => {
    const result = noAddressTooltipKey(10, 1)
    expect(result?.key).toBe("requestsLog.dnsTooltips.rcode")
    expect(result?.options?.code).toBe(10)
  })
})

describe("noAddressReason", () => {
  it("uses translation function to format reasons", () => {
    const reason = noAddressReason(3, 1, mockT)
    expect(reason).toBe("requestsLog.dnsReasons.nxdomain")

    const nodata = noAddressReason(0, 1, mockT)
    expect(nodata).toContain("requestsLog.dnsReasons.nodata")
    expect(nodata).toContain("A")
  })
})

describe("noAddressTooltip", () => {
  it("uses translation function to format tooltips", () => {
    const tooltip = noAddressTooltip(3, 1, mockT)
    expect(tooltip).toBe("requestsLog.dnsTooltips.nxdomain")

    const nodata = noAddressTooltip(0, 28, mockT)
    expect(nodata).toContain("requestsLog.dnsTooltips.nodata")
    expect(nodata).toContain("AAAA")
  })
})
