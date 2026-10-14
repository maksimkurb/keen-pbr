import type { RequestMethod } from "@/lib/request-flags"

export const allRequestMethods: RequestMethod[] = [
  "dns",
  "http",
  "sni",
  "quic",
  "marker",
]

/**
 * keen-pbr actions on a request. A row is shown when any of its actions is
 * checked; a row without any action counts as "none".
 */
export type RequestAction =
  | "listed"
  | "added"
  | "refreshed"
  | "problem"
  | "none"

export const allRequestActions: RequestAction[] = [
  "listed",
  "added",
  "refreshed",
  "problem",
  "none",
]

/** What a DNS reply carried. Every DNS event has exactly one kind. */
export type AnswerKind =
  | "addresses"
  | "blocked"
  | "empty"
  | "nxdomain"
  | "error"

export const allAnswerKinds: AnswerKind[] = [
  "addresses",
  "blocked",
  "empty",
  "nxdomain",
  "error",
]

/** Empty (NODATA) answers are noise for routing, so they start hidden. */
export const defaultAnswerKinds: AnswerKind[] = allAnswerKinds.filter(
  (kind) => kind !== "empty"
)

export type RequestFilters = {
  device: string
  methods: RequestMethod[]
  actions: RequestAction[]
  answers: AnswerKind[]
  domain: string
  ip: string
}

export const emptyRequestFilters: RequestFilters = {
  device: "",
  methods: allRequestMethods,
  actions: allRequestActions,
  answers: defaultAnswerKinds,
  domain: "",
  ip: "",
}

const sameSet = <T>(a: readonly T[], b: readonly T[]) =>
  a.length === b.length && a.every((item) => b.includes(item))

/** Number of checkbox groups (method, action, answer) changed from default. */
export function changedKindGroups(filters: RequestFilters): number {
  return (
    Number(!sameSet(filters.methods, allRequestMethods)) +
    Number(!sameSet(filters.actions, allRequestActions)) +
    Number(!sameSet(filters.answers, defaultAnswerKinds))
  )
}

export function hasActiveFilters(filters: RequestFilters): boolean {
  return (
    filters.device.trim() !== "" ||
    filters.domain.trim() !== "" ||
    filters.ip.trim() !== "" ||
    changedKindGroups(filters) > 0
  )
}

/**
 * Case-insensitive wildcard match: `*` is any run of characters, `?` is one
 * character. A pattern without wildcards is a substring match. An empty
 * pattern matches everything.
 */
export function wildcardMatch(pattern: string, value: string): boolean {
  const p = pattern.trim().toLowerCase()
  if (p === "") return true
  const v = value.toLowerCase()
  if (!/[*?]/.test(p)) return v.includes(p)
  const source = Array.from(p, (ch) =>
    ch === "*"
      ? ".*"
      : ch === "?"
        ? "."
        : ch.replace(/[.+^${}()|[\]\\/-]/g, "\\$&")
  ).join("")
  return new RegExp(`^${source}$`, "s").test(v)
}

type ParsedIp = { version: 4 | 6; value: bigint }

function parseIpv4(text: string): bigint | null {
  const parts = text.split(".")
  if (parts.length !== 4) return null
  let value = 0n
  for (const part of parts) {
    if (!/^\d{1,3}$/.test(part)) return null
    const n = Number(part)
    if (n > 255) return null
    value = (value << 8n) | BigInt(n)
  }
  return value
}

function parseIpv6(input: string): bigint | null {
  const text = input.split("%")[0]
  if (!text.includes(":")) return null
  const halves = text.split("::")
  if (halves.length > 2) return null
  const toGroups = (part: string): number[] | null => {
    if (part === "") return []
    const groups: number[] = []
    const items = part.split(":")
    for (let i = 0; i < items.length; i++) {
      const item = items[i]
      if (item.includes(".")) {
        if (i !== items.length - 1) return null
        const v4 = parseIpv4(item)
        if (v4 === null) return null
        groups.push(Number(v4 >> 16n), Number(v4 & 0xffffn))
      } else {
        if (!/^[0-9a-fA-F]{1,4}$/.test(item)) return null
        groups.push(parseInt(item, 16))
      }
    }
    return groups
  }
  const head = toGroups(halves[0])
  const tail = halves.length === 2 ? toGroups(halves[1]) : []
  if (head === null || tail === null) return null
  let groups: number[]
  if (halves.length === 2) {
    const missing = 8 - head.length - tail.length
    if (missing < 1) return null
    groups = [...head, ...new Array<number>(missing).fill(0), ...tail]
  } else {
    groups = head
  }
  if (groups.length !== 8) return null
  return groups.reduce((acc, g) => (acc << 16n) | BigInt(g), 0n)
}

/** Parses an IPv4 or IPv6 address (IPv6 `::` compression supported). */
export function parseIp(text: string): ParsedIp | null {
  const t = text.trim()
  const v4 = parseIpv4(t)
  if (v4 !== null) return { version: 4, value: v4 }
  const v6 = parseIpv6(t)
  if (v6 !== null) return { version: 6, value: v6 }
  return null
}

