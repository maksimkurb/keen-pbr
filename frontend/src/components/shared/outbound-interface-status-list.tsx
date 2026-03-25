import { Badge } from "@/components/ui/badge"

export type OutboundInterfaceStatusItem = {
  name: string
  tone: "healthy" | "degraded" | "unknown" | "info"
  latency?: string
  stateLabel?: string
  active?: boolean
  isLast?: boolean
  secondaryLabel?: string
}

export function OutboundInterfaceStatusList({
  items,
  variant = "tree",
}: {
  items: OutboundInterfaceStatusItem[]
  variant?: "tree" | "list"
}) {
  return (
    <div className={variant === "list" ? "space-y-1.5" : undefined}>
      {items.map((item, index) => (
        <OutboundInterfaceStatusRow
          item={item}
          key={`${item.name}-${index}`}
          variant={variant}
        />
      ))}
    </div>
  )
}

function OutboundInterfaceStatusRow({
  item,
  variant,
}: {
  item: OutboundInterfaceStatusItem
  variant: "tree" | "list"
}) {
  return (
    <div
      className={
        variant === "tree"
          ? "ml-1 flex flex-wrap items-center text-base md:text-sm"
          : "flex flex-wrap items-center gap-y-1 text-sm"
      }
    >
      {variant === "tree" ? <TreeConnector isLast={item.isLast ?? false} /> : null}
      <span
        className={`relative ml-2 inline-flex size-2 rounded-full ${
          item.tone === "healthy"
            ? "bg-success"
            : item.tone === "info"
              ? "bg-sky-500"
            : item.tone === "degraded"
              ? "bg-destructive"
              : "bg-warning"
        }`}
      />
      <span className="ml-2 font-medium">{item.name}</span>
      {item.secondaryLabel ? (
        <span className="ml-2 text-muted-foreground">{item.secondaryLabel}</span>
      ) : null}
      {item.latency ? (
        <span className="ml-2 text-muted-foreground">{item.latency}</span>
      ) : null}
      {item.stateLabel ? (
        <span className="ml-2">
          <Badge
            size="xs"
            variant={
              item.active
                ? "success"
                : item.tone === "healthy"
                  ? "secondary"
                  : item.tone === "info"
                    ? "outline"
                  : item.tone === "degraded"
                    ? "destructive"
                    : "outline"
            }
          >
            {item.stateLabel}
          </Badge>
        </span>
      ) : null}
    </div>
  )
}

function TreeConnector({ isLast }: { isLast: boolean }) {
  return (
    <svg
      aria-hidden="true"
      className="mr-0.5 h-full shrink-0 self-stretch"
      preserveAspectRatio="none"
      viewBox="0 0 16 24"
      width="16"
    >
      {isLast ? (
        <path
          d="M2 0V12H14"
          fill="none"
          stroke="currentColor"
          strokeOpacity="0.3"
          strokeWidth="1"
        />
      ) : (
        <path
          d="M2 0V24M2 12H14"
          fill="none"
          stroke="currentColor"
          strokeOpacity="0.3"
          strokeWidth="1"
        />
      )}
    </svg>
  )
}
