import type { RuntimeInterfaceState, RuntimeOutboundState } from "@/api/generated/model"
import { Badge } from "@/components/ui/badge"
import { cn } from "@/lib/utils"

type TranslateFn = (key: string, options?: Record<string, unknown>) => string

export function RuntimeOutboundStateSummary({
  runtimeState,
  t,
  compact = false,
}: {
  runtimeState?: RuntimeOutboundState
  t: TranslateFn
  compact?: boolean
}) {
  if (!runtimeState) {
    return (
      <span className="text-sm text-muted-foreground">
        {t("common.noneShort")}
      </span>
    )
  }

  return (
    <div className={cn("space-y-2", compact ? "max-w-sm" : "max-w-md")}>
      <div className="flex flex-wrap items-center gap-2">
        <Badge variant={getOutboundStatusVariant(runtimeState.status)}>
          {t(`runtime.outboundStatus.${runtimeState.status}`)}
        </Badge>
        {runtimeState.active_outbound_tag ? (
          <Badge size="xs" variant="outline">
            {t("runtime.activeOutbound", {
              value: runtimeState.active_outbound_tag,
            })}
          </Badge>
        ) : null}
        {runtimeState.active_interface_name ? (
          <Badge size="xs" variant="outline">
            {t("runtime.activeInterface", {
              value: runtimeState.active_interface_name,
            })}
          </Badge>
        ) : null}
      </div>

      {runtimeState.interfaces.length > 0 ? (
        <div className="flex flex-wrap gap-1.5">
          {runtimeState.interfaces.map((interfaceState) => (
            <InterfaceStatusBadge
              interfaceState={interfaceState}
              key={`${runtimeState.tag}-${interfaceState.outbound_tag}`}
              t={t}
            />
          ))}
        </div>
      ) : null}

      {runtimeState.detail ? (
        <div className="text-xs text-muted-foreground">{runtimeState.detail}</div>
      ) : null}
    </div>
  )
}

export function InterfaceStatusBadge({
  interfaceState,
  t,
}: {
  interfaceState: RuntimeInterfaceState
  t: TranslateFn
}) {
  const label =
    interfaceState.interface_name?.trim().length
      ? interfaceState.interface_name
      : interfaceState.outbound_tag

  const tone = getInterfaceStatusVariant(interfaceState.status)
  const statusLabel = t(`runtime.interfaceStatus.${interfaceState.status}`)

  return (
    <div className="inline-flex max-w-full flex-wrap items-center gap-1 rounded-md border border-border/60 bg-muted/20 px-2 py-1 text-xs">
      <Badge size="xs" variant={tone}>
        {statusLabel}
      </Badge>
      <span className="font-medium text-foreground">{label}</span>
      {interfaceState.outbound_tag !== label ? (
        <span className="text-muted-foreground">{interfaceState.outbound_tag}</span>
      ) : null}
      {interfaceState.detail ? (
        <span className="text-muted-foreground">{interfaceState.detail}</span>
      ) : null}
    </div>
  )
}

function getOutboundStatusVariant(
  status: RuntimeOutboundState["status"]
): "success" | "warning" | "destructive" | "outline" {
  switch (status) {
    case "healthy":
      return "success"
    case "degraded":
      return "warning"
    case "unavailable":
      return "destructive"
    case "unknown":
      return "outline"
  }
}

function getInterfaceStatusVariant(
  status: RuntimeInterfaceState["status"]
): "success" | "secondary" | "warning" | "destructive" | "outline" {
  switch (status) {
    case "active":
      return "success"
    case "available":
      return "secondary"
    case "degraded":
      return "warning"
    case "unavailable":
      return "destructive"
    case "unknown":
      return "outline"
  }
}
