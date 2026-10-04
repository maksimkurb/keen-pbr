export type ProcessingTimeTone = "normal" | "warning" | "danger"

export type FormattedProcessingTime = {
  text: string
  tone: ProcessingTimeTone
}

const usPerMs = 1_000
const usPerSecond = 1_000_000

// Truncates (never rounds up) so a value just below a unit boundary cannot
// display as the boundary itself, e.g. 999.9 ms must not become "1000 ms".
function truncate(value: number, decimals: number): string {
  const factor = 10 ** decimals
  return (Math.floor(value * factor + 1e-9) / factor).toFixed(decimals)
}

/**
 * Formats a duration given in microseconds with an automatic unit.
 *
 * - below 1000 µs: integer "N µs"
 * - 1000 µs up to 1 s: "N ms" (1 decimal below 10 ms, integer above), warning
 * - 1 s and above: "N s" (2 decimals below 10 s, 1 below 100 s, integer above), danger
 * - missing or invalid input: "—"
 */
export function formatProcessingTime(
  valueUs: number | null | undefined
): FormattedProcessingTime {
  if (
    valueUs === null ||
    valueUs === undefined ||
    !Number.isFinite(valueUs) ||
    valueUs < 0
  ) {
    return { text: "—", tone: "normal" }
  }
  if (valueUs < usPerMs) {
    return { text: `${Math.floor(valueUs)} µs`, tone: "normal" }
  }
  if (valueUs < usPerSecond) {
    const ms = valueUs / usPerMs
    return {
      text: `${truncate(ms, ms < 10 ? 1 : 0)} ms`,
      tone: "warning",
    }
  }
  const s = valueUs / usPerSecond
  return {
    text: `${truncate(s, s < 10 ? 2 : s < 100 ? 1 : 0)} s`,
    tone: "danger",
  }
}

export function processingTimeToneClass(tone: ProcessingTimeTone): string {
  if (tone === "warning") return "text-amber-600 dark:text-amber-400"
  if (tone === "danger") return "text-destructive"
  return ""
}
