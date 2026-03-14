import type { ReactNode } from "react"

import { Button } from "@/components/ui/button"
import { Tooltip, TooltipContent, TooltipTrigger } from "@/components/ui/tooltip"

export function IconButtonWithTooltip({
  label,
  children,
  ...buttonProps
}: Omit<React.ComponentProps<typeof Button>, "aria-label" | "children" | "title"> & {
  label: string
  children?: ReactNode
}) {
  return (
    <Tooltip>
      <TooltipTrigger
        render={<Button aria-label={label} {...buttonProps} />}
      >
        {children}
      </TooltipTrigger>
      <TooltipContent>{label}</TooltipContent>
    </Tooltip>
  )
}
