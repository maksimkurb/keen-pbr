import { useEffect, useRef, useState } from "react"
import type { ReactNode } from "react"

import { useLocation } from "wouter"

import { AppSidebar } from "@/components/app-sidebar"
import { AppBrandHeader } from "@/components/layout/app-brand-header"
import { getMobileFabCollapsedState } from "@/components/shared/mobile-add-fab-utils"
import { useWarningBannerState } from "@/components/layout/warning-banner-state"
import { WarningBanner } from "@/components/layout/warning-banner"
import { SidebarInset, SidebarProvider } from "@/components/ui/sidebar"
import { useSidebar } from "@/components/ui/sidebar-context"
import { isFullBleedPath } from "@/lib/routes"
import { cn } from "@/lib/utils"

export function AppShell({ children }: { children: ReactNode }) {
  const warningBannerState = useWarningBannerState()
  const [pathname] = useLocation()
  const fullBleed = isFullBleedPath(pathname)

  return (
    <SidebarProvider defaultOpen={true}>
      <div
        className={cn(
          "flex w-full max-w-full overflow-x-clip bg-muted/20",
          fullBleed ? "h-dvh overflow-y-clip" : "min-h-screen"
        )}
      >
        <a
          className="sr-only z-50 rounded-md bg-background px-3 py-2 text-sm font-medium shadow focus:not-sr-only focus:fixed focus:top-3 focus:left-3"
          href="#main-content"
        >
          Skip to content
        </a>
        <AppSidebar />
        <SidebarInset
          className={cn(
            "max-w-full min-w-0 overflow-x-clip",
            fullBleed ? "min-h-0 overflow-y-clip" : null
          )}
        >
          <MobileSidebarHeader key={pathname} />
          <main
            aria-labelledby="page-title"
            className={cn(
              "min-w-0 flex-1",
              fullBleed ? "flex min-h-0 flex-col" : null
            )}
            id="main-content"
          >
            <div
              className={cn(
                "min-w-0 px-4 py-4",
                fullBleed
                  ? "flex min-h-0 w-full flex-1 flex-col"
                  : "mx-auto max-w-7xl",
                warningBannerState.isVisible
                  ? "pb-[calc(var(--warning-banner-height,3.5rem)+1rem)] min-[1100px]:pb-48"
                  : null
              )}
            >
              {children}
            </div>
          </main>
          <WarningBanner state={warningBannerState} />
        </SidebarInset>
      </div>
    </SidebarProvider>
  )
}

function MobileSidebarHeader() {
  const { toggleSidebar } = useSidebar()
  const [hidden, setHidden] = useState(false)
  const headerRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    let lastChangeY = window.scrollY
    const onScroll = () => {
      if (headerRef.current?.contains(document.activeElement)) {
        setHidden(false)
        return
      }
      const nextHidden = getMobileFabCollapsedState(window.scrollY, lastChangeY)
      if (nextHidden === null) return
      setHidden(nextHidden)
      lastChangeY = window.scrollY
    }

    window.addEventListener("scroll", onScroll, { passive: true })
    return () => window.removeEventListener("scroll", onScroll)
  }, [])

  return (
    <div
      aria-hidden={hidden}
      className={`sticky top-0 z-30 border-b bg-background shadow-sm transition-transform duration-200 motion-reduce:transition-none md:hidden ${hidden ? "-translate-y-full" : "translate-y-0"}`}
      inert={hidden}
      onFocusCapture={() => setHidden(false)}
      ref={headerRef}
    >
      <div className="px-4 pt-[calc(env(safe-area-inset-top)+0.5rem)] pb-2">
        <AppBrandHeader onMenuClick={toggleSidebar} variant="topbar" />
      </div>
    </div>
  )
}
