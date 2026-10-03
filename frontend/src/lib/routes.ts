export type PageKey =
  | "overview"
  | "general"
  | "lists"
  | "outbounds"
  | "dns-servers"
  | "routing-rules"

export type NavItem = {
  key: PageKey
  label: string
  path: string
  group: "status" | "internet" | "rules"
}

export const navItems: NavItem[] = [
  { key: "overview", label: "Dashboard", path: "/", group: "status" },
  { key: "general", label: "Settings", path: "/general", group: "status" },
  {
    key: "outbounds",
    label: "Outbounds",
    path: "/outbounds",
    group: "internet",
  },
  {
    key: "dns-servers",
    label: "DNS Servers",
    path: "/dns-servers",
    group: "internet",
  },
  { key: "lists", label: "Lists", path: "/lists", group: "rules" },
  {
    key: "routing-rules",
    label: "Traffic Rules",
    path: "/routing-rules",
    group: "rules",
  },
]
