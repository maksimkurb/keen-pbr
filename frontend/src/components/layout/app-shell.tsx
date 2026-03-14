import type { ReactNode } from "react"

import { TopNav } from "@/components/layout/top-nav"
import { WarningBanner } from "@/components/layout/warning-banner"

export function AppShell({ children }: { children: ReactNode }) {
  return (
    <div className="min-h-svh bg-[#f3f5f8] text-slate-900">
      <TopNav />
      <main className="mx-auto max-w-[1260px] px-4 pb-8 pt-6">
        <WarningBanner />
        {children}
      </main>
    </div>
  )
}
