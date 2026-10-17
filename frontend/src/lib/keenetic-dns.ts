import type { ApiError } from "@/api/client"
import type { KeeneticDnsProxy } from "@/api/generated/model/keeneticDnsProxy"
import { useGetKeeneticDnsProxies } from "@/api/queries"
import { PLATFORM_DEVELOPMENT, PLATFORM_KEENETIC } from "@/lib/platform"

export const KEENETIC_SYSTEM_POLICY = "System"

/** Keenetic DNS proxy groups exist on Keenetic builds only. */
export const supportsKeeneticDns =
  import.meta.env.VITE_KEEN_PBR_PLATFORM === PLATFORM_KEENETIC ||
  import.meta.env.VITE_KEEN_PBR_PLATFORM === PLATFORM_DEVELOPMENT

/** "VPN Only (Policy0)", or just the name when the router gave no description. */
export function keeneticProxyLabel(proxy: KeeneticDnsProxy): string {
  return proxy.description
    ? `${proxy.description} (${proxy.name})`
    : proxy.name
}

/** The DNS proxy groups the daemon last read from the router. */
export function useKeeneticDnsProxies(enabled: boolean) {
  const query = useGetKeeneticDnsProxies({
    query: { enabled: enabled && supportsKeeneticDns, staleTime: 30_000 },
  })
  const data = query.data?.status === 200 ? query.data.data : undefined
  return {
    isLoading: query.isLoading,
    proxies: data?.proxies,
    fetchedAt: data?.fetched_at,
    error: query.isError
      ? ((query.error as Partial<ApiError> | null)?.message ?? "")
      : (data?.error ?? undefined),
  }
}
