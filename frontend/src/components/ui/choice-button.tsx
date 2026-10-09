import type { ComponentProps } from "react"
import { RadioGroupItem } from "@/components/ui/radio-group"
import { cn } from "@/lib/utils"

export function ChoiceButton({
  className,
  value,
  disabled,
  children,
  ...props
}: ComponentProps<"button">) {
  const classes = cn(
    "relative grid min-w-0 cursor-pointer grid-cols-[auto_minmax(0,1fr)] items-center gap-x-2 gap-y-1 rounded-xl border bg-card p-3 text-left transition-colors hover:bg-muted/50 focus-visible:ring-3 focus-visible:ring-ring/50 aria-pressed:border-primary aria-pressed:bg-primary/5 aria-pressed:ring-1 aria-pressed:ring-primary sm:flex sm:flex-col sm:items-start sm:gap-2 sm:p-4 [&>:nth-child(3)]:col-span-2 [&>svg]:size-4 sm:[&>svg]:size-5",
    className
  )
  if (typeof value === "string") {
    return (
      <label
        className={cn(
          classes,
          "pr-9 has-focus-visible:ring-3 has-focus-visible:ring-ring/50 has-data-checked:border-primary has-data-checked:bg-primary/5 has-data-checked:ring-1 has-data-checked:ring-primary has-data-disabled:cursor-not-allowed has-data-disabled:opacity-60 sm:pr-9"
        )}
      >
        {children}
        <RadioGroupItem
          className="absolute top-3 right-3"
          value={value}
          disabled={disabled}
        />
      </label>
    )
  }
  return (
    <button type="button" className={classes} disabled={disabled} {...props}>
      {children}
    </button>
  )
}
