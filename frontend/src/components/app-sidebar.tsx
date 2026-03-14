"use client"

import type { ComponentProps } from "react"
import {
  LayoutGridIcon,
  ShieldIcon,
  WaypointsIcon,
} from "lucide-react"

import { AppBrandHeader } from "@/components/layout/app-brand-header"
import { WarningBanner } from "@/components/layout/warning-banner"
import { NavMain } from "@/components/nav-main"
import {
  Sidebar,
  SidebarContent,
  SidebarHeader,
  useSidebar,
} from "@/components/ui/sidebar"

const data = {
  navMain: [
    {
      title: "General",
      url: "#",
      icon: LayoutGridIcon,
      items: [
        {
          title: "System monitor",
          url: "/",
        },
        {
          title: "Settings",
          url: "/general",
        },
      ],
    },
    {
      title: "Internet",
      url: "#",
      icon: WaypointsIcon,
      items: [
        {
          title: "Outbounds",
          url: "/outbounds",
        },
        {
          title: "DNS Servers",
          url: "/dns-servers",
        },
      ],
    },
    {
      title: "Network Rules",
      url: "#",
      icon: ShieldIcon,
      items: [
        {
          title: "Lists",
          url: "/lists",
        },
        {
          title: "Routing rules",
          url: "/routing-rules",
        },
        {
          title: "DNS Rules",
          url: "/dns-rules",
        },
      ],
    },
  ],
}

export function AppSidebar(props: ComponentProps<typeof Sidebar>) {
  const { isMobile, toggleSidebar } = useSidebar()

  return (
    <Sidebar collapsible="offcanvas" {...props}>
      <SidebarHeader className={isMobile ? "border-b px-4 py-2" : "border-b"}>
        <SidebarMenuHeader isMobile={isMobile} onMenuClick={toggleSidebar} />
      </SidebarHeader>
      <SidebarContent>
        {!isMobile ? <WarningBanner className="mx-2 mt-2 mb-0 w-auto" compact /> : null}
        <NavMain items={data.navMain} />
      </SidebarContent>
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
    return <AppBrandHeader onMenuClick={onMenuClick} variant="topbar" />
  }

  return <AppBrandHeader />
}
