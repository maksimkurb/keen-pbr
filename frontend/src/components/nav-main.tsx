"use client"

import { type LucideIcon } from "lucide-react"
import { useLocation } from "wouter"

import {
  SidebarGroup,
  SidebarGroupContent,
  SidebarMenu,
  SidebarMenuItem,
  SidebarMenuSub,
  SidebarMenuSubButton,
  SidebarMenuSubItem
} from "@/components/ui/sidebar"
import { useSidebar } from "@/components/ui/sidebar-context"

export function NavMain({
  items,
}: {
  items: {
    title: string
    url: string
    icon?: LucideIcon
    items?: {
      title: string
      url: string
    }[]
  }[]
}) {
  const [location, navigate] = useLocation()
  const { isMobile, setOpenMobile } = useSidebar()

  return (
    <SidebarGroup>
      <SidebarGroupContent>
        <SidebarMenu>
          {items.map((item) => {
            const Icon = item.icon
            const hasChildren = Boolean(item.items?.length)

            return (
              <SidebarMenuItem key={item.title}>
                <div className="flex h-11 items-center gap-3 px-2 text-base font-medium md:h-10 md:text-sm">
                  {Icon ? <Icon className="size-4 text-primary" /> : null}
                  <span>{item.title}</span>
                </div>
                {hasChildren ? (
                  <SidebarMenuSub className="mx-4">
                    {item.items?.map((subItem) => (
                      <SidebarMenuSubItem key={subItem.title}>
                        <SidebarMenuSubButton
                          className="min-h-11 text-base md:min-h-7 md:text-sm"
                          href={subItem.url}
                          isActive={location === subItem.url}
                          onClick={(event) => {
                            event.preventDefault()
                            navigate(subItem.url)
                            if (isMobile) {
                              setOpenMobile(false)
                            }
                          }}
                        >
                          <span>{subItem.title}</span>
                        </SidebarMenuSubButton>
                      </SidebarMenuSubItem>
                    ))}
                  </SidebarMenuSub>
                ) : null}
              </SidebarMenuItem>
            )
          })}
        </SidebarMenu>
      </SidebarGroupContent>
    </SidebarGroup>
  )
}
