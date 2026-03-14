import type { ReactNode } from "react"

import { ArrowDown, ArrowUp } from "lucide-react"

import { Button } from "@/components/ui/button"

export function OrderedGroupCard({
  title,
  description,
  canMoveUp = true,
  canMoveDown = true,
  canRemove = true,
  onMoveUp,
  onMoveDown,
  onRemove,
  children,
}: {
  title: ReactNode
  description?: ReactNode
  canMoveUp?: boolean
  canMoveDown?: boolean
  canRemove?: boolean
  onMoveUp?: () => void
  onMoveDown?: () => void
  onRemove?: () => void
  children: ReactNode
}) {
  return (
    <div className="rounded-xl border border-border p-4">
      <div className="mb-4 flex items-start justify-between gap-3">
        <div className="space-y-1">
          <div className="text-sm font-medium md:text-xs">{title}</div>
          {description ? (
            <div className="text-sm text-muted-foreground md:text-xs">{description}</div>
          ) : null}
        </div>
        <div className="flex gap-2">
          <Button
            disabled={!canMoveUp}
            onClick={onMoveUp}
            size="sm"
            type="button"
            variant="outline"
          >
            <ArrowUp className="h-4 w-4" />
            Up
          </Button>
          <Button
            disabled={!canMoveDown}
            onClick={onMoveDown}
            size="sm"
            type="button"
            variant="outline"
          >
            <ArrowDown className="h-4 w-4" />
            Down
          </Button>
          <Button
            disabled={!canRemove}
            onClick={onRemove}
            size="sm"
            type="button"
            variant="outline"
          >
            Remove
          </Button>
        </div>
      </div>
      {children}
    </div>
  )
}
