import { type ReactNode, useState } from "react"
import { ChevronDown, Info } from "lucide-react"

import { Badge } from "@/components/ui/badge"
import {
  Collapsible,
  CollapsibleContent,
  CollapsibleTrigger,
} from "@/components/ui/collapsible"
import { ToggleGroup, ToggleGroupItem } from "@/components/ui/toggle-group"
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip"
import { cn } from "@/lib/utils"

/** Info icon with a tooltip; safe to place inside a <label>. */
export function InfoHint({
  text,
  label,
  className,
}: {
  text: ReactNode
  label: string
  className?: string
}) {
  return (
    <Tooltip>
      <TooltipTrigger
        aria-label={label}
        className={cn(
          "inline-flex cursor-help rounded-full text-muted-foreground outline-none hover:text-primary focus-visible:ring-3 focus-visible:ring-ring/50",
          className
        )}
      >
        <Info className="size-3.5" />
      </TooltipTrigger>
      <TooltipContent className="max-w-64 text-left">{text}</TooltipContent>
    </Tooltip>
  )
}

export function SegmentedControl<T extends string>({
  value,
  options,
  onChange,
  disabled,
  "aria-label": ariaLabel,
}: {
  value: T
  options: ReadonlyArray<{ value: T; label: string }>
  onChange: (value: T) => void
  disabled?: boolean
  "aria-label"?: string
}) {
  return (
    <ToggleGroup
      aria-label={ariaLabel}
      className="flex-wrap"
      disabled={disabled}
      onValueChange={(next) => {
        const selected = next[0] as T | undefined
        if (selected) {
          onChange(selected)
        }
      }}
      spacing={0}
      value={[value]}
      variant="outline"
    >
      {options.map((option) => (
        <ToggleGroupItem
          className="px-3 data-pressed:bg-primary data-pressed:text-primary-foreground data-pressed:hover:bg-primary/90"
          key={option.value}
          value={option.value}
        >
          {option.label}
        </ToggleGroupItem>
      ))}
    </ToggleGroup>
  )
}

export function AdvancedSection({
  title,
  badge,
  changed,
  defaultOpen,
  errorLabel,
  children,
}: {
  title: string
  badge: string
  changed: boolean
  defaultOpen?: boolean
  /** Set when a field inside has an error: the section opens to show it. */
  errorLabel?: string | null
  children: ReactNode
}) {
  const hasError = Boolean(errorLabel)
  const [open, setOpen] = useState(Boolean(defaultOpen))
  const [hadError, setHadError] = useState(hasError)
  if (hasError !== hadError) {
    setHadError(hasError)
    if (hasError) setOpen(true)
  }
  return (
    <Collapsible
      className={cn(
        "rounded-xl border bg-card",
        hasError && "border-destructive/50"
      )}
      onOpenChange={setOpen}
      open={open}
    >
      <CollapsibleTrigger className="group/adv flex w-full cursor-pointer items-center gap-2 rounded-xl px-3.5 py-2.5 text-left text-sm font-medium outline-none hover:bg-muted/50 focus-visible:ring-3 focus-visible:ring-ring/50">
        <ChevronDown className="size-4 text-muted-foreground transition-transform group-data-panel-open/adv:rotate-180" />
        <span className="flex-1">{title}</span>
        <Badge
          size="xs"
          variant={hasError ? "destructive" : changed ? "warning" : "outline"}
        >
          {errorLabel || badge}
        </Badge>
      </CollapsibleTrigger>
      <CollapsibleContent className="border-t px-3.5 py-4">
        {children}
      </CollapsibleContent>
    </Collapsible>
  )
}

export function FormSection({
  className,
  children,
}: {
  className?: string
  children: ReactNode
}) {
  return (
    <section className={cn("space-y-4 rounded-xl border p-4", className)}>
      {children}
    </section>
  )
}
