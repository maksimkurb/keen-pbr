import type { ReactNode } from "react"

import { Button } from "@/components/ui/button"

export function ActionButtons({
  actions,
}: {
  actions: Array<{ label: string; icon?: ReactNode; variant?: "ghost" | "outline" }>
}) {
  return (
    <div className="flex justify-end gap-2">
      {actions.map((action) => (
        <Button
          key={action.label}
          size="sm"
          title={action.label}
          variant={action.variant ?? "ghost"}
        >
          {action.icon}
        </Button>
      ))}
    </div>
  )
}
