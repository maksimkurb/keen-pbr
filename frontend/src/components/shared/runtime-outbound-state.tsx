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
}: {
  runtimeState?: RuntimeOutboundState
  t: TranslateFn
  variant?: "tree" | "list"
}) {
  if (!runtimeState) {
    return null
  }

  if (runtimeState.type !== "interface" && runtimeState.type !== "urltest") {
    return null
  }

  const items = runtimeState.interfaces.map((interfaceState, index) =>
    mapRuntimeInterfaceToItem(
      interfaceState,
      runtimeState.type,
      index === runtimeState.interfaces.length - 1,
      t
    )
  )

  if (items.length === 0) {
    return null
  }

  return (
    <OutboundInterfaceStatusList
      activeLabel={t("overview.outbounds.inUse")}
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
  const name =
    interfaceState.interface_name?.trim().length
      ? interfaceState.interface_name
      : interfaceState.outbound_tag

  return {
    name,
    tone: getInterfaceTone(interfaceState.status),
    active: interfaceState.status === "active",
    isLast,
    secondaryLabel:
      parentType === "urltest" && interfaceState.outbound_tag !== name
        ? interfaceState.outbound_tag
        : undefined,
    stateLabel: t(`runtime.interfaceStatus.${interfaceState.status}`),
  }
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
