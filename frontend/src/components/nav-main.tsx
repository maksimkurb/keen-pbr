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
  SidebarMenuSubItem,
  useSidebar,
} from "@/components/ui/sidebar"

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
                <div className="flex h-10 items-center gap-3 px-2 text-sm font-medium">
                  {Icon ? <Icon className="size-4 text-primary" /> : null}
                  <span>{item.title}</span>
                </div>
                {hasChildren ? (
                  <SidebarMenuSub className="mx-4">
                    {item.items?.map((subItem) => (
                      <SidebarMenuSubItem key={subItem.title}>
                        <SidebarMenuSubButton
                          className="min-h-10 md:min-h-7"
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
