import type { ReactNode } from "react"

import { cn } from "@/lib/utils"

export function PageHeader({
  title,
  description,
  actions,
  className,
}: {
  title: string
  description?: string
  actions?: ReactNode
  className?: string
}) {
  return (
    <header
      className={cn(
        "mb-6 flex flex-col gap-4 md:mb-8 md:flex-row md:items-start md:justify-between",
        className
      )}
    >
      <div className="min-w-0">
        <h1
          className="text-balance text-3xl font-semibold tracking-tight md:text-2xl"
          id="page-title"
        >
          {title}
        </h1>
        {description ? (
          <p className="mt-1 max-w-[60ch] text-pretty text-base text-muted-foreground md:text-sm">
            {description}
          </p>
        ) : null}
      </div>
      {actions}
    </header>
  )
}
