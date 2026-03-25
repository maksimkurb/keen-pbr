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
      <Table className={compact ? "w-full min-w-[640px] text-sm" : "w-full min-w-[640px] text-base"}>
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
                        ? "h-8 w-px whitespace-nowrap font-semibold"
                        : "w-px whitespace-nowrap font-semibold"
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
        <TableBody>
          {rows.map((row, index) => (
            <TableRow key={`${row[0]}-${index}`}>
              {row.map((cell, cellIndex) => (
                <TableCell
                  className={
                    cellIndex === lastColumnIndex
                      ? compact
                        ? "w-px whitespace-nowrap px-2 py-1.5 text-right align-top"
                        : "w-px whitespace-nowrap p-3 text-right align-top"
                      : narrowColumnSet.has(cellIndex)
                        ? compact
                          ? "w-px whitespace-nowrap px-2 py-1.5 align-top"
                          : "w-px whitespace-nowrap p-3 align-top"
                      : compact
                        ? "whitespace-normal px-2 py-1.5 align-top"
                        : "whitespace-normal p-3 align-top"
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
