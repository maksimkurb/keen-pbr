"use client"

import type { ComponentProps } from "react"
import {
  LayoutGridIcon,
  ShieldIcon,
  WaypointsIcon,
} from "lucide-react"

import logoUrl from "@/assets/logo.svg"
import { NavMain } from "@/components/nav-main"
import {
  Sidebar,
  SidebarContent,
  SidebarHeader,
  SidebarRail,
} from "@/components/ui/sidebar"

const data = {
  navMain: [
    {
      title: "Status",
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
  return (
    <Sidebar collapsible="icon" {...props}>
      <SidebarHeader>
        <SidebarMenuHeader />
      </SidebarHeader>
      <SidebarContent>
        <NavMain items={data.navMain} />
      </SidebarContent>
      <SidebarRail />
    </Sidebar>
  )
}

function SidebarMenuHeader() {
  return (
    <div className="flex items-center gap-3 rounded-lg border bg-sidebar px-3 py-2">
      <div className="flex size-10 items-center justify-center overflow-hidden rounded-lg border bg-[#1A2D35] p-1.5">
        <img alt="keen-pbr logo" className="size-full object-contain" src={logoUrl} />
      </div>
      <div className="grid flex-1 text-left text-sm leading-tight">
        <span className="truncate font-medium">keen-pbr</span>
        <span className="truncate text-xs text-muted-foreground">Router control</span>
      </div>
    </div>
  )
}
