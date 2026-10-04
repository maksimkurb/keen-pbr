import {
  Fragment,
  useCallback,
  useDeferredValue,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react"
import { useTranslation } from "react-i18next"

import {
  getDnsEventHub,
  type DnsTestGapEvent,
  type DnsTestStreamEvent,
} from "@/api/dns-event-hub"
import type { DnsTestInterceptEvent } from "@/api/generated/model"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Checkbox } from "@/components/ui/checkbox"
import { Input } from "@/components/ui/input"
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
import {
  allRequestMethods,
  emptyRequestFilters,
  filterRequests,
  hasActiveFilters,
  parseIpFilter,
  type RequestFilters,
} from "@/lib/request-filters"
import {
  buildRequestFlags,
  methodBadge,
  type RequestMethod,
} from "@/lib/request-flags"
import { timingBreakdown } from "@/lib/timeout-cause"
import { cn } from "@/lib/utils"

const maxRows = 2_000
const rowHeight = 32
const defaultViewportHeight = 560
const filtersStorageKey = "keen-pbr.requestsLog.filters"

function loadFilters(): RequestFilters {
  try {
    const raw = window.localStorage.getItem(filtersStorageKey)
    if (!raw) return emptyRequestFilters
    const data = JSON.parse(raw) as Partial<RequestFilters>
    return {
      device: typeof data.device === "string" ? data.device : "",
      domain: typeof data.domain === "string" ? data.domain : "",
      ip: typeof data.ip === "string" ? data.ip : "",
      methods: Array.isArray(data.methods)
        ? allRequestMethods.filter((m) => data.methods?.includes(m))
        : allRequestMethods,
    }
  } catch {
    return emptyRequestFilters
  }
}

type RequestRow = (DnsTestInterceptEvent | DnsTestGapEvent) & {
  uiKey: string
}

export function RequestsLogPage() {
  const { t, i18n } = useTranslation()
  const [rows, setRows] = useState<RequestRow[]>([])
  const [copyStatus, setCopyStatus] = useState<"idle" | "copied" | "failed">(
    "idle"
  )
  const [filters, setFilters] = useState<RequestFilters>(loadFilters)
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

  useEffect(() => {
    try {
      window.localStorage.setItem(filtersStorageKey, JSON.stringify(filters))
    } catch {
      // Persistence is a convenience only.
    }
  }, [filters])

  // Typing stays responsive; the (up to 2000 rows) filter pass runs deferred.
  const deferredFilters = useDeferredValue(filters)
  const filtering = hasActiveFilters(deferredFilters)
  const filteredRows = useMemo(
    () => filterRequests(rows, deferredFilters),
    [rows, deferredFilters]
  )
  const eventCount = (list: RequestRow[]) =>
    list.reduce((n, row) => (row.type === "GAP" ? n : n + 1), 0)
  const totalEvents = useMemo(() => eventCount(rows), [rows])
  const shownEvents = useMemo(() => eventCount(filteredRows), [filteredRows])
  const ipInvalid = parseIpFilter(filters.ip).kind === "invalid"

  // Virtualization runs over the filtered rows so scroll height matches.
  const visible = useMemo(() => {
    const start = Math.max(0, Math.floor(scrollTop / rowHeight) - 5)
    const count = Math.ceil(viewportHeight / rowHeight) + 10
    return { start, end: Math.min(filteredRows.length, start + count) }
  }, [filteredRows.length, scrollTop, viewportHeight])

  useEffect(() => {
    // Filters shrink the list; keep the scroll position inside it.
    const element = viewportRef.current
    if (element) setScrollTop(element.scrollTop)
  }, [filteredRows.length])

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
      <PageHeader className="mb-0 md:mb-0" title={t("nav.items.requestsLog")} />
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
      <RequestFilterBar
        filters={filters}
        ipInvalid={ipInvalid}
        onChange={setFilters}
        shown={shownEvents}
        t={t}
        total={totalEvents}
      />
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
                {t("requestsLog.columns.device")}
              </TableHead>
              <TableHead className="min-w-20 px-2 py-1 text-xs">
                {t("requestsLog.columns.method")}
              </TableHead>
              <TableHead className="min-w-64 px-2 py-1 text-xs">
                {t("requestsLog.columns.domain")}
              </TableHead>
              <TableHead className="min-w-32 px-2 py-1 text-xs">
                {t("requestsLog.columns.lists")}
              </TableHead>
              <TableHead className="min-w-40 px-2 py-1 text-xs">
                {t("requestsLog.columns.ip")}
              </TableHead>
              <TableHead className="min-w-24 px-2 py-1 text-xs">
                {t("requestsLog.columns.processingTime")}
              </TableHead>
              <TableHead className="min-w-48 px-2 py-1 text-xs">
                {t("requestsLog.columns.flags")}
              </TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            <tr aria-hidden className="pointer-events-none border-0">
              <td colSpan={7} style={{ height: visible.start * rowHeight }} />
            </tr>
            {filteredRows
              .slice(visible.start, visible.end)
              .map((row, index) => (
                <RequestTableRow
                  copyIps={copyIps}
                  index={visible.start + index}
                  key={row.uiKey}
                  language={i18n.language}
                  row={row}
                  t={t}
                />
              ))}
            <tr aria-hidden className="pointer-events-none border-0">
              <td
                colSpan={7}
                style={{
                  height:
                    Math.max(0, filteredRows.length - visible.end) * rowHeight,
                }}
              />
            </tr>
          </TableBody>
        </Table>
        {rows.length === 0 ? (
          <div className="px-3 py-8 text-center text-sm text-muted-foreground">
            {t("requestsLog.empty")}
          </div>
        ) : filtering && shownEvents === 0 ? (
          <div className="px-3 py-8 text-center text-sm text-muted-foreground">
            {t("requestsLog.filters.noMatches")}
          </div>
        ) : null}
      </div>
    </div>
  )
}

