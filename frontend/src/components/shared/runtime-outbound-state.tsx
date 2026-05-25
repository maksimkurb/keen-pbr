import type {
  RuntimeInterfaceInventoryEntry,
  RuntimeInterfaceState,
  RuntimeOutboundState,
} from "@/api/generated/model"
import { InterfaceRowContent } from "@/components/shared/interface-picker"
import {
  OutboundInterfaceStatusList,
  type OutboundInterfaceStatusItem,
  RuntimeStateBadge,
} from "@/components/shared/outbound-interface-status-list"
import { Badge } from "@/components/ui/badge"

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
  return title ? (
    <RuntimeOutboundStatusLabel
      runtimeState={runtimeState}
      t={t}
      title={title}
    />
  ) : null
}

export function RuntimeOutboundStatusLabel({
  runtimeState,
  title,
  t,
}: {
  runtimeState?: RuntimeOutboundState
  title: string
  t: TranslateFn
}) {
  return (
    <div className="flex min-w-0 items-center gap-2">
      <span className="truncate font-medium">{title}</span>
      <Badge
        size="xs"
        variant={
          runtimeState?.status === "healthy"
            ? "success"
            : runtimeState?.status === "degraded"
              ? "warning"
              : runtimeState?.status === "unavailable"
                ? "destructive"
                : "outline"
        }
      >
        {t(`runtime.outboundStatus.${runtimeState?.status ?? "unknown"}`)}
      </Badge>
    </div>
  )
}

export function RuntimeOutboundDetails({
  runtimeState,
  t,
  variant = "list",
  fallbackLabel,
  fallbackTone = "unknown",
  runtimeInterfaces,
}: {
  runtimeState?: RuntimeOutboundState
  t: TranslateFn
  variant?: "tree" | "list"
  fallbackLabel?: string
  fallbackTone?: OutboundInterfaceStatusItem["tone"]
  runtimeInterfaces?: Map<string, RuntimeInterfaceInventoryEntry>
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
            runtimeInterfaces,
            t
          )
        )
      : getRuntimeFallbackItems(fallbackLabel, fallbackTone)

  if (items.length === 0) {
    return null
  }

  return <OutboundInterfaceStatusList items={items} variant={variant} />
}

function mapRuntimeInterfaceToItem(
  interfaceState: RuntimeInterfaceState,
  parentType: RuntimeOutboundState["type"],
  isLast: boolean,
  runtimeInterfaces: Map<string, RuntimeInterfaceInventoryEntry> | undefined,
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
  const inventoryEntry = hasInterfaceName
    ? runtimeInterfaces?.get(interfaceState.interface_name!)
    : undefined
  const latencyLabel =
    typeof interfaceState.latency_ms === "number"
      ? `${interfaceState.latency_ms} ms`
      : undefined
  const content =
    runtimeInterfaces && hasInterfaceName ? (
      <InterfaceRowContent
        afterStatus={
          <>
            <RuntimeStateBadge
              active={interfaceState.status === "active"}
              label={t(`runtime.interfaceStatus.${interfaceState.status}`)}
              tone={getInterfaceTone(interfaceState.status)}
            />
            {latencyLabel ? (
              <span className="text-xs text-muted-foreground">
                {latencyLabel}
              </span>
            ) : null}
          </>
        }
        grow={false}
        interfaceEntry={inventoryEntry}
        isVirtual={!inventoryEntry}
        name={
          parentType === "urltest" && hasInterfaceName
            ? `${interfaceState.outbound_tag} (${interfaceState.interface_name})`
            : name
        }
      />
    ) : undefined

  return {
    name,
    content,
    tone: getInterfaceTone(interfaceState.status),
    active: interfaceState.status === "active",
    isLast,
    latency: content ? undefined : latencyLabel,
    secondaryLabel,
    stateLabel: content
      ? undefined
      : t(`runtime.interfaceStatus.${interfaceState.status}`),
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
