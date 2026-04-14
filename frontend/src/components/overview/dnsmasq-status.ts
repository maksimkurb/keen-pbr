import type {
  ResolverConfigSyncState,
  RuntimeOutboundStatus,
} from "@/api/generated/model"

export type DnsmasqBadgeState = {
  labelKey:
    | "overview.runtime.dnsmasqHealthy"
    | "overview.runtime.dnsmasqWaiting"
    | "overview.runtime.dnsmasqStale"
    | "overview.runtime.dnsmasqUnavailable"
    | "overview.runtime.dnsmasqUnknown"
  tone: "healthy" | "warning" | "degraded"
}

export function getDnsmasqBadgeState(
  liveStatus: RuntimeOutboundStatus | undefined,
  syncState: ResolverConfigSyncState | undefined
): DnsmasqBadgeState {
  if (liveStatus === "healthy") {
    if (syncState === "converging") {
      return {
        labelKey: "overview.runtime.dnsmasqWaiting",
        tone: "warning",
      }
    }

    if (syncState === "stale") {
      return {
        labelKey: "overview.runtime.dnsmasqStale",
        tone: "warning",
      }
    }

    return {
      labelKey: "overview.runtime.dnsmasqHealthy",
      tone: "healthy",
    }
  }

  if (liveStatus === "degraded" || liveStatus === "unavailable") {
    return {
      labelKey: "overview.runtime.dnsmasqUnavailable",
      tone: "degraded",
    }
  }

  return {
    labelKey: "overview.runtime.dnsmasqUnknown",
    tone: "warning",
  }
}
