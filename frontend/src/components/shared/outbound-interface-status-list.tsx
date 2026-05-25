import type { ReactNode } from "react"

import { Badge } from "@/components/ui/badge"

export type OutboundInterfaceStatusItem = {
  name: string
  tone: RuntimeStatusTone
  content?: ReactNode
  latency?: string
  stateLabel?: string
  active?: boolean
  isLast?: boolean
  secondaryLabel?: string
}

export type RuntimeStatusTone = "healthy" | "degraded" | "unknown" | "info"

export function RuntimeStateBadge({
  active,
  label,
  tone,
}: {
  active: boolean
  label: string
  tone: RuntimeStatusTone
}) {
  return (
    <Badge
      size="xs"
      variant={
        active
          ? "success"
          : tone === "healthy"
            ? "secondary"
            : tone === "info"
              ? "outline"
              : tone === "degraded"
                ? "destructive"
                : "outline"
      }
    >
      {label}
    </Badge>
  )
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
        <RuntimeInterfaceStatusRow
          item={item}
          key={`${item.name}-${index}`}
          variant={variant}
        />
      ))}
    </div>
  )
}

export function RuntimeInterfaceStatusRow({
  item,
  variant,
  children,
}: {
  item: OutboundInterfaceStatusItem
  variant: "tree" | "list"
  children?: ReactNode
}) {
  const latency = item.latency ? (
    <span className="text-xs text-muted-foreground">{item.latency}</span>
  ) : null
  const stateBadge = item.stateLabel ? (
    <RuntimeStateBadge
      active={item.active ?? false}
      label={item.stateLabel}
      tone={item.tone}
    />
  ) : null

  return (
    <div
      className={
        variant === "tree"
          ? "ml-1 grid grid-cols-[auto_auto_minmax(0,1fr)] items-start gap-x-2 text-sm"
          : "grid grid-cols-[auto_minmax(0,1fr)] items-start gap-x-2 gap-y-1 text-sm"
      }
    >
      {variant === "tree" ? (
        <TreeConnector isLast={item.isLast ?? false} />
      ) : null}
      <span
        className={`relative mt-1.5 inline-flex size-2 shrink-0 rounded-full ${getToneDotClass(item.tone)}`}
      />
      {children ? (
        <span className="flex min-w-0 flex-wrap items-center gap-2">
          {children}
          {latency}
          {stateBadge}
        </span>
      ) : item.content ? (
        <span className="flex min-w-0 flex-wrap items-center gap-2">
          {item.content}
          {latency}
          {stateBadge}
        </span>
      ) : (
        <span className="flex min-w-0 flex-wrap items-center gap-2">
          <span className="font-medium">{item.name}</span>
          {item.secondaryLabel ? (
            <span className="text-muted-foreground">{item.secondaryLabel}</span>
          ) : null}
          {latency}
          {stateBadge}
        </span>
      )}
    </div>
  )
}

function getToneDotClass(tone: RuntimeStatusTone) {
  switch (tone) {
    case "healthy":
      return "bg-success"
    case "info":
      return "bg-sky-500"
    case "degraded":
      return "bg-destructive"
    case "unknown":
      return "bg-warning"
  }
}

function TreeConnector({ isLast }: { isLast: boolean }) {
  return (
    <span
      aria-hidden="true"
      className="relative h-full min-h-6 w-4 shrink-0 self-stretch text-foreground/30"
    >
      <span
        className={`absolute top-0 left-0 w-px bg-current ${isLast ? "h-3" : "h-full"}`}
      />
      <span className="absolute top-3 left-0 h-px w-3 bg-current" />
    </span>
  )
}
