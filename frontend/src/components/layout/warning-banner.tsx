import { useGetHealthService, useGetConfig } from "@/api/queries"
import { usePostConfigSaveMutation } from "@/api/mutations"
import { selectConfigIsDraft } from "@/api/selectors"
import {
  Alert,
  AlertAction,
  AlertDescription,
  AlertTitle,
} from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { cn } from "@/lib/utils"

export function WarningBanner({
  compact = false,
  className,
}: {
  compact?: boolean
  className?: string
}) {
  const configQuery = useGetConfig()
  const healthQuery = useGetHealthService({
    query: {
      refetchInterval: 30_000,
      refetchIntervalInBackground: false,
    },
  })
  const postConfigSaveMutation = usePostConfigSaveMutation()

  const isDraft =
    healthQuery.data?.data.config_is_draft ?? selectConfigIsDraft(configQuery.data)

  if (!isDraft) {
    return null
  }

  if (compact) {
    return (
      <Alert
        className={cn(
          "mb-0 px-2 py-1.5 text-warning-foreground border-warning/50 bg-warning/5 [&_[data-slot=alert-title]]:text-xs [&_[data-slot=alert-title]]:font-medium",
          className
        )}
      >
        <AlertTitle>Configuration draft pending save.</AlertTitle>
        <AlertAction className="top-1.5 right-1.5">
          <Button
            disabled={postConfigSaveMutation.isPending}
            onClick={() => postConfigSaveMutation.mutate()}
            size="xs"
            variant="outline"
          >
            {postConfigSaveMutation.isPending ? "Saving..." : "Apply"}
          </Button>
        </AlertAction>
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
      <AlertTitle>Configuration is unsaved</AlertTitle>
      <AlertDescription>
        Configuration has been staged in memory. Save and apply it to persist it
        to disk and reload the service.
      </AlertDescription>
      <AlertAction>
        <Button
          disabled={postConfigSaveMutation.isPending}
          onClick={() => postConfigSaveMutation.mutate()}
          size="sm"
          variant="outline"
        >
          {postConfigSaveMutation.isPending ? "Applying..." : "Apply config"}
        </Button>
      </AlertAction>
    </Alert>
  )
}
