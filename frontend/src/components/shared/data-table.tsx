import type { ReactNode } from "react"

import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table"

export function DataTable({
  headers,
  rows,
  compact = false,
  narrowColumns = [],
}: {
  headers?: string[]
  rows: ReactNode[][]
  compact?: boolean
  narrowColumns?: number[]
}) {
  const lastColumnIndex = headers ? headers.length - 1 : (
    (rows.length && rows[0]?.length) ? rows[0].length - 1 : 0
  )
  const narrowColumnSet = new Set(narrowColumns)

  return (
    <div className="max-w-full overflow-x-auto rounded-md border">
      <Table className={compact ? "w-full text-sm" : "w-full text-base"}>
        {headers && (
          <TableHeader className="bg-muted/50">
            <TableRow>
              {headers.map((header, headerIndex) => (
                <TableHead
                  className={
                    headerIndex === lastColumnIndex
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
                  key={header}
                >
                  {header}
                </TableHead>
              ))}
            </TableRow>
          </TableHeader>
        )}
        <TableBody>
          {rows.map((row, index) => (
            <TableRow key={`${row[0]}-${index}`}>
              {row.map((cell, cellIndex) => (
                <TableCell
                  className={
                    cellIndex === lastColumnIndex
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
                  key={cellIndex}
                >
                  {cell}
                </TableCell>
              ))}
            </TableRow>
          ))}
        </TableBody>
      </Table>
    </div>
  )
}
