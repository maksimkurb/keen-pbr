import {
  Fragment,
  useCallback,
  useDeferredValue,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react"
import { ChevronDownIcon, ListFilterIcon } from "lucide-react"
import { useTranslation } from "react-i18next"
import { Link } from "wouter"
import { toast } from "sonner"

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
  Popover,
  PopoverContent,
  PopoverTrigger,
} from "@/components/ui/popover"
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
import { useAuth } from "@/auth/auth-context"
import { ListPlaceholder } from "@/components/shared/list-placeholder"
import { PageHeader } from "@/components/shared/page-header"
import { useIsMobile } from "@/hooks/use-mobile"
import {
  formatProcessingTime,
  processingTimeToneClass,
} from "@/lib/processing-time"
import {
  allAnswerKinds,
  allRequestActions,
  allRequestMethods,
  changedKindGroups,
  defaultAnswerKinds,
  emptyRequestFilters,
  isUnlearnableIp,
  filterRequests,
  hasActiveFilters,
  parseIpFilter,
  type RequestFilters,
} from "@/lib/request-filters"
import {
  buildRequestFlags,
  getIpColumnInfo,
  methodBadge,
  methodBadgeStyles,
  type RequestFlag,
} from "@/lib/request-flags"
import { timingBreakdown } from "@/lib/timeout-cause"
import { cn } from "@/lib/utils"

const maxRows = 2_000
const rowHeight = 32
// Mobile cards: four 18px lines, three 4px gaps and 8px vertical padding.
const cardHeight = 100
const defaultViewportHeight = 560
// v2: actions became "show checked" like the other groups (all by default).
const filtersStorageKey = "keen-pbr.requestsLog.filters.v2"
const legacyFiltersStorageKey = "keen-pbr.requestsLog.filters"
// Only widths changed from the defaults are stored, so new defaults apply.
const columnWidthsStorageKey = "keen-pbr.requestsLog.columnWidths.v2"

const columns = [
  { key: "device", width: 144 },
  { key: "method", width: 80 },
  { key: "domain", width: 256 },
  { key: "lists", width: 128 },
  { key: "ip", width: 160 },
  { key: "processingTime", width: 96 },
  { key: "flags", width: 360 },
] as const

type ColumnKey = (typeof columns)[number]["key"]
type ColumnWidths = Record<ColumnKey, number>

const minColumnWidth = 48
const maxColumnWidth = 1200
const keyboardResizeStep = 16
const defaultColumnWidths = Object.fromEntries(
  columns.map(({ key, width }) => [key, width])
) as ColumnWidths

const clampColumnWidth = (width: number) =>
  Math.round(Math.min(maxColumnWidth, Math.max(minColumnWidth, width)))

function loadFilters(): RequestFilters {
  try {
    const raw =
      window.localStorage.getItem(filtersStorageKey) ??
      window.localStorage.getItem(legacyFiltersStorageKey)
    if (!raw) return emptyRequestFilters
    const data = JSON.parse(raw) as Partial<RequestFilters> & {
      hideEmptyAnswers?: unknown
    }
    return {
      device: typeof data.device === "string" ? data.device : "",
      domain: typeof data.domain === "string" ? data.domain : "",
      ip: typeof data.ip === "string" ? data.ip : "",
      methods: Array.isArray(data.methods)
        ? allRequestMethods.filter((m) => data.methods?.includes(m))
        : allRequestMethods,
      actions: Array.isArray(data.actions)
        ? allRequestActions.filter(
            (a) =>
              data.actions?.includes(a) ||
              // "decision" was renamed to "listed".
              (a === "listed" &&
                (data.actions as string[] | undefined)?.includes("decision"))
          )
        : allRequestActions,
      answers: Array.isArray(data.answers)
        ? allAnswerKinds.filter((k) => data.answers?.includes(k))
        : data.hideEmptyAnswers === false
          ? allAnswerKinds
          : defaultAnswerKinds,
    }
  } catch {
    return emptyRequestFilters
  }
}

