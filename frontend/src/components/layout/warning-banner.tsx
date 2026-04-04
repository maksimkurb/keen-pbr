import { SaveIcon } from "lucide-react"
import { useTranslation } from "react-i18next"

import { useGetHealthService, useGetConfig } from "@/api/queries"
import {
  useApplyConfigMutation,
  usePostServiceActionMutation,
  useRoutingControlPendingState,
} from "@/api/mutations"
import { selectConfigIsDraft } from "@/api/selectors"
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { cn } from "@/lib/utils"

export function WarningBanner({ className }: { className?: string }) {
  const { t } = useTranslation()
  const configQuery = useGetConfig()
  const healthQuery = useGetHealthService({
    query: {
      refetchInterval: 30_000,
      refetchIntervalInBackground: false,
    },
  })
  const applyConfigMutation = useApplyConfigMutation()
  const restartRoutingMutation = usePostServiceActionMutation("restart")
  const { anyPending, applyPending, restartPending } =
    useRoutingControlPendingState()

  const serviceHealth =
    healthQuery.data?.status === 200 ? healthQuery.data.data : null
  const isDraft =
    serviceHealth?.config_is_draft ?? selectConfigIsDraft(configQuery.data)
  const expectedResolverHash = serviceHealth?.resolver_config_hash
  const actualResolverHash = serviceHealth?.resolver_config_hash_actual
  const hasResolverHashMismatch =
    Boolean(expectedResolverHash) &&
    Boolean(actualResolverHash) &&
    expectedResolverHash !== actualResolverHash
  const isServiceRunning = serviceHealth?.status === "running"

  if (!isDraft && !hasResolverHashMismatch) {
    return null
  }

  return (
    <div className={cn("space-y-2", className)}>
      {isDraft ? (
        <Alert variant="warning">
          <AlertDescription>{t("warning.draftChanged")}</AlertDescription>
          <Button
            disabled={anyPending}
            onClick={() => applyConfigMutation.mutate()}
            size="xs"
            variant="outline"
            className="mt-2"
          >
            <SaveIcon className="mr-1 h-3 w-3" />
            {applyPending
              ? t("warning.actions.applying")
              : t("warning.actions.apply")}
          </Button>
        </Alert>
      ) : null}

      {hasResolverHashMismatch ? (
        <Alert variant="warning">
          <AlertTitle>{t("warning.compact.resolverStale")}</AlertTitle>
          <Button
            disabled={anyPending || !serviceHealth || !isServiceRunning}
            onClick={() => restartRoutingMutation.mutate()}
            size="xs"
            variant="outline"
            className="mt-2"
          >
            {restartPending
              ? t("warning.actions.restarting")
              : t("warning.actions.restart")}
          </Button>
        </Alert>
      ) : null}
    </div>
  )
}
