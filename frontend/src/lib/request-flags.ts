import type { DnsTestInterceptEvent } from "@/api/generated/model"
import { formatProcessingTime } from "@/lib/processing-time"
import { timeoutBadge } from "@/lib/timeout-cause"
import {
  queryTypeToString,
  noAddressReason,
  noAddressTooltip,
} from "@/lib/dns-query-types"

export type Translate = (
  key: string,
  options?: Record<string, unknown>
) => string

export type RequestFlagKey =
  | "added"
  | "refreshed"
  | "errors"
  | "not_learned"
  | "timeout"
  | "parse"
  | "set"
  | "seq"
  | "time"

export type RequestFlag = {
  key: RequestFlagKey
  label: string
  tooltip: string
  tone: "danger" | "neutral"
}

export type RequestFlagRow = Pick<
  DnsTestInterceptEvent,
  | "added"
  | "refreshed"
  | "errors"
  | "not_learned"
  | "seq"
  | "ts_ms"
  | "timed_out"
  | "timeout_cause"
  | "parse_us"
  | "set_write_us"
>

function pad(value: number, width = 2): string {
  return String(value).padStart(width, "0")
}

/** Local wall-clock time as HH:MM:SS.mmm. */
export function formatLocalTime(tsMs: number): string {
  const date = new Date(tsMs)
  return `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}.${pad(date.getMilliseconds(), 3)}`
}

/** Human duration from microseconds, with localized unit and decimal mark. */
function humanDuration(valueUs: number, t: Translate): string {
  const text = formatProcessingTime(valueUs).text
  const [number, unit] = text.split(" ")
  if (unit === undefined) return text
  const unitKey = unit === "µs" ? "us" : unit
  return `${number.replace(".", t("requestsLog.decimalSeparator"))} ${t(`requestsLog.units.${unitKey}`)}`
}

/**
 * Builds the flag badges of a request row. Each flag carries its own
 * explanatory tooltip with the value substituted.
 */
export function buildRequestFlags(
  row: RequestFlagRow,
  t: Translate,
  locale?: string
): RequestFlag[] {
  const flags: RequestFlag[] = []
  if (row.added > 0) {
    flags.push({
      key: "added",
      label: `+${row.added}`,
      tooltip: t("requestsLog.flags.added", { count: row.added }),
      tone: "neutral",
    })
  }
  if (row.refreshed > 0) {
    flags.push({
      key: "refreshed",
      label: `↻${row.refreshed}`,
      tooltip: t("requestsLog.flags.refreshed", { count: row.refreshed }),
      tone: "neutral",
    })
  }
  if (row.errors > 0) {
    flags.push({
      key: "errors",
      label: `!${row.errors}`,
      tooltip: t("requestsLog.flags.errors", { count: row.errors }),
      tone: "danger",
    })
  }
  if ((row.not_learned ?? 0) > 0) {
    flags.push({
      key: "not_learned",
      label: `⊘${row.not_learned}`,
      tooltip: `${t("requestsLog.flags.not_learned", { count: row.not_learned })} — ${t("requestsLog.flags.not_learned_tooltip")}`,
      tone: "neutral",
    })
  }
  const timeout = timeoutBadge(row)
  if (timeout) {
    flags.push({
      key: "timeout",
      label: t(`requestsLog.timeout.${timeout.cause}.label`),
      tooltip: t(`requestsLog.timeout.${timeout.cause}.tooltip`),
      tone: timeout.tone,
    })
  }
  if (row.parse_us !== undefined) {
    flags.push({
      key: "parse",
      label: `parse:${row.parse_us}µs`,
      tooltip: t("requestsLog.flags.parse", {
        value: humanDuration(row.parse_us, t),
      }),
      tone: "neutral",
    })
  }
  if (row.set_write_us !== undefined) {
    flags.push({
      key: "set",
      label: `set:${row.set_write_us}µs`,
      tooltip: t("requestsLog.flags.setWrite", {
        value: humanDuration(row.set_write_us, t),
      }),
      tone: "neutral",
    })
  }
  if (row.seq > 0) {
    flags.push({
      key: "seq",
      label: `#${row.seq}`,
      tooltip: t("requestsLog.flags.seq", { seq: row.seq }),
      tone: "neutral",
    })
  }
  if (row.ts_ms > 0) {
    const time = formatLocalTime(row.ts_ms)
    flags.push({
      key: "time",
      label: `@${time}`,
      tooltip: t("requestsLog.flags.time", {
        time,
        date: new Date(row.ts_ms).toLocaleString(locale),
      }),
      tone: "neutral",
    })
  }
  return flags
}

