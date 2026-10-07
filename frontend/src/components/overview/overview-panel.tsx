import type { ComponentProps, ReactNode } from "react"

import { cn } from "@/lib/utils"

export type DotTone = "ok" | "warn" | "bad" | "off"

export function Panel({ className, ...props }: ComponentProps<"section">) {
  return (
    <section
      className={cn(
        "min-w-0 overflow-hidden rounded-xl bg-card text-sm text-card-foreground ring-1 ring-foreground/10",
        className
      )}
      {...props}
    />
  )
}

export function PanelHeader({
  title,
  subtitle,
  children,
}: {
  title: ReactNode
  subtitle?: ReactNode
  children?: ReactNode
}) {
  return (
    <div className="flex min-h-11.5 flex-wrap items-center justify-between gap-x-3 gap-y-1 border-b px-3.5 py-2.5">
      <div className="min-w-0">
        <span className="font-semibold">{title}</span>
        {subtitle ? (
          <span className="ml-2 text-xs text-muted-foreground">{subtitle}</span>
        ) : null}
      </div>
      {children ? (
        <div className="flex flex-wrap items-center gap-2">{children}</div>
      ) : null}
    </div>
  )
}

export function StatusDot({
  tone,
  className,
  title,
}: {
  tone: DotTone
  className?: string
  title?: string
}) {
  return (
    <span
      aria-hidden={title ? undefined : true}
      className={cn(
        "inline-block size-2 shrink-0 rounded-full",
        tone === "ok" && "bg-success",
        tone === "warn" && "bg-warning",
        tone === "bad" && "bg-destructive",
        tone === "off" && "bg-muted-foreground/40",
        className
      )}
      role={title ? "img" : undefined}
      title={title}
      aria-label={title}
    />
  )
}

export function PanelNote({
  tone,
  children,
  className,
}: {
  tone: "ok" | "warn" | "bad" | "muted"
  children: ReactNode
  className?: string
}) {
  return (
    <div
      className={cn(
        "flex min-w-0 items-center gap-2 rounded-lg border px-2.5 py-2 text-[13px] font-medium",
        tone === "ok" && "border-success/30 bg-success/5 text-success",
        tone === "warn" &&
          "border-warning/40 bg-warning/10 text-warning-foreground",
        tone === "bad" &&
          "border-destructive/30 bg-destructive/5 text-destructive",
        tone === "muted" && "border-border bg-muted/40 text-muted-foreground",
        className
      )}
    >
      {children}
    </div>
  )
}
