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
  headers: string[]
  rows: ReactNode[][]
  compact?: boolean
  narrowColumns?: number[]
}) {
  const lastColumnIndex = headers.length - 1
  const narrowColumnSet = new Set(narrowColumns)

  return (
    <div className="max-w-full overflow-x-auto rounded-md border">
      <Table className={compact ? "w-full text-sm" : "w-full text-base"}>
        <TableBody>
          {rows.map((row, index) => (
            <TableRow key={`${row[0]}-${index}`}>
              {row.map((cell, cellIndex) => (
                <TableCell
                  className={
                    cellIndex === lastColumnIndex
                      ? compact
                        ? "w-px whitespace-nowrap px-2 py-1.5 text-right align-middle"
                        : "w-px whitespace-nowrap p-3 text-right align-middle"
                      : narrowColumnSet.has(cellIndex)
                        ? compact
                          ? "w-px whitespace-nowrap px-2 py-1.5 align-middle"
                          : "w-px whitespace-nowrap p-3 align-middle"
                      : compact
                        ? "whitespace-normal px-2 py-1.5 align-middle"
                        : "whitespace-normal p-3 align-middle"
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
