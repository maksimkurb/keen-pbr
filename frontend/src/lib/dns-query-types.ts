// RFC 1035 DNS query types
export const dnsQueryTypes: Record<number, string> = {
  1: "A",
  2: "NS",
  5: "CNAME",
  6: "SOA",
  12: "PTR",
  15: "MX",
  16: "TXT",
  28: "AAAA",
  33: "SRV",
  64: "SVCB",
  65: "HTTPS",
}

export type Translate = (
  key: string,
  options?: Record<string, unknown>
) => string

export function queryTypeToString(qtype: number): string {
  return dnsQueryTypes[qtype] || `TYPE${qtype}`
}

export function noAddressReasonKey(
  rcode: number,
  qtype: number
): { key: string; options?: Record<string, unknown> } | null {
  switch (rcode) {
    case 3:
      return { key: "requestsLog.dnsReasons.nxdomain" }
    case 2:
      return { key: "requestsLog.dnsReasons.servfail" }
    case 5:
      return { key: "requestsLog.dnsReasons.refused" }
    case 0:
      // NODATA case
      const typeStr = queryTypeToString(qtype)
      if (qtype === 1 || qtype === 28) {
        // A or AAAA: show "no A" or "no AAAA"
        return { key: "requestsLog.dnsReasons.nodata", options: { type: typeStr } }
      }
      // Other types: show "<TYPE> record"
      return { key: "requestsLog.dnsReasons.nodataOther", options: { type: typeStr } }
    default:
      if (rcode !== 0) {
        return { key: "requestsLog.dnsReasons.rcode", options: { code: rcode } }
      }
  }
  return null
}

export function noAddressReason(
  rcode: number,
  qtype: number,
  t: Translate
): string | null {
  const reasonKey = noAddressReasonKey(rcode, qtype)
  if (!reasonKey) return null
  return t(reasonKey.key, reasonKey.options)
}

export function noAddressTooltipKey(
  rcode: number,
  qtype: number
): { key: string; options?: Record<string, unknown> } | null {
  const typeStr = queryTypeToString(qtype)

  switch (rcode) {
    case 3:
      return { key: "requestsLog.dnsTooltips.nxdomain" }
    case 2:
      return { key: "requestsLog.dnsTooltips.servfail" }
    case 5:
      return { key: "requestsLog.dnsTooltips.refused" }
    case 0:
      if (qtype === 1 || qtype === 28) {
        return {
          key: "requestsLog.dnsTooltips.nodata",
          options: { type: typeStr },
        }
      }
      return {
        key: "requestsLog.dnsTooltips.nodataOther",
        options: { type: typeStr },
      }
    default:
      if (rcode !== 0) {
        return {
          key: "requestsLog.dnsTooltips.rcode",
          options: { code: rcode },
        }
      }
  }
  return null
}

export function noAddressTooltip(
  rcode: number,
  qtype: number,
  t: Translate
): string {
  const tooltipKey = noAddressTooltipKey(rcode, qtype)
  if (!tooltipKey) return ""
  return t(tooltipKey.key, tooltipKey.options)
}
