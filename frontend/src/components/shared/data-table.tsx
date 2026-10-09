import {
  useEffect,
  useRef,
  useState,
  type ComponentProps,
  type ReactNode,
} from "react"
import { useTranslation } from "react-i18next"

import { TooltipTouchGesture } from "@/components/ui/tooltip-touch"
import { Button } from "@/components/ui/button"

import { Checkbox } from "@/components/ui/checkbox"
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table"

export type DataTableSelection = {
  rowIds: string[]
  selectedIds: ReadonlySet<string>
  disabled?: boolean
  isSelecting?: boolean
  onStartSelecting?: () => void
  onToggle: (rowId: string) => void
  onToggleAll: (checked: boolean) => void
  selectAllLabel?: string
  getRowLabel: (rowId: string) => string
}

export function DataTable({
  headers,
  rows,
  compact = false,
  narrowColumns = [],
  selection,
  getRowProps,
  mobileCards,
}: {
  headers?: string[]
  rows: ReactNode[][]
  compact?: boolean
  narrowColumns?: number[]
  selection?: DataTableSelection
  mobileCards?: {
    titleColumns: number[]
    bodyColumns: number[]
    hideLabels?: number[]
    beforeBody?: (rowIndex: number) => ReactNode
  }
  getRowProps?: (
    rowIndex: number
  ) => Omit<ComponentProps<"tr">, "children" | "key">
}) {
  const { t } = useTranslation()
  const [gesture] = useState(() => new TooltipTouchGesture())
  const touchListeners = useRef<AbortController | null>(null)
  useEffect(
    () => () => {
      gesture.cancel()
      touchListeners.current?.abort()
    },
    [gesture]
  )
  const hasSelection = Boolean(
    selection && selection.rowIds.length === rows.length
  )
  const headersWithSelection =
    hasSelection && headers ? ["", ...headers] : headers
  const lastColumnIndex = headersWithSelection
    ? headersWithSelection.length - 1
    : rows.length && rows[0]?.length
      ? rows[0].length - 1
      : 0
  const narrowColumnSet = new Set(
    hasSelection ? narrowColumns.map((index) => index + 1) : narrowColumns
  )
  const visibleRowIds = hasSelection
    ? selection!.rowIds.filter((rowId) => rowId.length > 0)
    : []
  const allVisibleSelected =
    visibleRowIds.length > 0 &&
    visibleRowIds.every((rowId) => selection!.selectedIds.has(rowId))

  function headClass(headerIndex: number) {
    if (hasSelection && headerIndex === 0) {
      return compact
        ? "h-8 w-px px-1.5 font-semibold whitespace-nowrap"
        : "w-px px-2 font-semibold whitespace-nowrap"
    }

    return headerIndex === lastColumnIndex
      ? compact
        ? "h-8 w-px text-right font-semibold"
        : "w-px text-right font-semibold"
      : narrowColumnSet.has(headerIndex)
        ? compact
          ? "h-8 w-px font-semibold whitespace-nowrap"
          : "w-px font-semibold whitespace-nowrap"
        : compact
          ? "h-8 font-semibold"
          : "font-semibold"
  }

  function cellClass(cellIndex: number) {
    if (hasSelection && cellIndex === 0) {
      return compact
        ? "w-px px-1.5 py-1.5 align-middle whitespace-nowrap"
        : "w-px px-2 py-3 align-middle whitespace-nowrap"
    }

    return cellIndex === lastColumnIndex
      ? compact
        ? "w-px px-2 py-1.5 text-right align-middle whitespace-nowrap"
        : "w-px p-3 text-right align-middle whitespace-nowrap"
      : narrowColumnSet.has(cellIndex)
        ? compact
          ? "w-px px-2 py-1.5 align-middle whitespace-nowrap"
          : "w-px p-3 align-middle whitespace-nowrap"
        : compact
          ? "px-2 py-1.5 align-middle whitespace-normal"
          : "p-3 align-middle whitespace-normal"
  }

  return (
    <>
      {mobileCards ? (
        <div className="space-y-3 md:hidden">
          {hasSelection ? (
            <div className="flex justify-end">
              <Button
                variant="outline"
                className={
                  selection!.isSelecting || selection!.selectedIds.size > 0
                    ? "invisible"
                    : undefined
                }
                disabled={
                  selection!.disabled ||
                  selection!.isSelecting ||
                  selection!.selectedIds.size > 0
                }
                onClick={selection!.onStartSelecting}
              >
                {t("common.selection.select")}
              </Button>
            </div>
          ) : null}
          {rows.map((row, index) => {
            const rowId = hasSelection ? selection!.rowIds[index] : ""
            const selected = hasSelection && selection!.selectedIds.has(rowId)
            const selectionActive =
              hasSelection &&
              (selection!.isSelecting || selection!.selectedIds.size > 0)
            return (
              <article
                key={rowId || index}
                data-selected={selected}
                data-selecting={selectionActive}
                onPointerDownCapture={(event) => {
                  gesture.reset()
                  touchListeners.current?.abort()
                  if (
                    event.pointerType !== "touch" ||
                    !event.isPrimary ||
                    selectionActive ||
                    !hasSelection ||
                    selection!.disabled ||
                    !rowId ||
                    event.defaultPrevented
                  )
                    return
                  if (
                    (event.target as Element).closest(
                      "button, a, input, select, textarea, [role=button], [role=checkbox], [role=switch]"
                    )
                  )
                    return
                  gesture.start(
                    event.pointerId,
                    event.clientX,
                    event.clientY,
                    true,
                    () => {
                      selection!.onStartSelecting?.()
                      if (!selected) selection!.onToggle(rowId)
                    }
                  )
                  const controller = new AbortController()
                  touchListeners.current = controller
                  const options = {
                    capture: true,
                    passive: true,
                    signal: controller.signal,
                  }
                  window.addEventListener(
                    "pointermove",
                    (e) => gesture.move(e.pointerId, e.clientX, e.clientY),
                    options
                  )
                  window.addEventListener(
                    "pointerup",
                    (e) => {
                      gesture.end(e.pointerId)
                      controller.abort()
                    },
                    options
                  )
                  const cancel = () => {
                    gesture.cancel()
                    controller.abort()
                  }
                  window.addEventListener("pointercancel", cancel, options)
                  window.addEventListener("scroll", cancel, options)
                }}
                onClickCapture={(event) => {
                  if (gesture.consumeClick(event.detail)) {
                    event.preventDefault()
                    event.stopPropagation()
                  }
                }}
                onContextMenu={(event) => {
                  if (gesture.contextMenu()) {
                    event.preventDefault()
                    event.stopPropagation()
                  }
                }}
                onClick={() => {
                  if (selectionActive && !selection!.disabled && rowId)
                    selection!.onToggle(rowId)
                }}
                className="min-w-0 overflow-hidden rounded-xl border bg-card text-card-foreground data-[selected=true]:border-primary data-[selecting=true]:cursor-pointer"
              >
                <div
                  data-selected={selected}
                  className="flex items-start gap-2 border-b bg-muted/50 px-3 py-2 data-[selected=true]:border-primary data-[selected=true]:bg-primary/20"
                >
                  <div
                    inert={selectionActive}
                    className="flex min-w-0 flex-1 flex-wrap items-center gap-2 self-center font-medium wrap-anywhere"
                  >
                    {mobileCards.titleColumns.map((column) => (
                      <div key={column} className="flex min-w-0 items-center">
                        {row[column]}
                      </div>
                    ))}
                  </div>
                  <div className="relative -my-2 -mr-3 min-h-11 shrink-0">
                    <div
                      inert={selectionActive}
                      className={selectionActive ? "invisible" : undefined}
                    >
                      {row[row.length - 1]}
                    </div>
                    {selectionActive ? (
                      <label
                        onClick={(event) => event.stopPropagation()}
                        className="absolute inset-y-0 right-0 flex size-11 items-center justify-center"
                      >
                        <Checkbox
                          aria-label={selection!.getRowLabel(rowId)}
                          checked={selected}
                          disabled={selection!.disabled || !rowId}
                          onCheckedChange={() => selection!.onToggle(rowId)}
                        />
                      </label>
                    ) : null}
                  </div>
                </div>
                <div inert={selectionActive} className="space-y-2 p-3">
                  {mobileCards.beforeBody?.(index)}
                  {mobileCards.bodyColumns.map((column) => (
                    <div
                      key={column}
                      className="min-w-0 space-y-1 text-sm wrap-anywhere"
                    >
                      {headers?.[column] &&
                      !mobileCards.hideLabels?.includes(column) ? (
                        <div className="text-xs text-muted-foreground">
                          {headers[column]}
                        </div>
                      ) : null}
                      {row[column]}
                    </div>
                  ))}
                </div>
              </article>
            )
          })}
        </div>
      ) : null}
      <div
        className={`max-w-full overflow-x-auto rounded-md border ${mobileCards ? "hidden md:block" : ""}`}
      >
        <Table className={compact ? "w-full text-sm" : "w-full text-base"}>
          {headersWithSelection && (
            <TableHeader className="bg-muted/50">
              <TableRow>
                {headersWithSelection.map((header, headerIndex) => (
                  <TableHead
                    className={headClass(headerIndex)}
                    key={`${header}-${headerIndex}`}
                  >
                    {hasSelection && headerIndex === 0 ? (
                      <div className="flex justify-center">
                        <Checkbox
                          aria-label={
                            selection!.selectAllLabel ??
                            "Select all visible rows"
                          }
                          checked={allVisibleSelected}
                          disabled={
                            selection!.disabled || visibleRowIds.length === 0
                          }
                          onCheckedChange={(checked) => {
                            selection!.onToggleAll(checked === true)
                          }}
                        />
                      </div>
                    ) : (
                      header
                    )}
                  </TableHead>
                ))}
              </TableRow>
            </TableHeader>
          )}
          <TableBody>
            {rows.map((row, index) => {
              const rowId = hasSelection ? (selection!.rowIds[index] ?? "") : ""
              const rowProps = getRowProps?.(index)

              return (
                <TableRow
                  {...rowProps}
                  key={hasSelection ? rowId || index : `${row[0]}-${index}`}
                >
                  {hasSelection ? (
                    <TableCell className={cellClass(0)}>
                      <div className="flex justify-center">
                        <Checkbox
                          aria-label={
                            rowId
                              ? selection!.getRowLabel(rowId)
                              : (selection!.selectAllLabel ?? "Select row")
                          }
                          checked={
                            rowId ? selection!.selectedIds.has(rowId) : false
                          }
                          disabled={selection!.disabled || !rowId}
                          onCheckedChange={() => {
                            if (rowId) {
                              selection!.onToggle(rowId)
                            }
                          }}
                        />
                      </div>
                    </TableCell>
                  ) : null}
                  {row.map((cell, cellIndex) => {
                    const displayIndex = cellIndex + (hasSelection ? 1 : 0)

                    return (
                      <TableCell
                        className={cellClass(displayIndex)}
                        key={cellIndex}
                      >
                        {cell}
                      </TableCell>
                    )
                  })}
                </TableRow>
              )
            })}
          </TableBody>
        </Table>
      </div>
    </>
  )
}
