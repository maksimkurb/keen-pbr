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
}: {
  headers: string[]
  rows: ReactNode[][]
}) {
  const lastColumnIndex = headers.length - 1

  return (
    <div className="max-w-full overflow-x-auto rounded-md border">
      <Table className="w-full min-w-[640px] text-base">
        <TableHeader className="bg-muted/50">
          <TableRow>
            {headers.map((header, headerIndex) => (
              <TableHead
                className={headerIndex === lastColumnIndex ? "w-px text-right font-semibold" : "font-semibold"}
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
                      ? "w-px whitespace-nowrap p-3 text-right align-top"
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