type TFn = (key: string, options?: Record<string, unknown>) => string

function RequestFilterBar({
  filters,
  onChange,
  ipInvalid,
  shown,
  total,
  t,
}: {
  filters: RequestFilters
  onChange: (filters: RequestFilters) => void
  ipInvalid: boolean
  shown: number
  total: number
  t: TFn
}) {
  const active = hasActiveFilters(filters)
  const toggleMethod = (method: RequestMethod, checked: boolean) =>
    onChange({
      ...filters,
      methods: allRequestMethods.filter((m) =>
        m === method ? checked : filters.methods.includes(m)
      ),
    })
  const textField = (
    key: "device" | "domain" | "ip",
    placeholder: string,
    invalid = false
  ) => (
    <label className="flex min-w-36 flex-1 basis-40 flex-col gap-1 text-xs text-muted-foreground">
      {t(`requestsLog.columns.${key}`)}
      <Input
        aria-invalid={invalid || undefined}
        className="h-8 font-mono text-xs md:text-xs"
        onChange={(event) =>
          onChange({ ...filters, [key]: event.target.value })
        }
        placeholder={placeholder}
        spellCheck={false}
        title={t(`requestsLog.filters.hint.${key}`)}
        value={filters[key]}
      />
    </label>
  )

  return (
    <div className="flex shrink-0 flex-wrap items-end gap-x-3 gap-y-2">
      {textField("device", t("requestsLog.filters.placeholder.device"))}
      <fieldset className="flex min-w-0 flex-col gap-1 text-xs text-muted-foreground">
        <legend className="float-left mb-1 p-0">
          {t("requestsLog.columns.method")}
        </legend>
        <div className="clear-both flex h-8 flex-wrap items-center gap-x-3 gap-y-1">
          {allRequestMethods.map((method) => {
            const badge = methodBadge(method, t)
            return (
              <label
                className="flex cursor-pointer items-center gap-1.5"
                key={method}
                title={badge.tooltip}
              >
                <Checkbox
                  checked={filters.methods.includes(method)}
                  onCheckedChange={(checked) => toggleMethod(method, checked)}
                />
                <Badge
                  className={badge.className}
                  size="xs"
                  variant={badge.variant}
                >
                  {badge.label}
                </Badge>
              </label>
            )
          })}
        </div>
      </fieldset>
      {textField("domain", t("requestsLog.filters.placeholder.domain"))}
      {textField("ip", t("requestsLog.filters.placeholder.ip"), ipInvalid)}
      <div className="flex h-8 items-center gap-2 text-xs text-muted-foreground">
        <span aria-live="polite" className="tabular-nums">
          {t("requestsLog.filters.count", { shown, total })}
        </span>
        {active ? (
          <Button
            onClick={() => onChange(emptyRequestFilters)}
            size="xs"
            variant="outline"
          >
            {t("requestsLog.filters.clear")}
          </Button>
        ) : null}
      </div>
      {ipInvalid ? (
        <p className="basis-full text-xs text-destructive">
          {t("requestsLog.filters.invalidIp")}
        </p>
      ) : null}
    </div>
  )
}

