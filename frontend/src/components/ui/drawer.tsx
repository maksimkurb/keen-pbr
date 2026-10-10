import type { ComponentProps } from "react"
import { DrawerPreview as DrawerPrimitive } from "@base-ui/react/drawer"
import { cn } from "@/lib/utils"

const Drawer = DrawerPrimitive.Root
const DrawerTrigger = DrawerPrimitive.Trigger
const DrawerTitle = DrawerPrimitive.Title
const DrawerDescription = DrawerPrimitive.Description

function DrawerContent({
  className,
  children,
  container,
  insetClassName,
  ...props
}: ComponentProps<typeof DrawerPrimitive.Popup> & {
  container?: ComponentProps<typeof DrawerPrimitive.Portal>["container"]
  insetClassName?: string
}) {
  return (
    <DrawerPrimitive.Portal container={container}>
      <DrawerPrimitive.Backdrop
        className={cn(
          "fixed inset-0 z-50 bg-black/20 transition-opacity duration-200 data-ending-style:opacity-0 data-starting-style:opacity-0 motion-reduce:transition-none",
          insetClassName
        )}
      />
      <DrawerPrimitive.Viewport
        className={cn("fixed inset-0 z-50 flex items-end", insetClassName)}
      >
        <DrawerPrimitive.Popup
          data-slot="drawer-content"
          className={cn(
            "flex max-h-[85dvh] w-full [transform:translateY(var(--drawer-swipe-movement-y,0px))] flex-col rounded-t-2xl bg-background shadow-lg transition-transform duration-250 ease-out outline-none data-ending-style:[transform:translateY(100%)] data-starting-style:[transform:translateY(100%)] data-swiping:transition-none motion-reduce:transition-none",
            className
          )}
          {...props}
        >
          <div
            aria-hidden="true"
            className="flex h-8 shrink-0 items-center justify-center"
          >
            <div className="h-1 w-10 rounded-full bg-current/30" />
          </div>
          <DrawerPrimitive.Content className="min-h-0 overflow-y-auto overscroll-contain p-4 pt-0 pb-[calc(env(safe-area-inset-bottom)+1rem)]">
            {children}
          </DrawerPrimitive.Content>
        </DrawerPrimitive.Popup>
      </DrawerPrimitive.Viewport>
    </DrawerPrimitive.Portal>
  )
}

export { Drawer, DrawerTrigger, DrawerContent, DrawerTitle, DrawerDescription }
