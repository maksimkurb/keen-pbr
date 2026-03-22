"use client"

import type { ComponentProps } from "react"
import { LayoutGridIcon, ShieldIcon, WaypointsIcon } from "lucide-react"
import { useTranslation } from "react-i18next"

import { LanguageSelector } from "@/components/language-selector"
import { AppBrandHeader } from "@/components/layout/app-brand-header"
import { ThemeSelector } from "@/components/theme-selector"
import { WarningBanner } from "@/components/layout/warning-banner"
import { NavMain } from "@/components/nav-main"
import {
  Sidebar,
  SidebarContent,
  SidebarFooter,
  SidebarHeader,
} from "@/components/ui/sidebar"
import { useSidebar } from "@/components/ui/sidebar-context"

export function AppSidebar(props: ComponentProps<typeof Sidebar>) {
  const { isMobile, toggleSidebar } = useSidebar()
  const { t } = useTranslation()

  const data = {
    navMain: [
      {
        title: t("nav.groups.general"),
        url: "#",
        icon: LayoutGridIcon,
        items: [
          {
            title: t("nav.items.systemMonitor"),
            url: "/",
          },
          {
            title: t("nav.items.settings"),
            url: "/general",
          },
        ],
      },
      {
        title: t("nav.groups.internet"),
        url: "#",
        icon: WaypointsIcon,
        items: [
          {
            title: t("nav.items.outbounds"),
            url: "/outbounds",
          },
          {
            title: t("nav.items.dnsServers"),
            url: "/dns-servers",
          },
        ],
      },
      {
        title: t("nav.groups.networkRules"),
        url: "#",
        icon: ShieldIcon,
        items: [
          {
            title: t("nav.items.lists"),
            url: "/lists",
          },
          {
            title: t("nav.items.routingRules"),
            url: "/routing-rules",
          },
          {
            title: t("nav.items.dnsRules"),
            url: "/dns-rules",
          },
        ],
      },
    ],
  }

  return (
    <Sidebar collapsible="offcanvas" {...props}>
      <SidebarHeader className={isMobile ? "border-b px-4 py-2" : "border-b"}>
        <SidebarMenuHeader isMobile={isMobile} onMenuClick={toggleSidebar} />
      </SidebarHeader>
      <SidebarContent>
        {!isMobile ? <WarningBanner className="mx-2 mt-2 mb-0 w-auto" compact /> : null}
        <NavMain items={data.navMain} />
      </SidebarContent>
      <SidebarFooter className={isMobile ? "border-t px-4 py-3" : "border-t"}>
        <div className="space-y-3">
          <LanguageSelector />
          <ThemeSelector />
        </div>
      </SidebarFooter>
    </Sidebar>
  )
}

function SidebarMenuHeader({
  isMobile,
  onMenuClick,
}: {
  isMobile: boolean
  onMenuClick: () => void
}) {
  if (isMobile) {
    return <AppBrandHeader onMenuClick={onMenuClick} />
  }

  return <AppBrandHeader />
}
