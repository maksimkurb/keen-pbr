import type { getConfigResponse } from "@/api/generated/keen-api"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { ConfigStateResponseListRefreshState } from "@/api/generated/model/configStateResponseListRefreshState"
import type { Outbound } from "@/api/generated/model/outbound"
import type { ResolverIntegrationMode } from "@/api/generated/model/resolverIntegrationMode"

export function selectConfig(
  response?: getConfigResponse
): ConfigObject | undefined {
  if (!response || response.status !== 200) {
    return undefined
  }

  return response.data.config
}

export function selectConfigIsDraft(response?: getConfigResponse): boolean {
  if (!response || response.status !== 200) {
    return false
  }

  return response.data.is_draft
}

export function selectListRefreshState(
  response?: getConfigResponse
): ConfigStateResponseListRefreshState {
  if (!response || response.status !== 200) {
    return {}
  }

  return response.data.list_refresh_state ?? {}
}

export function selectOutbounds(config?: ConfigObject | null): Outbound[] {
  return config?.outbounds ?? []
}

export function findOutboundByTag(
  config: ConfigObject | null | undefined,
  tag: string
): Outbound | undefined {
  return selectOutbounds(config).find((outbound) => outbound.tag === tag)
}

// The saved value, or "none". Configs written before 3.4.0 get "dnsmasq"
// filled in by the daemon when it loads them, so nothing is inferred here.
export function effectiveResolverIntegration(
  config?: ConfigObject | null
): ResolverIntegrationMode {
  return config?.dns?.resolver_integration ?? "none"
}
