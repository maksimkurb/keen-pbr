export type PageKey =
  | "overview"
  | "general"
  | "lists"
  | "outbounds"
  | "dns"
  | "routing-rules"

export type NavItem = {
  key: PageKey
  label: string
  path: string
  group: "main" | "config"
}

export const navItems: NavItem[] = [
  { key: "overview", label: "Dashboard", path: "/", group: "main" },
  { key: "general", label: "General", path: "/general", group: "config" },
  { key: "lists", label: "Lists", path: "/lists", group: "config" },
  { key: "outbounds", label: "Outbounds", path: "/outbounds", group: "config" },
  { key: "dns", label: "DNS", path: "/dns", group: "config" },
  { key: "routing-rules", label: "Routes", path: "/routing-rules", group: "config" },
]
