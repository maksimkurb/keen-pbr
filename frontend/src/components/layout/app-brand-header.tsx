import { MenuIcon } from "lucide-react"

import logoUrl from "@/assets/logo.svg"
import { IconButtonWithTooltip } from "@/components/shared/icon-button-with-tooltip"
import { cn } from "@/lib/utils"

export function AppBrandHeader({
  onMenuClick,
  className = "",
}: {
  onMenuClick?: () => void
  variant?: "sidebar" | "topbar"
  className?: string
}) {
  return (
    <div
      className={cn(
        "flex items-center gap-3 px-0 py-0",
        className
      )}
    >
      {onMenuClick ? (
        <IconButtonWithTooltip
          className="size-8 shrink-0 rounded-md border bg-muted text-muted-foreground shadow-none hover:bg-muted"
          label="Open menu"
          onClick={onMenuClick}
          size="icon"
          variant="ghost"
        >
          <MenuIcon className="h-4 w-4" />
        </IconButtonWithTooltip>
      ) : null}
      <div className="flex size-10 shrink-0 items-center justify-center overflow-hidden rounded-lg border bg-[#1A2D35] p-1.5">
        <img alt="keen-pbr logo" className="size-full object-contain" src={logoUrl} />
      </div>
      <div className="grid min-w-0 flex-1 text-left leading-tight">
        <span className="truncate text-base font-medium">keen-pbr</span>
        <span className="truncate text-sm text-muted-foreground">Get packets sorted</span>
      </div>
    </div>
  )
}
