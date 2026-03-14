import type { ReactNode } from "react"

import { AppSidebar } from "@/components/app-sidebar"
import { AppBrandHeader } from "@/components/layout/app-brand-header"
import { WarningBanner } from "@/components/layout/warning-banner"
import {
  SidebarInset,
  SidebarProvider,
  useSidebar,
} from "@/components/ui/sidebar"

export function AppShell({ children }: { children: ReactNode }) {
  return (
    <SidebarProvider defaultOpen={true}>
      <div className="flex min-h-screen w-full max-w-full overflow-x-clip bg-muted/20">
        <AppSidebar />
        <SidebarInset className="min-w-0 max-w-full overflow-x-clip">
          <MobileSidebarHeader />
          <main className="min-w-0 flex-1">
            <div className="mx-auto min-w-0 max-w-7xl px-4 py-4">
              {children}
            </div>
          </main>
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
      <div className="px-4 py-2">
        <WarningBanner className="rounded-md" compact />
      </div>
    </div>
  )
}
