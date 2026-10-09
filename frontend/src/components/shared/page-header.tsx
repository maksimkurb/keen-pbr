import type { ReactNode } from "react"

import { cn } from "@/lib/utils"

export function PageHeader({
  title,
  description,
  actions,
  className,
}: {
  title: ReactNode
  description?: string
  actions?: ReactNode
  className?: string
}) {
  return (
    <header
      className={cn(
        "mb-6 grid grid-cols-1 gap-x-4 gap-y-2 border-b pb-4 md:mb-8 md:grid-cols-[minmax(0,1fr)_auto] md:items-start",
        className
      )}
    >
      <h1
        className="min-w-0 text-3xl font-semibold tracking-tight text-balance md:text-2xl"
        id="page-title"
      >
        {title}
      </h1>
      {actions ? (
        <div className="order-3 min-w-0 pt-2 md:order-none md:pt-0">
          {actions}
        </div>
      ) : null}
      {description ? (
        <p className="min-w-0 text-base text-pretty text-muted-foreground md:col-span-2 md:text-sm">
          {description}
        </p>
      ) : null}
    </header>
  )
}
