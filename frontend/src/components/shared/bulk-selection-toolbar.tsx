import type { ReactNode } from "react"

export function BulkSelectionToolbar({
  countLabel,
  children,
}: {
  countLabel: string
  children: ReactNode
}) {
  return (
    <div
      className="flex flex-wrap items-center gap-2 rounded-md border bg-muted/20 px-3 py-2"
      data-testid="bulk-selection-toolbar"
    >
      <span
        aria-atomic="true"
        aria-live="polite"
        className="text-sm font-medium tabular-nums"
        data-testid="bulk-selection-count"
      >
        {countLabel}
      </span>
      {children}
    </div>
  )
}
