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
  return (
    <div className="overflow-hidden rounded-md border border-slate-200">
      <Table className="min-w-[760px]">
        <TableHeader className="bg-slate-50">
          <TableRow>
            {headers.map((header) => (
              <TableHead className="font-semibold" key={header}>
                {header}
              </TableHead>
            ))}
          </TableRow>
        </TableHeader>
        <TableBody>
          {rows.map((row, index) => (
            <TableRow key={`${row[0]}-${index}`}>
              {row.map((cell, cellIndex) => (
                <TableCell className="align-top whitespace-normal" key={cellIndex}>
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
