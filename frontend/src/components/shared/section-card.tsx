import type { ReactNode } from "react"

import {
  Card,
  CardContent,
  CardHeader,
  CardTitle,
} from "@/components/ui/card"

export function SectionCard({
  title,
  children,
  action,
}: {
  title: string
  children: ReactNode
  action?: ReactNode
}) {
  return (
    <Card className="border border-slate-200 bg-white shadow-sm">
      <CardHeader className="mb-1">
        <div className="flex items-center justify-between gap-3">
          <CardTitle className="text-2xl font-semibold">{title}</CardTitle>
          {action}
        </div>
      </CardHeader>
      <CardContent className="space-y-3">{children}</CardContent>
    </Card>
  )
}
