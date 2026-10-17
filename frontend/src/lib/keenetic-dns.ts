import type { ApiError } from "@/api/client"
import type { KeeneticDnsProxyUpstream } from "@/api/generated/model/keeneticDnsProxyUpstream"
import { useGetKeeneticDnsProxies } from "@/api/queries"
import { PLATFORM_DEVELOPMENT, PLATFORM_KEENETIC } from "@/lib/platform"

export const KEENETIC_SYSTEM_POLICY = "System"

/** The Keenetic built-in DNS exists on Keenetic builds only. */
export const supportsKeeneticDns =
  import.meta.env.VITE_KEEN_PBR_PLATFORM === PLATFORM_KEENETIC ||
  import.meta.env.VITE_KEEN_PBR_PLATFORM === PLATFORM_DEVELOPMENT

/** Two upstreams are the same one when protocol and target match. */
export function isSameKeeneticUpstream(
  a: KeeneticDnsProxyUpstream,
  b: KeeneticDnsProxyUpstream
): boolean {
  return a.protocol === b.protocol && a.target === b.target
}

/** "DoT 8.8.8.8@dns.google" */
export function keeneticUpstreamLabel(
  upstream: KeeneticDnsProxyUpstream
): string {
  return `${upstream.protocol} ${upstream.target}`
}

/** The Keenetic built-in DNS (System proxy) as the daemon last read it. */
export function useKeeneticSystemProxy(enabled: boolean) {
  const query = useGetKeeneticDnsProxies({
    query: { enabled: enabled && supportsKeeneticDns, staleTime: 30_000 },
  })
  const data = query.data?.status === 200 ? query.data.data : undefined
  return {
    isLoading: query.isLoading,
    proxy: data?.proxies.find((proxy) => proxy.name === KEENETIC_SYSTEM_POLICY),
    fetchedAt: data?.fetched_at,
    error: query.isError
      ? ((query.error as Partial<ApiError> | null)?.message ?? "")
      : (data?.error ?? undefined),
  }
}
