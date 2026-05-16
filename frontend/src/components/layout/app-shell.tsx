import type { ReactNode } from "react"
import { useTranslation } from "react-i18next"

import { AppSidebar } from "@/components/app-sidebar"
import { AppBrandHeader } from "@/components/layout/app-brand-header"
import { DemoModeBanner } from "@/components/layout/demo-mode-banner"
import { useWarningBannerState } from "@/components/layout/warning-banner-state"
import { WarningBanner } from "@/components/layout/warning-banner"
import { SidebarInset, SidebarProvider } from "@/components/ui/sidebar"
import { useSidebar } from "@/components/ui/sidebar-context"
import { cn } from "@/lib/utils"

export function AppShell({ children }: { children: ReactNode }) {
  const warningBannerState = useWarningBannerState()
  const { t } = useTranslation()

  return (
    <SidebarProvider defaultOpen={true}>
      <div className="flex min-h-screen w-full max-w-full overflow-x-clip bg-muted/20">
        <a
          className="sr-only z-[100] rounded-md px-4 py-2 text-sm font-medium outline-none ring-2 ring-ring ring-offset-2 ring-offset-background transition-[clip,opacity,transform] focus:not-sr-only focus:fixed focus:top-3 focus:left-3 focus:bg-card focus:text-foreground focus:shadow-lg"
          href="#main-content"
        >
          {t("common.skipToMain")}
        </a>
        <AppSidebar />
        <SidebarInset className="max-w-full min-w-0 overflow-x-clip">
          <MobileSidebarHeader />
          <main className="min-w-0 flex-1 outline-none" id="main-content">
            <div
              className={cn(
                "mx-auto max-w-7xl min-w-0 px-4 py-4",
                warningBannerState.isVisible ? "pb-44 md:pb-48" : null
              )}
            >
              <DemoModeBanner />
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
