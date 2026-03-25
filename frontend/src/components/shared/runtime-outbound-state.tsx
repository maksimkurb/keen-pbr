import type { RuntimeInterfaceState, RuntimeOutboundState } from "@/api/generated/model"
import {
  OutboundInterfaceStatusList,
  type OutboundInterfaceStatusItem,
} from "@/components/shared/outbound-interface-status-list"
import { Badge } from "@/components/ui/badge"
import { cn } from "@/lib/utils"

type TranslateFn = (key: string, options?: Record<string, unknown>) => string

export function RuntimeOutboundEntry({
  runtimeState,
  title,
  t,
}: {
  runtimeState?: RuntimeOutboundState
  title?: string
  t: TranslateFn
}) {
  if (!runtimeState) {
    return title ? <div className="font-medium">{title}</div> : null
  }

  const supportsDiagnostics =
    runtimeState.type === "interface" || runtimeState.type === "urltest"

  return (
    <div className={cn("max-w-md")}>
      <div className="flex flex-wrap items-center gap-2">
        {title ? <div className="font-medium">{title}</div> : null}
        {supportsDiagnostics ? (
          <Badge
            variant={runtimeState.status === "healthy" ? "success" : "warning"}
          >
            {runtimeState.status === "healthy"
              ? t("runtime.healthy")
              : t("runtime.notHealthy")}
          </Badge>
        ) : null}
      </div>
    </div>
  )
}

export function RuntimeOutboundDetails({
  runtimeState,
  t,
  variant = "list",
  fallbackLabel,
  fallbackTone = "unknown",
}: {
  runtimeState?: RuntimeOutboundState
  t: TranslateFn
  variant?: "tree" | "list"
  fallbackLabel?: string
  fallbackTone?: OutboundInterfaceStatusItem["tone"]
}) {
  if (!runtimeState) {
    return null
  }

  const items =
    runtimeState.type === "interface" || runtimeState.type === "urltest"
      ? runtimeState.interfaces.map((interfaceState, index) =>
          mapRuntimeInterfaceToItem(
            interfaceState,
            runtimeState.type,
            index === runtimeState.interfaces.length - 1,
            t
          )
        )
      : getRuntimeFallbackItems(fallbackLabel, fallbackTone)

  if (items.length === 0) {
    return null
  }

  return (
    <OutboundInterfaceStatusList
      items={items}
      variant={variant}
    />
  )
}

function mapRuntimeInterfaceToItem(
  interfaceState: RuntimeInterfaceState,
  parentType: RuntimeOutboundState["type"],
  isLast: boolean,
  t: TranslateFn
): OutboundInterfaceStatusItem {
  const hasInterfaceName = Boolean(interfaceState.interface_name?.trim().length)
  const name =
    parentType === "urltest"
      ? interfaceState.outbound_tag
      : hasInterfaceName
        ? interfaceState.interface_name!
        : interfaceState.outbound_tag
  const secondaryLabel =
    parentType === "urltest" &&
    hasInterfaceName &&
    interfaceState.interface_name !== interfaceState.outbound_tag
      ? `(${interfaceState.interface_name})`
      : undefined

  return {
    name,
    tone: getInterfaceTone(interfaceState.status),
    active: interfaceState.status === "active",
    isLast,
    latency:
      typeof interfaceState.latency_ms === "number"
        ? `${interfaceState.latency_ms} ms`
        : undefined,
    secondaryLabel,
    stateLabel: t(`runtime.interfaceStatus.${interfaceState.status}`),
  }
}

function getRuntimeFallbackItems(
  fallbackLabel?: string,
  fallbackTone: OutboundInterfaceStatusItem["tone"] = "unknown"
): OutboundInterfaceStatusItem[] {
  if (!fallbackLabel) {
    return []
  }

  return [
    {
      name: fallbackLabel,
      tone: fallbackTone,
      isLast: true,
    },
  ]
}

function getInterfaceTone(
  status: RuntimeInterfaceState["status"]
): "healthy" | "degraded" | "unknown" {
  switch (status) {
    case "active":
      return "healthy"
    case "backup":
      return "healthy"
    case "degraded":
      return "degraded"
    case "unavailable":
      return "degraded"
    case "unknown":
      return "unknown"
  }
}