function loadColumnWidths(): ColumnWidths {
  try {
    const raw = window.localStorage.getItem(columnWidthsStorageKey)
    if (!raw) return defaultColumnWidths
    const data = JSON.parse(raw) as Partial<Record<string, unknown>>
    return Object.fromEntries(
      columns.map(({ key, width }) => {
        const value = data[key]
        return [
          key,
          typeof value === "number" && Number.isFinite(value)
            ? clampColumnWidth(value)
            : width,
        ]
      })
    ) as ColumnWidths
  } catch {
    return defaultColumnWidths
  }
}

type RequestRow = (DnsTestInterceptEvent | DnsTestGapEvent) & {
  uiKey: string
}

export function RequestsLogPage() {
  const { t } = useTranslation()
  const auth = useAuth()
  // The daemon refuses the full stream without authentication: it would
  // expose every client's traffic to anyone who can reach the router.
  if (!auth.enabled) {
    return (
      <div className="flex flex-col gap-3">
        <PageHeader
          className="mb-0 md:mb-0"
          title={t("nav.items.requestsLog")}
        />
        <ListPlaceholder
          action={
            <Button render={<Link href="/security" />}>
              {t("requestsLog.authRequired.action")}
            </Button>
          }
          description={t("requestsLog.authRequired.description")}
          title={t("requestsLog.authRequired.title")}
        />
      </div>
    )
  }
  return <RequestsLog />
}

