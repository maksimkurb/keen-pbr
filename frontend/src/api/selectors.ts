import type { getConfigResponse } from "@/api/generated/keen-api"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { Outbound } from "@/api/generated/model/outbound"

export function selectConfig(response?: getConfigResponse): ConfigObject | undefined {
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

export function selectOutbounds(config?: ConfigObject | null): Outbound[] {
  return config?.outbounds ?? []
}

export function findOutboundByTag(
  config: ConfigObject | null | undefined,
  tag: string
): Outbound | undefined {
  return selectOutbounds(config).find((outbound) => outbound.tag === tag)
}
