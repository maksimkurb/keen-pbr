import { useTranslation } from "react-i18next"

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
  const { t } = useTranslation()
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
          <Alert className="mb-0 border-warning/50 bg-warning/5 px-2 py-1.5 text-warning-foreground [&_[data-slot=alert-title]]:text-xs [&_[data-slot=alert-title]]:font-medium">
            <AlertTitle>{t("warning.compact.draftPending")}</AlertTitle>
            <AlertAction className="right-1.5 top-1.5">
              <Button
                disabled={postConfigSaveMutation.isPending}
                onClick={() => postConfigSaveMutation.mutate()}
                size="xs"
                variant="outline"
              >
                {postConfigSaveMutation.isPending
                  ? t("warning.compact.saving")
                  : t("warning.compact.apply")}
              </Button>
            </AlertAction>
          </Alert>
        ) : null}

        {hasResolverHashMismatch ? (
          <Alert className="mb-0 border-warning/50 bg-warning/5 px-2 py-1.5 text-warning-foreground [&_[data-slot=alert-title]]:text-xs [&_[data-slot=alert-title]]:font-medium">
            <AlertTitle>{t("warning.compact.resolverStale")}</AlertTitle>
            <AlertAction className="right-1.5 top-1.5">
              <Button
                disabled={postReloadMutation.isPending}
                onClick={() => postReloadMutation.mutate()}
                size="xs"
                variant="outline"
              >
                {postReloadMutation.isPending
                  ? t("warning.compact.reloading")
                  : t("warning.compact.reload")}
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
          <AlertTitle>{t("warning.full.unsavedTitle")}</AlertTitle>
          <AlertDescription>{t("warning.full.unsavedDescription")}</AlertDescription>
          <AlertAction>
            <Button
              disabled={postConfigSaveMutation.isPending}
              onClick={() => postConfigSaveMutation.mutate()}
              size="sm"
              variant="outline"
            >
              {postConfigSaveMutation.isPending
                ? t("warning.full.applying")
                : t("warning.full.applyConfig")}
            </Button>
          </AlertAction>
        </Alert>
      ) : null}

      {hasResolverHashMismatch ? (
        <Alert className="border-warning/50 bg-warning/5 text-warning-foreground">
          <AlertTitle>{t("warning.full.staleTitle")}</AlertTitle>
          <AlertDescription>
            {t("warning.full.staleDescription", {
              expected: expectedResolverHash?.slice(0, 10),
              actual: actualResolverHash?.slice(0, 10),
            })}
          </AlertDescription>
          <AlertAction>
            <Button
              disabled={postReloadMutation.isPending}
              onClick={() => postReloadMutation.mutate()}
              size="sm"
              variant="outline"
            >
              {postReloadMutation.isPending
                ? t("warning.full.reloading")
                : t("warning.full.reloadService")}
            </Button>
          </AlertAction>
        </Alert>
      ) : null}
    </div>
  )
}