function RequestsLog() {
  const { t, i18n } = useTranslation()
  const isMobile = useIsMobile()
  const [rows, setRows] = useState<RequestRow[]>([])
  const [filters, setFilters] = useState<RequestFilters>(loadFilters)
  const [columnWidths, setColumnWidths] =
    useState<ColumnWidths>(loadColumnWidths)
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

  useEffect(() => {
    // Dragging changes widths every frame; store once the drag settles.
    const timer = window.setTimeout(() => {
      try {
        const changed = Object.fromEntries(
          columns
            .filter(({ key, width }) => columnWidths[key] !== width)
            .map(({ key }) => [key, columnWidths[key]])
        )
        if (Object.keys(changed).length === 0) {
          window.localStorage.removeItem(columnWidthsStorageKey)
        } else {
          window.localStorage.setItem(
            columnWidthsStorageKey,
            JSON.stringify(changed)
          )
        }
      } catch {
        // Persistence is a convenience only.
      }
    }, 300)
    return () => window.clearTimeout(timer)
  }, [columnWidths])

  const resizeColumn = useCallback((key: ColumnKey, width: number) => {
    setColumnWidths((prev) => ({ ...prev, [key]: clampColumnWidth(width) }))
  }, [])

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
  const deviceInvalid = parseIpFilter(filters.device).kind === "invalid"
  const ipInvalid = parseIpFilter(filters.ip).kind === "invalid"

  // Virtualization runs over the filtered rows so scroll height matches.
  const itemHeight = isMobile ? cardHeight : rowHeight
  const visible = useMemo(() => {
    const start = Math.max(0, Math.floor(scrollTop / itemHeight) - 5)
    const count = Math.ceil(viewportHeight / itemHeight) + 10
    return { start, end: Math.min(filteredRows.length, start + count) }
  }, [filteredRows.length, itemHeight, scrollTop, viewportHeight])
  const visibleRows = filteredRows.slice(visible.start, visible.end)

  useEffect(() => {
    // Filters shrink the list; keep the scroll position inside it.
    const element = viewportRef.current
    if (element) setScrollTop(element.scrollTop)
  }, [filteredRows.length, isMobile])

  const copyText = useCallback(
    async (text: string) => {
      const copied = await writeClipboard(text)
      if (copied) {
        toast.success(t("common.copied"), { description: text })
      } else {
        toast.error(t("common.clipboardUnavailable"), { richColors: true })
      }
    },
    [t]
  )

  const tableWidth = columns.reduce(
    (sum, { key }) => sum + columnWidths[key],
    0
  )
  // Trailing filler column: absorbs spare width so the sized columns keep
  // exactly the width they were given.
  const spanAll = columns.length + 1

  return (
    <div className="flex min-h-0 flex-1 flex-col gap-3">
      <PageHeader className="mb-0 md:mb-0" title={t("nav.items.requestsLog")} />
      <RequestFilterBar
        deviceInvalid={deviceInvalid}
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
        {isMobile ? (
          <div
            className="relative"
            style={{ height: filteredRows.length * cardHeight }}
          >
            <div
              style={{
                transform: `translateY(${visible.start * cardHeight}px)`,
              }}
            >
              {visibleRows.map((row) => (
                <RequestCard
                  copyText={copyText}
                  key={row.uiKey}
                  language={i18n.language}
                  row={row}
                  t={t}
                />
              ))}
            </div>
          </div>
        ) : (
          <Table
            className="table-fixed text-xs whitespace-nowrap"
            containerClassName="overflow-visible"
            style={{ width: tableWidth, minWidth: "100%" }}
          >
            {/* Rows are virtualized, so content-sized columns would jump while
                scrolling; widths come from the colgroup only. */}
            <colgroup>
              {columns.map(({ key }) => (
                <col key={key} style={{ width: columnWidths[key] }} />
              ))}
              <col />
            </colgroup>
            <TableHeader className="sticky top-0 z-10 bg-muted/95 backdrop-blur">
              <TableRow className="h-8 hover:bg-transparent">
                {columns.map(({ key }) => {
                  const label = t(`requestsLog.columns.${key}`)
                  return (
                    <TableHead className="relative px-2 py-1 text-xs" key={key}>
                      <span className="block truncate" title={label}>
                        {label}
                      </span>
                      <ColumnResizeHandle
                        label={t("requestsLog.resizeColumn", { column: label })}
                        onReset={() =>
                          resizeColumn(key, defaultColumnWidths[key])
                        }
                        onResize={(width) => resizeColumn(key, width)}
                        width={columnWidths[key]}
                      />
                    </TableHead>
                  )
                })}
                <TableHead aria-hidden className="p-0" />
              </TableRow>
            </TableHeader>
            <TableBody>
              <tr aria-hidden className="pointer-events-none border-0">
                <td
                  colSpan={spanAll}
                  style={{ height: visible.start * rowHeight }}
                />
              </tr>
              {visibleRows.map((row, index) => (
                <RequestTableRow
                  copyText={copyText}
                  index={visible.start + index}
                  key={row.uiKey}
                  language={i18n.language}
                  row={row}
                  t={t}
                />
              ))}
              <tr aria-hidden className="pointer-events-none border-0">
                <td
                  colSpan={spanAll}
                  style={{
                    height:
                      Math.max(0, filteredRows.length - visible.end) *
                      rowHeight,
                  }}
                />
              </tr>
            </TableBody>
          </Table>
        )}
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

async function writeClipboard(text: string): Promise<boolean> {
  try {
    if (navigator.clipboard?.writeText) {
      await navigator.clipboard.writeText(text)
      return true
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
    return document.execCommand("copy")
  } catch {
    return false
  } finally {
    document.body.removeChild(textarea)
  }
}

function ColumnResizeHandle({
  label,
  width,
  onResize,
  onReset,
}: {
  label: string
  width: number
  onResize: (width: number) => void
  onReset: () => void
}) {
  const dragRef = useRef<{
    pointerId: number
    startX: number
    startWidth: number
  } | null>(null)
  const endDrag = (event: React.PointerEvent<HTMLDivElement>) => {
    if (dragRef.current?.pointerId === event.pointerId) dragRef.current = null
  }

  return (
    <div
      aria-label={label}
      aria-orientation="vertical"
      aria-valuemax={maxColumnWidth}
      aria-valuemin={minColumnWidth}
      aria-valuenow={width}
      className="absolute inset-y-0 -right-1 z-10 w-2 cursor-col-resize touch-none select-none after:absolute after:inset-y-1.5 after:left-1/2 after:w-px after:-translate-x-1/2 after:bg-border hover:after:w-0.5 hover:after:bg-primary focus-visible:outline-none focus-visible:after:w-0.5 focus-visible:after:bg-ring"
      onDoubleClick={onReset}
      onKeyDown={(event) => {
        if (event.key !== "ArrowLeft" && event.key !== "ArrowRight") return
        event.preventDefault()
        const step =
          event.key === "ArrowLeft" ? -keyboardResizeStep : keyboardResizeStep
        onResize(width + step)
      }}
      onLostPointerCapture={endDrag}
      onPointerCancel={endDrag}
      onPointerDown={(event) => {
        if (event.button !== 0) return
        event.preventDefault()
        event.currentTarget.setPointerCapture(event.pointerId)
        dragRef.current = {
          pointerId: event.pointerId,
          startX: event.clientX,
          startWidth: width,
        }
      }}
      onPointerMove={(event) => {
        const drag = dragRef.current
        if (drag?.pointerId !== event.pointerId) return
        onResize(drag.startWidth + event.clientX - drag.startX)
      }}
      onPointerUp={endDrag}
      role="separator"
      tabIndex={0}
    />
  )
}

type TFn = (key: string, options?: Record<string, unknown>) => string

/** Keeps `all` order: `selected` with `item` added or removed. */
function toggleItem<T>(
  all: readonly T[],
  selected: readonly T[],
  item: T,
  checked: boolean
): T[] {
  return all.filter((x) => (x === item ? checked : selected.includes(x)))
}

function KindFilterGroup({
  title,
  children,
}: {
  title: string
  children: React.ReactNode
}) {
  return (
    <div
      aria-label={title}
      className="flex flex-col gap-0.5 border-t pt-2.5 first:border-t-0 first:pt-0"
      role="group"
    >
      <div className="font-medium text-foreground">{title}</div>
      {children}
    </div>
  )
}

function KindFilterOption({
  checked,
  onCheckedChange,
  title,
  children,
}: {
  checked: boolean
  onCheckedChange: (checked: boolean) => void
  title?: string
  children: React.ReactNode
}) {
  return (
    <label
      className="flex min-h-7 cursor-pointer items-center gap-2 rounded-md px-1 hover:bg-muted"
      title={title}
    >
      <Checkbox checked={checked} onCheckedChange={onCheckedChange} />
      {children}
    </label>
  )
}

function KindFilterMenu({
  filters,
  onChange,
  t,
}: {
  filters: RequestFilters
  onChange: (filters: RequestFilters) => void
  t: TFn
}) {
  const changed = changedKindGroups(filters)
  return (
    <Popover>
      <PopoverTrigger
        render={<Button className="h-8 gap-1.5" size="sm" variant="outline" />}
      >
        <ListFilterIcon />
        {t("requestsLog.filters.kinds")}
        {/* Fixed slot so the trigger keeps its width. */}
        <Badge
          className={cn(
            "min-w-4.5 justify-center bg-destructive text-white dark:bg-destructive",
            changed === 0 && "invisible"
          )}
          size="xs"
          variant="destructive"
        >
          {changed}
        </Badge>
        <ChevronDownIcon className="text-muted-foreground" />
      </PopoverTrigger>
      <PopoverContent
        align="start"
        className="max-h-(--available-height) w-80 gap-2.5 overflow-y-auto p-3 text-xs"
      >
        <KindFilterGroup title={t("requestsLog.filters.groups.method")}>
          {allRequestMethods.map((method) => {
            const badge = methodBadge(method, t)
            return (
              <KindFilterOption
                checked={filters.methods.includes(method)}
                key={method}
                onCheckedChange={(checked) =>
                  onChange({
                    ...filters,
                    methods: toggleItem(
                      allRequestMethods,
                      filters.methods,
                      method,
                      checked
                    ),
                  })
                }
                title={badge.tooltip}
              >
                <Badge
                  className={badge.className}
                  size="xs"
                  variant={badge.variant}
                >
                  {badge.label}
                </Badge>
              </KindFilterOption>
            )
          })}
        </KindFilterGroup>
        <KindFilterGroup title={t("requestsLog.filters.groups.actions")}>
          {allRequestActions.map((action) => (
            <KindFilterOption
              checked={filters.actions.includes(action)}
              key={action}
              onCheckedChange={(checked) =>
                onChange({
                  ...filters,
                  actions: toggleItem(
                    allRequestActions,
                    filters.actions,
                    action,
                    checked
                  ),
                })
              }
            >
              {t(`requestsLog.filters.actions.${action}`)}
            </KindFilterOption>
          ))}
        </KindFilterGroup>
        <KindFilterGroup title={t("requestsLog.filters.groups.answers")}>
          {allAnswerKinds.map((kind) => (
            <KindFilterOption
              checked={filters.answers.includes(kind)}
              key={kind}
              onCheckedChange={(checked) =>
                onChange({
                  ...filters,
                  answers: toggleItem(
                    allAnswerKinds,
                    filters.answers,
                    kind,
                    checked
                  ),
                })
              }
            >
              {t(`requestsLog.filters.answers.${kind}`)}
            </KindFilterOption>
          ))}
        </KindFilterGroup>
      </PopoverContent>
    </Popover>
  )
}

function RequestFilterBar({
  filters,
  onChange,
  deviceInvalid,
  ipInvalid,
  shown,
  total,
  t,
}: {
  filters: RequestFilters
  onChange: (filters: RequestFilters) => void
  deviceInvalid: boolean
  ipInvalid: boolean
  shown: number
  total: number
  t: TFn
}) {
  const active = hasActiveFilters(filters)
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
      {textField(
        "device",
        t("requestsLog.filters.placeholder.device"),
        deviceInvalid
      )}
      {textField("domain", t("requestsLog.filters.placeholder.domain"))}
      {textField("ip", t("requestsLog.filters.placeholder.ip"), ipInvalid)}
      <KindFilterMenu filters={filters} onChange={onChange} t={t} />
      <div className="flex h-8 items-center gap-2 text-xs text-muted-foreground">
        <span aria-live="polite" className="min-w-[12ch] tabular-nums">
          {t("requestsLog.filters.count", { shown, total })}
        </span>
        {/* Always rendered so toggling filters never reflows the bar. */}
        <Button
          aria-hidden={!active || undefined}
          className={cn(!active && "invisible")}
          onClick={() => onChange(emptyRequestFilters)}
          size="xs"
          variant="outline"
        >
          {t("requestsLog.filters.clear")}
        </Button>
      </div>
      {deviceInvalid ? (
        <p className="basis-full text-xs text-destructive">
          {t("requestsLog.filters.invalidDevice")}
        </p>
      ) : null}
      {ipInvalid ? (
        <p className="basis-full text-xs text-destructive">
          {t("requestsLog.filters.invalidIp")}
        </p>
      ) : null}
    </div>
  )
}

type RequestEvent = Exclude<RequestRow, { type: "GAP" }>

function describeRequest(row: RequestEvent, t: TFn, language: string) {
  const processingUs =
    row.hold_us > 0
      ? row.hold_us
      : (row.parse_us ?? 0) + (row.set_write_us ?? 0)
  const processingDetail = [
    row.hold_us > 0 ? `hold ${row.hold_us} µs` : null,
    row.parse_us !== undefined ? `parse ${row.parse_us} µs` : null,
    row.set_write_us !== undefined ? `set ${row.set_write_us} µs` : null,
    ...timingBreakdown(row),
  ]
    .filter((value): value is string => value !== null)
    .join(", ")
  return {
    method: methodBadge(row.source, t, row),
    ipInfo: getIpColumnInfo(row, t),
    lists: row.lists.join(", "),
    processing: formatProcessingTime(processingUs),
    processingDetail,
    flags: buildRequestFlags(row, t, language),
  }
}

type RequestView = ReturnType<typeof describeRequest>

function ipToneClass(tone: RequestView["ipInfo"]["tone"]) {
  return cn({
    "text-muted-foreground": tone === "muted",
    [warnTextClass]: tone === "error",
  })
}

function MethodBadge({ method }: { method: RequestView["method"] }) {
  return (
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
  )
}

const copyButtonClass =
  "block max-w-full min-w-0 truncate text-left font-mono underline decoration-dotted underline-offset-2"

/** Click-to-copy text; an empty value renders a plain dash. */
function CopyValue({
  value,
  copyText,
  t,
  className,
}: {
  value: string | undefined
  copyText: (text: string) => Promise<void>
  t: TFn
  className?: string
}) {
  if (!value) return <>—</>
  return (
    <button
      aria-label={t("requestsLog.copyValue", { value })}
      className={cn(copyButtonClass, className)}
      onClick={() => void copyText(value)}
      title={value}
      type="button"
    >
      {value}
    </button>
  )
}

const warnTextClass = "text-amber-600 dark:text-amber-400"

/** Comma-joined addresses; ones keen-pbr never learns (0.0.0.0, ::,
 * loopback) are highlighted as a warning. */
function IpList({ ips }: { ips: string[] }) {
  return ips.map((ip, index) => (
    <Fragment key={`${index}-${ip}`}>
      {index > 0 ? ", " : null}
      <span className={cn(isUnlearnableIp(ip) && warnTextClass)}>{ip}</span>
    </Fragment>
  ))
}

function IpValue({
  row,
  ipInfo,
  copyText,
  t,
}: {
  row: RequestEvent
  ipInfo: RequestView["ipInfo"]
  copyText: (text: string) => Promise<void>
  t: TFn
}) {
  const ips = row.ips.join(", ")
  if (ipInfo.isClickable && ipInfo.text) {
    return (
      <Tooltip>
        <TooltipTrigger
          render={
            <button
              aria-label={t("requestsLog.copyIps", { value: ips })}
              className={copyButtonClass}
              onClick={() => void copyText(ips)}
              type="button"
            />
          }
        >
          <IpList ips={row.ips} />
        </TooltipTrigger>
        <TooltipContent>{ips}</TooltipContent>
      </Tooltip>
    )
  }
  if (!ipInfo.text) return <>—</>
  if (!ipInfo.tooltip) return <span className="truncate">{ipInfo.text}</span>
  return (
    <Tooltip>
      <TooltipTrigger
        render={<span className="min-w-0 truncate">{ipInfo.text}</span>}
      />
      <TooltipContent>{ipInfo.tooltip}</TooltipContent>
    </Tooltip>
  )
}

const flagBadgeStyles: Record<
  RequestFlag["tone"],
  { variant: "destructive" | "secondary" | "outline"; className: string }
> = {
  danger: { variant: "destructive", className: "" },
  positive: methodBadgeStyles.sni,
  info: methodBadgeStyles.http,
  neutral: { variant: "outline", className: "" },
}

function FlagBadges({
  flags,
  className,
}: {
  flags: RequestView["flags"]
  className?: string
}) {
  if (flags.length === 0) {
    return <span className="text-muted-foreground">—</span>
  }
  // Inline flow with real spaces (not flex gap) so copied text keeps a
  // separator between badges: "parse:7µs set:0µs #213".
  return (
    <div className={cn("truncate", className)}>
      {flags.map((flag, flagIndex) => (
        <Fragment key={flag.key}>
          {flagIndex > 0 ? " " : null}
          <Tooltip>
            <TooltipTrigger
              render={
                <Badge
                  className={cn(
                    "align-middle",
                    flagBadgeStyles[flag.tone].className
                  )}
                  size="xs"
                  tabIndex={0}
                  variant={flagBadgeStyles[flag.tone].variant}
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
  )
}

const cellClass = "overflow-hidden border-r px-2 py-1 text-ellipsis"

function RequestTableRow({
  row,
  index,
  copyText,
  t,
  language,
}: {
  row: RequestRow
  index: number
  copyText: (text: string) => Promise<void>
  t: TFn
  language: string
}) {
  if (row.type === "GAP") {
    return (
      <TableRow className="h-8 bg-amber-500/10 hover:bg-amber-500/10">
        <TableCell
          className="overflow-hidden px-2 py-1 font-medium text-ellipsis"
          colSpan={columns.length + 1}
        >
          {t("requestsLog.gap", {
            from: row.from_seq,
            to: row.to_seq,
          })}
        </TableCell>
      </TableRow>
    )
  }

  const view = describeRequest(row, t, language)

  return (
    <TableRow className="h-8" data-row-index={index}>
      <TableCell className={cellClass}>
        <CopyValue copyText={copyText} t={t} value={row.client_ip} />
      </TableCell>
      <TableCell className={cellClass}>
        <MethodBadge method={view.method} />
      </TableCell>
      <TableCell className={cellClass}>
        <CopyValue copyText={copyText} t={t} value={row.domain} />
      </TableCell>
      <TableCell className={cellClass} title={view.lists}>
        {view.lists || "—"}
      </TableCell>
      <TableCell className={cn(cellClass, ipToneClass(view.ipInfo.tone))}>
        <IpValue copyText={copyText} ipInfo={view.ipInfo} row={row} t={t} />
      </TableCell>
      <TableCell
        className={cn(
          cellClass,
          "font-mono",
          processingTimeToneClass(view.processing.tone)
        )}
        title={view.processingDetail}
      >
        {view.processing.text}
      </TableCell>
      <TableCell className={cellClass}>
        <FlagBadges flags={view.flags} />
      </TableCell>
      <TableCell aria-hidden className="p-0" />
    </TableRow>
  )
}

const cardLineClass = "flex h-[18px] min-w-0 items-center gap-2"

function RequestCard({
  row,
  copyText,
  t,
  language,
}: {
  row: RequestRow
  copyText: (text: string) => Promise<void>
  t: TFn
  language: string
}) {
  if (row.type === "GAP") {
    return (
      <div
        className="flex items-center border-b bg-amber-500/10 px-3 text-xs font-medium"
        style={{ height: cardHeight }}
      >
        {t("requestsLog.gap", {
          from: row.from_seq,
          to: row.to_seq,
        })}
      </div>
    )
  }

  const view = describeRequest(row, t, language)

  return (
    <div
      className="flex flex-col justify-center gap-1 overflow-hidden border-b px-3 py-2 text-xs"
      style={{ height: cardHeight }}
    >
      <div className={cardLineClass}>
        <MethodBadge method={view.method} />
        <span className="min-w-0 flex-1 truncate font-medium">
          <CopyValue copyText={copyText} t={t} value={row.domain} />
        </span>
        <span
          className={cn(
            "shrink-0 font-mono",
            processingTimeToneClass(view.processing.tone)
          )}
          title={view.processingDetail}
        >
          {view.processing.text}
        </span>
      </div>
      <div className={cn(cardLineClass, "text-muted-foreground")}>
        <span className="shrink-0">
          <CopyValue copyText={copyText} t={t} value={row.client_ip} />
        </span>
        {view.lists ? (
          <span className="min-w-0 truncate" title={view.lists}>
            {view.lists}
          </span>
        ) : null}
      </div>
      <div className={cn(cardLineClass, ipToneClass(view.ipInfo.tone))}>
        <IpValue copyText={copyText} ipInfo={view.ipInfo} row={row} t={t} />
      </div>
      <div className={cardLineClass}>
        <FlagBadges className="min-w-0 flex-1" flags={view.flags} />
      </div>
    </div>
  )
}
