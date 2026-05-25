import type { ReactNode } from "react"

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
}: {
  headers?: string[]
  rows: ReactNode[][]
  compact?: boolean
  narrowColumns?: number[]
  selection?: DataTableSelection
}) {
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
    <div className="max-w-full overflow-x-auto rounded-md border">
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
                          selection!.selectAllLabel ?? "Select all visible rows"
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

            return (
              <TableRow
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
  )
}
