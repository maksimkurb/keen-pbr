import { useCallback, useEffect, useMemo, useRef, useState } from "react"
import { useTranslation } from "react-i18next"

import {
  getDnsEventHub,
  type DnsTestGapEvent,
  type DnsTestStreamEvent,
} from "@/api/dns-event-hub"
import type { DnsTestInterceptEvent } from "@/api/generated/model"
import { Badge } from "@/components/ui/badge"
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table"
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip"
import { PageHeader } from "@/components/shared/page-header"
import {
  formatProcessingTime,
  processingTimeToneClass,
} from "@/lib/processing-time"
import { cn } from "@/lib/utils"

const maxRows = 2_000
const rowHeight = 32
const defaultViewportHeight = 560

type RequestRow = (DnsTestInterceptEvent | DnsTestGapEvent) & {
  uiKey: string
}

export function CurrentRequestsPage() {
  const { t } = useTranslation()
  const [rows, setRows] = useState<RequestRow[]>([])
  const [copyStatus, setCopyStatus] = useState<"idle" | "copied" | "failed">("idle")
  const [scrollTop, setScrollTop] = useState(0)
  const [viewportHeight, setViewportHeight] = useState(defaultViewportHeight)
  const viewportRef = useRef<HTMLDivElement>(null)
  const rowsRef = useRef<RequestRow[]>([])
  const pendingRef = useRef<DnsTestStreamEvent[]>([])
  const frameRef = useRef<number | null>(null)
  const nextRowIdRef = useRef(0)

  const flush = useCallback(() => {
    frameRef.current = null
    if (pendingRef.current.length === 0) return
    const pending = pendingRef.current.splice(0)
    const next = pending
      .filter(
        (event): event is Exclude<DnsTestStreamEvent, { type: "HELLO" }> =>
          event.type !== "HELLO"
      )
      .map((event) => ({
        ...event,
        uiKey: `request-${nextRowIdRef.current++}`,
      }))
      .reverse()
      .concat(rowsRef.current)
      .slice(0, maxRows)
    rowsRef.current = next
    setRows(next)
  }, [])

  useEffect(() => {
    const hub = getDnsEventHub("full")
    const release = hub.acquireLease()
    const unsubscribe = hub.subscribeStream((event) => {
      pendingRef.current.push(event)
      if (pendingRef.current.length > maxRows) {
        pendingRef.current.splice(0, pendingRef.current.length - maxRows)
      }
      if (frameRef.current === null) {
        frameRef.current = requestAnimationFrame(flush)
      }
    })

    return () => {
      unsubscribe()
      release()
      if (frameRef.current !== null) cancelAnimationFrame(frameRef.current)
      frameRef.current = null
      pendingRef.current = []
      nextRowIdRef.current = 0
    }
  }, [flush])

  useEffect(() => {
    const element = viewportRef.current
    if (!element) return
    const update = () => setViewportHeight(element.clientHeight)
    update()
    const observer = new ResizeObserver(update)
    observer.observe(element)
    return () => observer.disconnect()
  }, [])

  const visible = useMemo(() => {
    const start = Math.max(0, Math.floor(scrollTop / rowHeight) - 5)
    const count = Math.ceil(viewportHeight / rowHeight) + 10
    return { start, end: Math.min(rows.length, start + count) }
  }, [rows.length, scrollTop, viewportHeight])

  const copyIps = useCallback(async (ips: string[]) => {
    const text = ips.join(", ")
    try {
      if (navigator.clipboard?.writeText) {
        await navigator.clipboard.writeText(text)
        setCopyStatus("copied")
        return
      }
    } catch {
      // Fall through to the legacy copy API on router HTTP origins.
    }
    const textarea = document.createElement("textarea")
    textarea.value = text
    textarea.setAttribute("readonly", "")
    textarea.style.position = "fixed"
    textarea.style.opacity = "0"
    document.body.appendChild(textarea)
    textarea.select()
    try {
      setCopyStatus(document.execCommand("copy") ? "copied" : "failed")
    } catch {
      setCopyStatus("failed")
    } finally {
      document.body.removeChild(textarea)
    }
  }, [])

  return (
    <div className="flex min-h-0 flex-1 flex-col gap-3">
      <PageHeader
        className="mb-0 md:mb-0"
        title={t("nav.items.currentRequests")}
      />
      {copyStatus !== "idle" ? (
        <div
          aria-live="polite"
          className={
            copyStatus === "copied"
              ? "text-xs text-emerald-600"
              : "text-xs text-destructive"
          }
        >
          {copyStatus === "copied"
            ? t("common.copied")
            : t("common.clipboardUnavailable")}
        </div>
      ) : null}
      <div
        className="min-h-0 flex-1 overflow-auto rounded-md border"
        onScroll={(event) => setScrollTop(event.currentTarget.scrollTop)}
        ref={viewportRef}
      >
        <Table
          className="w-full min-w-max text-xs whitespace-nowrap"
          containerClassName="overflow-visible"
        >
          {/* Rows are virtualized, so content-sized columns would jump while
              scrolling; header minimums keep them stable. */}
          <TableHeader className="sticky top-0 z-10 bg-muted/95 backdrop-blur">
            <TableRow className="h-8 hover:bg-transparent">
              <TableHead className="min-w-36 px-2 py-1 text-xs">
                {t("currentRequests.columns.device")}
              </TableHead>
              <TableHead className="min-w-20 px-2 py-1 text-xs">
                {t("currentRequests.columns.method")}
              </TableHead>
              <TableHead className="min-w-64 px-2 py-1 text-xs">
                {t("currentRequests.columns.domain")}
              </TableHead>
              <TableHead className="min-w-32 px-2 py-1 text-xs">
                {t("currentRequests.columns.lists")}
              </TableHead>
              <TableHead className="min-w-40 px-2 py-1 text-xs">
                {t("currentRequests.columns.ip")}
              </TableHead>
              <TableHead className="min-w-24 px-2 py-1 text-xs">
                {t("currentRequests.columns.processingTime")}
              </TableHead>
              <TableHead className="min-w-48 px-2 py-1 text-xs">
                {t("currentRequests.columns.flags")}
              </TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            <tr aria-hidden className="pointer-events-none border-0">
              <td colSpan={7} style={{ height: visible.start * rowHeight }} />
            </tr>
            {rows.slice(visible.start, visible.end).map((row, index) => (
              <RequestTableRow
                copyIps={copyIps}
                index={visible.start + index}
                key={row.uiKey}
                row={row}
                t={t}
              />
            ))}
            <tr aria-hidden className="pointer-events-none border-0">
              <td
                colSpan={7}
                style={{ height: Math.max(0, rows.length - visible.end) * rowHeight }}
              />
            </tr>
          </TableBody>
        </Table>
        {rows.length === 0 ? (
          <div className="px-3 py-8 text-center text-sm text-muted-foreground">
            {t("currentRequests.empty")}
          </div>
        ) : null}
      </div>
    </div>
  )
}

