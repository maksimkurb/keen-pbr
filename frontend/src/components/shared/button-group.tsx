import type { ReactNode } from "react"

import { cn } from "@/lib/utils"

export function ButtonGroup({
  children,
  className,
}: {
  children: ReactNode
  className?: string
}) {
  return (
    <div
      className={cn(
        "flex w-full items-center gap-0 overflow-hidden rounded-md border border-border [&>[data-slot=button]]:flex-1 [&>[data-slot=button]]:rounded-none [&>[data-slot=button]]:border-0 [&>[data-slot=button]]:shadow-none",
        className
      )}
      data-slot="button-group"
    >
      {children}
    </div>
  )
}
