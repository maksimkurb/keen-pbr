import type { ConfigObject } from "@/api/generated/model/configObject"
import type { DnsServer } from "@/api/generated/model/dnsServer"
import { DnsServerType } from "@/api/generated/model/dnsServerType"

export type DnsServerDeleteImpact = {
  matchingRuleIndexes: number[]
  usesFallback: boolean
  usesBootstrap: boolean
}

export function getDnsServerDeleteReferenceInfo(
  config: ConfigObject,
  serverTags: Iterable<string>
): DnsServerDeleteImpact & {
  matchingRulesCount: number
} {
  const impact = getDnsServerDeleteImpact(config, serverTags)

  return {
    ...impact,
    matchingRulesCount: impact.matchingRuleIndexes.length,
  }
}

export function getDnsServerDeleteImpact(
  config: ConfigObject,
  serverTags: Iterable<string>
): DnsServerDeleteImpact {
  const tagSet = new Set(serverTags)
  const rules = config.dns?.rules ?? []
  const fallback = config.dns?.fallback ?? []
  const bootstrap = config.dns?.bootstrap ?? []

  return {
    matchingRuleIndexes: rules.flatMap((rule, index) =>
      rule.servers.some((tag) => tagSet.has(tag)) ? [index] : []
    ),
    usesFallback: fallback.some((tag) => tagSet.has(tag)),
    usesBootstrap: bootstrap.some((tag) => tagSet.has(tag)),
  }
}

export function buildUpdatedConfigForDnsServersDelete(
  config: ConfigObject,
  serverTags: Iterable<string>,
  cleanupReferences: boolean
): ConfigObject {
  const tagSet = new Set(serverTags)
  const dnsConfig = config.dns
  const rules = dnsConfig?.rules ?? []
  const fallback = dnsConfig?.fallback ?? []
  const dnsWithoutBootstrap = { ...(dnsConfig ?? {}) }
  delete dnsWithoutBootstrap.bootstrap
  // Bootstrap servers are left out when none remain.
  const bootstrap = cleanupReferences
    ? (dnsConfig?.bootstrap ?? []).filter((tag) => !tagSet.has(tag))
    : (dnsConfig?.bootstrap ?? [])

  return {
    ...config,
    dns: {
      ...dnsWithoutBootstrap,
      ...(bootstrap.length > 0 ? { bootstrap } : {}),
      servers: (dnsConfig?.servers ?? []).filter(
        (server) => !tagSet.has(server.tag)
      ),
      // A rule loses the deleted servers; one left without any is removed.
      rules: cleanupReferences
        ? rules.flatMap((rule) => {
            const servers = rule.servers.filter((tag) => !tagSet.has(tag))
            return servers.length > 0 ? [{ ...rule, servers }] : []
          })
        : rules,
      fallback: cleanupReferences
        ? fallback.filter((tag) => !tagSet.has(tag))
        : fallback,
    },
  }
}

/** The servers that can look up the host names of DoH URLs: not DoH ones. */
export function getBootstrapCandidateTags(servers: DnsServer[]): string[] {
  return servers
    .filter((server) => server.type !== DnsServerType.doh)
    .map((server) => server.tag)
    .filter(Boolean)
}

/**
 * What a server is used for besides DNS rules: a main server or a bootstrap
 * server, with its place in the list. `position` is left out when the list
 * has one server; `auto` marks the bootstrap server used when none is chosen.
 */
export type DnsServerRole = {
  kind: "fallback" | "bootstrap"
  position?: number
  auto?: boolean
}

export function getDnsServerRoles(
  tag: string,
  fallback: string[],
  bootstrap: string[],
  bootstrapOptions: string[]
): DnsServerRole[] {
  const roles: DnsServerRole[] = []
  const inList = (kind: DnsServerRole["kind"], list: string[]) => {
    const index = list.indexOf(tag)
    if (index >= 0) {
      roles.push(list.length > 1 ? { kind, position: index + 1 } : { kind })
    }
  }
  inList("fallback", fallback)
  if (bootstrap.length > 0) {
    inList("bootstrap", bootstrap)
  } else if (bootstrapOptions[0] === tag) {
    roles.push({ kind: "bootstrap", auto: true })
  }
  return roles
}

/**
 * DoH servers whose address has to be looked up: the URL names a host and
 * the server has no IP addresses of its own.
 */
export function getDohServersNeedingBootstrap(servers: DnsServer[]): string[] {
  return servers
    .filter((server) => {
      if (server.type !== DnsServerType.doh || (server.ips?.length ?? 0) > 0) {
        return false
      }
      let host: string
      try {
        host = new URL(server.address ?? "").hostname
      } catch {
        return false
      }
      host = host.replace(/^\[|\]$/g, "")
      return !isValidIpv4(host) && !isLikelyIpv6(host)
    })
    .map((server) => server.tag)
}

/**
 * The tabs of the DNS servers page that need attention: no main servers
 * chosen, or DoH servers to look up with no bootstrap servers chosen.
 */
export function getDnsServerListWarnings(config: ConfigObject | undefined): {
  fallback: boolean
  bootstrap: boolean
  dohWithoutIps: string[]
} {
  const dns = config?.dns
  const dohWithoutIps =
    (dns?.bootstrap?.length ?? 0) > 0
      ? []
      : getDohServersNeedingBootstrap(dns?.servers ?? [])
  return {
    fallback: !dns?.fallback?.length,
    bootstrap: dohWithoutIps.length > 0,
    dohWithoutIps,
  }
}

/**
 * Sets `dns.fallback` or `dns.bootstrap`. An empty list is left out of the
 * config instead of being written as `[]`.
 */
export function buildUpdatedConfigWithServerList(
  config: ConfigObject,
  key: "fallback" | "bootstrap",
  tags: string[]
): ConfigObject {
  const dns = { ...(config.dns ?? {}) }
  delete dns[key]
  return {
    ...config,
    dns: {
      ...dns,
      ...(tags.length > 0 ? { [key]: tags } : {}),
    },
  }
}

export function isValidIpv4(value: string) {
  const octets = value.split(".")
  if (octets.length !== 4) {
    return false
  }

  return octets.every((octet) => {
    if (!/^\d+$/.test(octet)) {
      return false
    }

    const num = Number(octet)
    return num >= 0 && num <= 255
  })
}

export function isLikelyIpv6(value: string) {
  if (!/^[0-9a-f:]+$/i.test(value)) {
    return false
  }

  return value.includes(":")
}

/**
 * Splits the "IP addresses of the server" field into addresses. `invalid`
 * says the text is not a list of distinct IPv4/IPv6 addresses without ports.
 */
export function parseServerIps(value: string): {
  ips: string[]
  invalid: boolean
} {
  const ips = value
    .split(/[\s,]+/)
    .map((item) => item.trim().toLowerCase())
    .filter(Boolean)
  const valid = ips.every((ip) => isValidIpv4(ip) || isLikelyIpv6(ip))
  return { ips, invalid: !valid || new Set(ips).size !== ips.length }
}