function RequestTableRow({
  row,
  index,
  copyIps,
  t,
}: {
  row: RequestRow
  index: number
  copyIps: (ips: string[]) => Promise<void>
  t: (key: string, options?: Record<string, unknown>) => string
}) {
  if (row.type === "GAP") {
    return (
      <TableRow className="h-8 bg-amber-500/10 hover:bg-amber-500/10">
        <TableCell className="px-2 py-1 font-medium" colSpan={7}>
          {t("currentRequests.gap", {
            from: row.from_seq,
            to: row.to_seq,
          })}
        </TableCell>
      </TableRow>
    )
  }

  const method = {
    dns: t("currentRequests.methods.dns"),
    http: t("currentRequests.methods.http"),
    sni: t("currentRequests.methods.sni"),
    quic: t("currentRequests.methods.quic"),
    marker: t("currentRequests.methods.marker"),
  }[row.source]
  const ips = row.ips.join(", ")
  const processingUs =
    row.hold_us > 0 ? row.hold_us : (row.parse_us ?? 0) + (row.set_write_us ?? 0)
  const processing = formatProcessingTime(processingUs)
  const processingDetail = [
    row.hold_us > 0 ? `hold ${row.hold_us} µs` : null,
    row.parse_us !== undefined ? `parse ${row.parse_us} µs` : null,
    row.set_write_us !== undefined ? `set ${row.set_write_us} µs` : null,
  ]
    .filter((value): value is string => value !== null)
    .join(", ")
  const flags = [
    row.added > 0 ? `+${row.added}` : null,
    row.refreshed > 0 ? `↻${row.refreshed}` : null,
    row.errors > 0 ? `!${row.errors}` : null,
    row.timed_out ? "timeout" : null,
    row.parse_us !== undefined ? `parse:${row.parse_us}µs` : null,
    row.set_write_us !== undefined ? `set:${row.set_write_us}µs` : null,
    row.seq > 0 ? `#${row.seq}` : null,
    row.ts_ms > 0 ? `@${new Date(row.ts_ms).toISOString().slice(11, 23)}` : null,
  ].filter((flag): flag is string => flag !== null)

  return (
    <TableRow className="h-8" data-row-index={index}>
      <TableCell className="border-r px-2 py-1 font-mono" title={row.client_ip ?? ""}>
        {row.client_ip || "—"}
      </TableCell>
      <TableCell className="border-r px-2 py-1" title={method}>
        {method}
      </TableCell>
      <TableCell className="border-r px-2 py-1 font-mono" title={row.domain}>
        {row.domain || "—"}
      </TableCell>
      <TableCell className="border-r px-2 py-1" title={row.lists.join(", ")}>
        {row.lists.join(", ") || "—"}
      </TableCell>
      <TableCell className="border-r px-2 py-1">
        {ips ? (
          <Tooltip>
            <TooltipTrigger
              render={
                <button
                  aria-label={t("currentRequests.copyIps", { value: ips })}
                  className="block max-w-56 truncate text-left font-mono underline decoration-dotted underline-offset-2"
                  onClick={() => void copyIps(row.ips)}
                  type="button"
                />
              }
            >
              {ips}
            </TooltipTrigger>
            <TooltipContent>{ips}</TooltipContent>
          </Tooltip>
        ) : (
          "—"
        )}
      </TableCell>
      <TableCell className={cn(
          "border-r px-2 py-1 font-mono",
          processingTimeToneClass(processing.tone)
        )}
        title={processingDetail}
      >
        {processing.text}
      </TableCell>
      <TableCell className="px-2 py-1">
        <Tooltip>
          <TooltipTrigger
            className="block"
            render={<div className="flex w-max gap-1" />}
          >
            {flags.length > 0 ? (
              flags.map((flag) => (
                <Badge
                  key={flag}
                  size="xs"
                  variant={
                    flag.startsWith("!") || flag === "timeout"
                      ? "destructive"
                      : "outline"
                  }
                >
                  {flag}
                </Badge>
              ))
            ) : (
              <span className="text-muted-foreground">—</span>
            )}
          </TooltipTrigger>
          <TooltipContent>{flags.join(", ") || "—"}</TooltipContent>
        </Tooltip>
      </TableCell>
    </TableRow>
  )
}