function RequestTableRow({
  row,
  index,
  copyIps,
  t,
  language,
}: {
  row: RequestRow
  index: number
  copyIps: (ips: string[]) => Promise<void>
  t: (key: string, options?: Record<string, unknown>) => string
  language: string
}) {
  if (row.type === "GAP") {
    return (
      <TableRow className="h-8 bg-amber-500/10 hover:bg-amber-500/10">
        <TableCell className="px-2 py-1 font-medium" colSpan={7}>
          {t("requestsLog.gap", {
            from: row.from_seq,
            to: row.to_seq,
          })}
        </TableCell>
      </TableRow>
    )
  }

  const method = methodBadge(row.source, t)
  const ips = row.ips.join(", ")
  const processingUs =
    row.hold_us > 0
      ? row.hold_us
      : (row.parse_us ?? 0) + (row.set_write_us ?? 0)
  const processing = formatProcessingTime(processingUs)
  const processingDetail = [
    row.hold_us > 0 ? `hold ${row.hold_us} µs` : null,
    row.parse_us !== undefined ? `parse ${row.parse_us} µs` : null,
    row.set_write_us !== undefined ? `set ${row.set_write_us} µs` : null,
    ...timingBreakdown(row),
  ]
    .filter((value): value is string => value !== null)
    .join(", ")
  const flags = buildRequestFlags(row, t, language)

  return (
    <TableRow className="h-8" data-row-index={index}>
      <TableCell
        className="border-r px-2 py-1 font-mono"
        title={row.client_ip ?? ""}
      >
        {row.client_ip || "—"}
      </TableCell>
      <TableCell className="border-r px-2 py-1">
        <Tooltip>
          <TooltipTrigger
            render={
              <Badge
                className={method.className}
                size="xs"
                tabIndex={0}
                variant={method.variant}
              />
            }
          >
            {method.label}
          </TooltipTrigger>
          <TooltipContent>{method.tooltip}</TooltipContent>
        </Tooltip>
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
                  aria-label={t("requestsLog.copyIps", { value: ips })}
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
      <TableCell
        className={cn(
          "border-r px-2 py-1 font-mono",
          processingTimeToneClass(processing.tone)
        )}
        title={processingDetail}
      >
        {processing.text}
      </TableCell>
      <TableCell className="px-2 py-1">
        {flags.length > 0 ? (
          // Inline flow with real spaces (not flex gap) so copied text keeps
          // a separator between badges: "parse:7µs set:0µs #213".
          <div className="w-max">
            {flags.map((flag, flagIndex) => (
              <Fragment key={flag.key}>
                {flagIndex > 0 ? " " : null}
                <Tooltip>
                  <TooltipTrigger
                    render={
                      <Badge
                        className="align-middle"
                        size="xs"
                        tabIndex={0}
                        variant={
                          flag.tone === "danger" ? "destructive" : "outline"
                        }
                      />
                    }
                  >
                    {flag.label}
                  </TooltipTrigger>
                  <TooltipContent>{flag.tooltip}</TooltipContent>
                </Tooltip>
              </Fragment>
            ))}
          </div>
        ) : (
          <span className="text-muted-foreground">—</span>
        )}
      </TableCell>
    </TableRow>
  )
}