export type IpFilter =
  | { kind: "none" }
  | { kind: "invalid" }
  | { kind: "wildcard"; pattern: string }
  | { kind: "cidr"; version: 4 | 6; network: bigint; mask: bigint }

/** Parses the IP filter input: CIDR when it contains `/`, wildcard otherwise. */
export function parseIpFilter(input: string): IpFilter {
  const text = input.trim()
  if (text === "") return { kind: "none" }
  if (!text.includes("/")) return { kind: "wildcard", pattern: text }
  const [addr, prefixText, ...rest] = text.split("/")
  if (rest.length > 0 || !/^\d{1,3}$/.test(prefixText ?? "")) {
    return { kind: "invalid" }
  }
  const ip = parseIp(addr)
  if (ip === null) return { kind: "invalid" }
  const bits = ip.version === 4 ? 32 : 128
  const prefix = Number(prefixText)
  if (prefix > bits) return { kind: "invalid" }
  const hostBits = BigInt(bits - prefix)
  const full = (1n << BigInt(bits)) - 1n
  const mask = (full >> hostBits) << hostBits
  return { kind: "cidr", version: ip.version, network: ip.value & mask, mask }
}

/** True when `ip` satisfies the parsed filter. Invalid/none filters match. */
export function ipMatches(filter: IpFilter, ip: string): boolean {
  if (filter.kind === "none" || filter.kind === "invalid") return true
  if (filter.kind === "wildcard") return wildcardMatch(filter.pattern, ip)
  const parsed = parseIp(ip)
  if (parsed === null || parsed.version !== filter.version) return false
  return (parsed.value & filter.mask) === filter.network
}

/**
 * Answer addresses keen-pbr never learns into sets (blocking or unroutable
 * replies): 0.0.0.0, ::, 127.0.0.0/8 and ::1. Mirrors
 * dns_wire::is_learnable_address.
 */
export function isUnlearnableIp(text: string): boolean {
  const ip = parseIp(text)
  if (ip === null) return false
  if (ip.version === 4) return ip.value === 0n || ip.value >> 24n === 127n
  return ip.value === 0n || ip.value === 1n
}

export type FilterableRow = {
  type?: string
  client_ip?: string
  source?: string
  domain?: string
  lists?: string[]
  ips?: string[]
  qtype?: number
  rcode?: number
  added?: number
  refreshed?: number
  errors?: number
  timed_out?: boolean
}

/** The answer kind of a DNS event; null for L7 events. */
export function answerKind(row: FilterableRow): AnswerKind | null {
  if (row.source !== "dns") return null
  const rcode = row.rcode ?? 0
  if (rcode === 3) return "nxdomain"
  if (rcode !== 0) return "error"
  const ips = row.ips ?? []
  if (ips.length === 0) return "empty"
  return ips.every(isUnlearnableIp) ? "blocked" : "addresses"
}

/** True when the row shows the given keen-pbr action. */
export function hasAction(row: FilterableRow, action: RequestAction): boolean {
  const added = row.added ?? 0
  const refreshed = row.refreshed ?? 0
  const errors = row.errors ?? 0
  switch (action) {
    case "listed":
      return (row.lists ?? []).length > 0
    case "added":
      return added > 0
    case "refreshed":
      return refreshed > 0
    case "problem":
      return errors > 0 || row.timed_out === true
    case "none":
      return allRequestActions.every(
        (other) => other === "none" || !hasAction(row, other)
      )
  }
}

/** Gap rows are never filtered out: lost events may have matched anything. */
export function filterRequests<T extends FilterableRow>(
  rows: T[],
  filters: RequestFilters
): T[] {
  // No early return on default filters: empty answers start hidden.
  const deviceFilter = parseIpFilter(filters.device)
  const ipFilter = parseIpFilter(filters.ip)
  const methods = new Set<string>(filters.methods)
  const answers = new Set<string>(filters.answers)
  const actions = new Set<string>(filters.actions)
  return rows.filter((row) => {
    if (row.type === "GAP") return true
    if (!ipMatches(deviceFilter, row.client_ip ?? "")) return false
    if (
      methods.size < allRequestMethods.length &&
      !methods.has(row.source ?? "")
    ) {
      return false
    }
    if (!wildcardMatch(filters.domain, row.domain ?? "")) return false
    if (ipFilter.kind !== "none" && ipFilter.kind !== "invalid") {
      if (!(row.ips ?? []).some((ip) => ipMatches(ipFilter, ip))) return false
    }
    if (
      actions.size < allRequestActions.length &&
      !filters.actions.some((action) => hasAction(row, action))
    ) {
      return false
    }
    const kind = answerKind(row)
    if (kind !== null && !answers.has(kind)) return false
    return true
  })
}
