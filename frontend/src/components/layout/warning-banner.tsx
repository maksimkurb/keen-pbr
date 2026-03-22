import { useGetHealthService, useGetConfig } from "@/api/queries"
import { usePostConfigSaveMutation, usePostReloadMutation } from "@/api/mutations"
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
  const postReloadMutation = usePostReloadMutation()

  const serviceHealth = healthQuery.data?.status === 200 ? healthQuery.data.data : null
  const isDraft =
    serviceHealth?.config_is_draft ?? selectConfigIsDraft(configQuery.data)
  const expectedResolverHash = serviceHealth?.resolver_config_hash
  const actualResolverHash = serviceHealth?.resolver_config_hash_actual
  const hasResolverHashMismatch =
    Boolean(expectedResolverHash) &&
    Boolean(actualResolverHash) &&
    expectedResolverHash !== actualResolverHash

  if (!isDraft && !hasResolverHashMismatch) {
    return null
  }

  if (compact) {
    return (
      <div className={cn("space-y-2", className)}>
        {isDraft ? (
          <Alert className="mb-0 px-2 py-1.5 text-warning-foreground border-warning/50 bg-warning/5 [&_[data-slot=alert-title]]:text-xs [&_[data-slot=alert-title]]:font-medium">
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
        ) : null}

        {hasResolverHashMismatch ? (
          <Alert className="mb-0 px-2 py-1.5 text-warning-foreground border-warning/50 bg-warning/5 [&_[data-slot=alert-title]]:text-xs [&_[data-slot=alert-title]]:font-medium">
            <AlertTitle>dnsmasq config is stale; reload required.</AlertTitle>
            <AlertAction className="top-1.5 right-1.5">
              <Button
                disabled={postReloadMutation.isPending}
                onClick={() => postReloadMutation.mutate()}
                size="xs"
                variant="outline"
              >
                {postReloadMutation.isPending ? "Reloading..." : "Reload"}
              </Button>
            </AlertAction>
          </Alert>
        ) : null}
      </div>
    )
  }

  return (
    <div className={cn("space-y-3", className)}>
      {isDraft ? (
        <Alert className="border-warning/50 bg-warning/5 text-warning-foreground">
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
      ) : null}

      {hasResolverHashMismatch ? (
        <Alert className="border-warning/50 bg-warning/5 text-warning-foreground">
          <AlertTitle>dnsmasq is using a stale resolver config</AlertTitle>
          <AlertDescription>
            The expected resolver hash ({expectedResolverHash?.slice(0, 10)}…)
            doesn&apos;t match dnsmasq&apos;s active hash ({actualResolverHash?.slice(0, 10)}…).
            Reload keen-pbr to regenerate and apply the current resolver configuration.
          </AlertDescription>
          <AlertAction>
            <Button
              disabled={postReloadMutation.isPending}
              onClick={() => postReloadMutation.mutate()}
              size="sm"
              variant="outline"
            >
              {postReloadMutation.isPending ? "Reloading..." : "Reload service"}
            </Button>
          </AlertAction>
        </Alert>
      ) : null}
    </div>
  )
}
