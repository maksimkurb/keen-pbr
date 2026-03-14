import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert"
import { cn } from "@/lib/utils"

export function WarningBanner({
  compact = false,
  className,
}: {
  compact?: boolean
  className?: string
}) {
  if (compact) {
    return (
      <Alert
        className={cn(
          "mb-0 px-2 py-1.5 text-warning-foreground border-warning/50 bg-warning/5 [&_[data-slot=alert-title]]:text-xs [&_[data-slot=alert-title]]:font-medium",
          className
        )}
      >
        <AlertTitle>Pending restart. Restart keen-pbr to apply changes.</AlertTitle>
      </Alert>
    )
  }

  return (
    <Alert
      className={cn(
        "border-warning/50 bg-warning/5 text-warning-foreground",
        "mb-6",
        className
      )}
    >
      <AlertTitle>Pending restart</AlertTitle>
      <AlertDescription>
        Configuration has changed. Restart keen-pbr service to apply changes.
      </AlertDescription>
    </Alert>
  )
}
