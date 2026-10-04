import type { DnsTestInterceptEvent } from "@/api/generated/model"

export type TimeoutBadgeTone = "danger" | "neutral"

export type TimeoutBadge = {
  /** i18n key suffix under `requestsLog.timeout`. */
  cause: "budget_spent_by_batch" | "admission_blocked" | "own_write_slow" | "late_batch_full" | "other"
  tone: TimeoutBadgeTone
}

type TimeoutEvent = Pick<
  DnsTestInterceptEvent,
  "timed_out" | "timeout_cause" | "added" | "errors"
>

/**
 * Maps a timed-out hold to a badge. The tone is `danger` when the event also
 * created new set elements or had errors (the first connection may have been
 * routed before the element existed); refresh-only timeouts are harmless.
 * Returns null when the hold did not time out.
 */
export function timeoutBadge(event: TimeoutEvent): TimeoutBadge | null {
  if (!event.timed_out) return null
  const cause: TimeoutBadge["cause"] =
    event.timeout_cause === "budget_spent_by_batch" ||
    event.timeout_cause === "admission_blocked" ||
    event.timeout_cause === "own_write_slow" ||
    event.timeout_cause === "late_batch_full"
      ? event.timeout_cause
      : "other"
  const risky = event.added > 0 || event.errors > 0
  return { cause, tone: risky ? "danger" : "neutral" }
}

type TimingEvent = Pick<
  DnsTestInterceptEvent,
  | "batch_pos"
  | "batch_size"
  | "queue_wait_us"
  | "budget_left_us"
  | "admission_wait_us"
  | "set_write_us"
  | "write_elements"
  | "late_batch_elements"
  | "write_errno"
>

/** Compact timing breakdown lines for the processing-time tooltip. */
export function timingBreakdown(event: TimingEvent): string[] {
  if (event.batch_pos === undefined) return []
  const lines: string[] = [
    event.batch_size !== undefined
      ? `batch ${event.batch_pos + 1}/${event.batch_size}`
      : `batch pos ${event.batch_pos}`,
  ]
  if (event.queue_wait_us !== undefined) lines.push(`queue wait ${event.queue_wait_us} µs`)
  if (event.budget_left_us !== undefined) lines.push(`budget left ${event.budget_left_us} µs`)
  if (event.admission_wait_us) lines.push(`admission wait ${event.admission_wait_us} µs`)
  if (event.set_write_us !== undefined) {
    const elements = [
      event.write_elements ? `${event.write_elements} elements` : null,
      event.late_batch_elements ? `late batch ${event.late_batch_elements}` : null,
    ].filter((value): value is string => value !== null)
    lines.push(
      `write ${event.set_write_us} µs${elements.length > 0 ? ` (${elements.join(", ")})` : ""}`
    )
  }
  if (event.write_errno) lines.push(`errno ${event.write_errno}`)
  return lines
}
