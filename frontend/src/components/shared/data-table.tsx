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
  onToggleRow: (rowId: string) => void
  onSelectAllVisible: (selected: boolean) => void
  /** Shown beside the header “select all” control (native title / concise hint). */
  selectAllTooltip?: string
  selectAllAriaLabel: string
  selectRowAriaLabel: (rowId: string) => string
  /** Disables checkbox column (e.g. while a mutation is pending). */
  selectionDisabled?: boolean
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
  const hasSelection =
    Boolean(selection && selection.rowIds.length === rows.length)
  const mergedHeaders =
    hasSelection && headers ? ["", ...headers] : headers
  const lastColumnIndex = mergedHeaders
    ? mergedHeaders.length - 1
    : (rows.length && rows[0]?.length)
      ? rows[0].length - 1
      : 0
  // Callers pass narrowColumns in content-column coordinates; when a selection
  // checkbox column is prepended, shift them so the styling stays aligned.
  const narrowColumnSet = new Set(
    hasSelection ? narrowColumns.map((index) => index + 1) : narrowColumns,
  )

  const visibleIds =
    hasSelection ? selection!.rowIds.filter((id) => id.length > 0) : []
  const allSelectableSelected =
    visibleIds.length > 0 &&
    visibleIds.every((id) => selection!.selectedIds.has(id))

  function headClass(headerIndex: number) {
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

  const selectionLocked = Boolean(selection?.selectionDisabled)

  return (
    <div
      aria-busy={selectionLocked}
      className="max-w-full overflow-x-auto rounded-md border"
    >
      <Table className={compact ? "w-full text-sm" : "w-full text-base"}>
        {mergedHeaders ? (
          <TableHeader className="bg-muted/50">
            <TableRow>
              {mergedHeaders.map((header, headerIndex) => (
                <TableHead className={headClass(headerIndex)} key={`h-${headerIndex}`}>
                  {hasSelection && headerIndex === 0 ? (
                    <div className="flex justify-center pt-1" title={selection!.selectAllTooltip}>
                      <Checkbox
                        aria-label={selection!.selectAllAriaLabel}
                        checked={allSelectableSelected}
                        disabled={selectionLocked || visibleIds.length === 0}
                        onCheckedChange={(checked) => {
                          selection!.onSelectAllVisible(checked === true)
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
        ) : null}
        <TableBody>
          {rows.map((row, index) => {
            const rowId = hasSelection ? selection!.rowIds[index] ?? "" : ""

            const cells = (
              <>
                {hasSelection ? (
                  <TableCell className={cellClass(0)} key="selection">
                    <div className="flex justify-center">
                      <Checkbox
                        aria-label={
                          rowId
                            ? selection!.selectRowAriaLabel(rowId)
                            : selection!.selectAllAriaLabel
                        }
                        checked={rowId ? selection!.selectedIds.has(rowId) : false}
                        disabled={selectionLocked || !rowId}
                        onCheckedChange={() => {
                          if (rowId) {
                            selection!.onToggleRow(rowId)
                          }
                        }}
                      />
                    </div>
                  </TableCell>
                ) : null}
                {row.map((cell, cellIndex) => {
                  const displayIndex =
                    cellIndex + (hasSelection ? 1 : 0)
                  return (
                    <TableCell className={cellClass(displayIndex)} key={cellIndex}>
                      {cell}
                    </TableCell>
                  )
                })}
              </>
            )

            return (
              <TableRow key={hasSelection ? rowId || `${index}` : `${row[0]}-${index}`}>
                {cells}
              </TableRow>
            )
          })}
        </TableBody>
      </Table>
    </div>
  )
}
