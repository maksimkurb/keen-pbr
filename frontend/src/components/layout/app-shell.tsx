import type { ReactNode } from "react"

import { AppSidebar } from "@/components/app-sidebar"
import { AppBrandHeader } from "@/components/layout/app-brand-header"
import {
  WarningBanner,
  useWarningBannerState,
} from "@/components/layout/warning-banner"
import { SidebarInset, SidebarProvider } from "@/components/ui/sidebar"
import { useSidebar } from "@/components/ui/sidebar-context"
import { cn } from "@/lib/utils"

export function AppShell({ children }: { children: ReactNode }) {
  const warningBannerState = useWarningBannerState()

  return (
    <SidebarProvider defaultOpen={true}>
      <div className="flex min-h-screen w-full max-w-full overflow-x-clip bg-muted/20">
        <AppSidebar />
        <SidebarInset className="max-w-full min-w-0 overflow-x-clip">
          <MobileSidebarHeader />
          <main className="min-w-0 flex-1">
            <div
              className={cn(
                "mx-auto max-w-7xl min-w-0 px-4 py-4",
                warningBannerState.isVisible ? "pb-44 md:pb-48" : null
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

  return (
    <div className="bg-background md:hidden">
      <div className="border-b px-4 py-2">
        <AppBrandHeader onMenuClick={toggleSidebar} variant="topbar" />
      </div>
    </div>
  )
}
