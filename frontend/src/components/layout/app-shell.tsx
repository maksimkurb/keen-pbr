import type { ReactNode } from "react"
import { MenuIcon } from "lucide-react"

import { AppSidebar } from "@/components/app-sidebar"
import logoUrl from "@/assets/logo.svg"
import { WarningBanner } from "@/components/layout/warning-banner"
import { Button } from "@/components/ui/button"
import {
  SidebarInset,
  SidebarProvider,
  useSidebar,
} from "@/components/ui/sidebar"

export function AppShell({ children }: { children: ReactNode }) {
  return (
    <SidebarProvider defaultOpen={true}>
      <div className="flex min-h-screen bg-muted/20">
        <AppSidebar />
        <SidebarInset className="min-w-0">
          <MobileSidebarHeader />
          <main className="min-w-0 flex-1">
            <div className="mx-auto max-w-7xl px-4 py-6 md:px-6">
              <WarningBanner />
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
    <div className="border-b bg-background px-4 py-2 md:hidden">
      <Button
        className="h-auto w-full justify-start gap-3 px-0 py-0 text-left text-foreground shadow-none hover:bg-transparent"
        onClick={toggleSidebar}
        variant="ghost"
      >
        <div className="flex size-8 cursor-pointer items-center justify-center rounded-md border bg-muted text-muted-foreground">
          <MenuIcon className="h-4 w-4" />
        </div>
        <div className="flex size-8 items-center justify-center overflow-hidden rounded-md bg-[#1A2D35] p-1">
          <img alt="keen-pbr logo" className="size-full object-contain" src={logoUrl} />
        </div>
        <div className="grid flex-1 text-left leading-tight">
          <span className="text-sm font-medium">keen-pbr</span>
          <span className="text-[11px] text-muted-foreground">Router control</span>
        </div>
      </Button>
    </div>
  )
}
