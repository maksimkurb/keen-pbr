import { useState, type ReactNode, type RefObject } from "react"
import {
  ChevronUp,
  CircleCheckBig,
  CircleX,
  LoaderCircle,
  Save,
} from "lucide-react"

import type { WarningBannerMode } from "@/components/layout/warning-banner-state"
import {
  Drawer,
  DrawerContent,
  DrawerDescription,
  DrawerTitle,
  DrawerTrigger,
} from "@/components/ui/drawer"
import { cn } from "@/lib/utils"

export function MobileWarningPanel({
  containerRef,
  sidebarVisible = false,
  className,
  mode,
  title,
  description,
  children,
}: {
  containerRef?: RefObject<HTMLDivElement | null>
  sidebarVisible?: boolean
  className?: string
  mode: WarningBannerMode
  title: string
  description: string
  children: ReactNode
}) {
  const [open, setOpen] = useState(false)
  const Icon =
    mode === "lifecycle-running"
      ? LoaderCircle
      : mode === "lifecycle-success"
        ? CircleCheckBig
        : mode === "lifecycle-error"
          ? CircleX
          : Save

  return (
    <Drawer open={open} onOpenChange={setOpen} swipeDirection="down">
      <div
        ref={containerRef}
        className={cn(
          "fixed inset-x-0 bottom-0 z-50 border-t border-warning/40 bg-[color-mix(in_srgb,var(--color-warning)_15%,var(--color-background))] pb-[env(safe-area-inset-bottom)] text-warning-foreground shadow-[0_10px_30px_hsl(0_0%_0%/0.18),0_24px_80px_hsl(0_0%_0%/0.4)] ring-1 ring-black/5 dark:ring-white/6",
          sidebarVisible && "left-(--sidebar-width)",
          className
        )}
      >
        <DrawerTrigger
          aria-haspopup="dialog"
          aria-expanded={open}
          render={
            <button
              type="button"
              className={cn(
                "flex min-h-13 w-full items-center gap-3 px-4 py-3 text-left text-sm font-medium focus-visible:outline-2 focus-visible:-outline-offset-2 focus-visible:outline-ring"
              )}
            />
          }
        >
          <Icon
            className={cn(
              "size-4 shrink-0",
              mode === "lifecycle-running" &&
                "animate-spin motion-reduce:animate-none"
            )}
          />
          <span className="min-w-0 flex-1 truncate">{title}</span>
          <ChevronUp className="size-4 shrink-0" />
        </DrawerTrigger>
      </div>
      <DrawerContent
        container={containerRef}
        insetClassName={sidebarVisible ? "left-(--sidebar-width)" : undefined}
        className="bg-background bg-[linear-gradient(to_bottom,color-mix(in_srgb,var(--color-warning)_15%,transparent),transparent_50%)] text-foreground"
      >
        <DrawerTitle className="sr-only">{title}</DrawerTitle>
        <DrawerDescription className="sr-only">{description}</DrawerDescription>
        {children}
      </DrawerContent>
    </Drawer>
  )
}