export type RequestMethod = "dns" | "http" | "sni" | "quic" | "marker"

export type MethodBadgeStyle = {
  variant: "secondary" | "outline"
  className: string
}

export const methodBadgeStyles: Record<RequestMethod, MethodBadgeStyle> = {
  dns: { variant: "secondary", className: "" },
  http: {
    variant: "secondary",
    className: "bg-sky-100 text-sky-800 dark:bg-sky-950 dark:text-sky-300",
  },
  sni: {
    variant: "secondary",
    className:
      "bg-emerald-100 text-emerald-800 dark:bg-emerald-950 dark:text-emerald-300",
  },
  quic: {
    variant: "secondary",
    className:
      "bg-violet-100 text-violet-800 dark:bg-violet-950 dark:text-violet-300",
  },
  marker: { variant: "outline", className: "" },
}

export type MethodBadge = {
  label: string
  tooltip: string
  variant: "secondary" | "outline"
  className: string
}

export function methodBadge(
  source: RequestMethod,
  t: Translate,
  event?: { qtype?: number }
): MethodBadge {
  // For DNS events, show the query type
  if (source === "dns" && event?.qtype && event.qtype > 0) {
    const typeStr = queryTypeToString(event.qtype)
    return {
      label: `DNS ${typeStr}`,
      tooltip: t(`requestsLog.methodTooltips.dns`),
      ...methodBadgeStyles[source],
    }
  }

  return {
    label: t(`requestsLog.methods.${source}`),
    tooltip: t(`requestsLog.methodTooltips.${source}`),
    ...methodBadgeStyles[source],
  }
}

export type IpColumnInfo = {
  text: string | null
  tooltip: string | null
  tone: "error" | "muted" | "normal"
  isClickable: boolean
}

export function getIpColumnInfo(
  event: { source?: string; ips?: string[]; qtype?: number; rcode?: number },
  t: Translate
): IpColumnInfo {
  // Non-DNS events or events with addresses
  const ips = event.ips ?? []
  if (event.source !== "dns" || ips.length > 0) {
    if (ips.length === 0) {
      return {
        text: "—",
        tooltip: null,
        tone: "normal",
        isClickable: false,
      }
    }
    return {
      text: ips.join(", "),
      tooltip: ips.join(", "),
      tone: "normal",
      isClickable: true,
    }
  }

  // DNS events with no addresses
  const qtype = event.qtype || 0
  const rcode = event.rcode || 0

  const reason = noAddressReason(rcode, qtype, t)
  if (!reason) {
    return {
      text: "—",
      tooltip: null,
      tone: "normal",
      isClickable: false,
    }
  }

  // Determine tone based on error type
  let tone: "error" | "muted" | "normal"
  if (rcode === 3) {
    // NXDOMAIN - muted
    tone = "muted"
  } else if (rcode === 2 || rcode === 5 || (rcode !== 0 && rcode !== 3)) {
    // SERVFAIL, REFUSED, or other errors - error tone (amber/warning)
    tone = "error"
  } else {
    // NODATA (rcode=0, no addresses) - muted
    tone = "muted"
  }

  const tooltip = noAddressTooltip(rcode, qtype, t)

  return {
    text: reason,
    tooltip,
    tone,
    isClickable: false,
  }
}
