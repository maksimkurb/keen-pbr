import type { DnsTestInterceptEvent } from "@/api/generated/model"
import { formatProcessingTime } from "@/lib/processing-time"
import { timeoutBadge } from "@/lib/timeout-cause"

export type Translate = (
  key: string,
  options?: Record<string, unknown>
) => string

export type RequestFlagKey =
  | "added"
  | "refreshed"
  | "errors"
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

export function methodBadge(source: RequestMethod, t: Translate) {
  return {
    label: t(`requestsLog.methods.${source}`),
    tooltip: t(`requestsLog.methodTooltips.${source}`),
    ...methodBadgeStyles[source],
  }
}
