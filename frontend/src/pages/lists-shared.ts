export type ListDraft = {
  name: string
  ttlMs: string
  domains: string
  ipCidrs: string
  url: string
  file: string
}

export type ListItem = {
  id: string
  draft: ListDraft
  locationLabel: string
  locationIcon?: "external"
  typeVariant?: "default" | "secondary" | "outline"
  rule: string
  stats: {
    totalHosts: number
    ipv4Subnets: number
    ipv6Subnets: number
  }
  canRefresh?: boolean
}

export const sampleNewList: ListDraft = {
  name: "",
  ttlMs: "300000",
  domains: "",
  ipCidrs: "",
  url: "",
  file: "",
}

export const listItems: ListItem[] = [
  {
    id: "direct",
    draft: {
      name: "direct",
      ttlMs: "300000",
      domains: "example.com",
      ipCidrs: "93.184.216.34",
      url: "",
      file: "/opt/etc/direct.txt",
    },
    locationLabel: "/opt/etc/direct.txt",
    locationIcon: "external",
    typeVariant: "secondary",
    rule: "kdirect",
    stats: { totalHosts: 1, ipv4Subnets: 0, ipv6Subnets: 0 },
  },
  {
    id: "vpn-local",
    draft: {
      name: "vpn-local",
      ttlMs: "300000",
      domains: "corp.internal",
      ipCidrs: "10.0.0.0/8\nfd00::/8",
      url: "",
      file: "",
    },
    locationLabel: "builtin",
    typeVariant: "outline",
    rule: "kvpn",
    stats: { totalHosts: 104, ipv4Subnets: 10, ipv6Subnets: 5 },
  },
  {
    id: "signal",
    draft: {
      name: "signal",
      ttlMs: "300000",
      domains: "signal.org",
      ipCidrs: "",
      url: "https://example.com/signal.txt",
      file: "",
    },
    locationLabel: "Updated 5 minutes ago",
    rule: "kvpn",
    stats: { totalHosts: 8, ipv4Subnets: 0, ipv6Subnets: 0 },
    canRefresh: true,
  },
]

export function getListDraft(listId?: string) {
  if (!listId) {
    return null
  }

  const list = listItems.find((item) => item.id === listId)
  return list ? list.draft : null
}

export function getListSourceLabel(draft: ListDraft) {
  const sources = [
    draft.url ? "url" : null,
    draft.file ? "file" : null,
    draft.domains ? "domains" : null,
    draft.ipCidrs ? "ip_cidrs" : null,
  ].filter(Boolean)

  if (sources.length === 0) {
    return "empty"
  }

  if (sources.length === 1) {
    return sources[0]
  }

  return "combined"
}
