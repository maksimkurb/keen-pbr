import type { ReactNode } from "react"

import { IconButtonWithTooltip } from "@/components/shared/icon-button-with-tooltip"

export function ActionButtons({
  actions,
}: {
  actions: Array<{
    label: string
    icon?: ReactNode
    variant?: "ghost" | "outline"
    disabled?: boolean
    onClick?: () => void
  }>
}) {
  return (
    <div className="ml-auto inline-flex justify-end gap-2">
      {actions.map((action) => (
        <IconButtonWithTooltip
          key={action.label}
          disabled={action.disabled}
          label={action.label}
          onClick={action.onClick}
          size="icon-sm"
          variant="ghost"
        >
          {action.icon}
        </IconButtonWithTooltip>
      ))}
    </div>
  )
}
