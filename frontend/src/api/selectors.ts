import type { ConfigObject } from "@/api/generated/model/configObject"
import type { Outbound } from "@/api/generated/model/outbound"

export function selectOutbounds(config?: ConfigObject | null): Outbound[] {
  return config?.outbounds ?? []
}

export function findOutboundByTag(
  config: ConfigObject | null | undefined,
  tag: string
): Outbound | undefined {
  return selectOutbounds(config).find((outbound) => outbound.tag === tag)
}
