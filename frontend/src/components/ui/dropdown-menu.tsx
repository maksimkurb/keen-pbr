import type { ComponentProps } from "react"
import { Menu } from "@base-ui/react/menu"
import { cn } from "@/lib/utils"

const DropdownMenu = Menu.Root
const DropdownMenuTrigger = Menu.Trigger

function DropdownMenuContent({
  className,
  children,
  ...props
}: ComponentProps<typeof Menu.Popup>) {
  return (
    <Menu.Portal>
      <Menu.Positioner
        align="end"
        sideOffset={4}
        collisionPadding={8}
        className="z-50 max-w-[calc(100vw-1rem)]"
      >
        <Menu.Popup
          className={cn(
            "max-h-(--available-height) w-56 max-w-[min(var(--available-width),calc(100vw-1rem))] overflow-y-auto rounded-lg border bg-popover p-1 text-popover-foreground shadow-md outline-none",
            className
          )}
          {...props}
        >
          {children}
        </Menu.Popup>
      </Menu.Positioner>
    </Menu.Portal>
  )
}

function DropdownMenuItem({
  className,
  destructive = false,
  ...props
}: ComponentProps<typeof Menu.Item> & { destructive?: boolean }) {
  return (
    <Menu.Item
      className={cn(
        "flex min-h-11 w-full cursor-pointer items-center gap-2 rounded-md px-3 py-2 text-left text-sm outline-none select-none hover:bg-accent active:bg-accent data-highlighted:bg-accent data-disabled:pointer-events-none data-disabled:opacity-50 [&_svg]:size-4 [&_svg]:shrink-0",
        destructive
          ? "text-destructive"
          : "hover:text-accent-foreground active:text-accent-foreground data-highlighted:text-accent-foreground",
        className
      )}
      {...props}
    />
  )
}

export {
  DropdownMenu,
  DropdownMenuTrigger,
  DropdownMenuContent,
  DropdownMenuItem,
}
