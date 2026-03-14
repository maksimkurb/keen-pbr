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
        "flex w-full flex-nowrap items-stretch gap-0 overflow-hidden rounded-md border border-border [&>*+*]:border-l [&>*+*]:border-border [&>[data-slot=button]]:min-w-0 [&>[data-slot=button]]:rounded-none [&>[data-slot=button]]:border-0 [&>[data-slot=button]]:shadow-none",
        className
      )}
      data-slot="button-group"
    >
      {children}
    </div>
  )
}
