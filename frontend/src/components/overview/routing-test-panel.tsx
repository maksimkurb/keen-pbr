import type { Ref } from "react"
import { Loader2, Search } from "lucide-react"
import { useTranslation } from "react-i18next"
import type { ApiError } from "@/api/client"

import { Button } from "@/components/ui/button"
import { Alert, AlertDescription } from "@/components/ui/alert"
import {
  Empty,
  EmptyDescription,
  EmptyHeader,
  EmptyTitle,
} from "@/components/ui/empty"
import {
  InputGroup,
  InputGroupAddon,
  InputGroupInput,
  InputGroupText,
} from "@/components/ui/input-group"
import { Skeleton } from "@/components/ui/skeleton"

import { buildRoutingTestRequest } from "./routing-test-criteria"
import { RoutingTestCriteriaFields } from "./routing-test-criteria-fields"
import { getApiErrorMessage } from "@/lib/api-errors"
import { RoutingDiagnosticsResult } from "./routing-diagnostics-result"
import { sanitizeRoutingTarget } from "./sanitize-routing-target"
import { useRoutingTestPanelState } from "./use-routing-test-panel-state"

export function RoutingTestPanel({
  state,
  targetInputRef,
}: {
  state: ReturnType<typeof useRoutingTestPanelState>
  targetInputRef?: Ref<HTMLInputElement>
}) {
  const { t } = useTranslation()
  const {
    testTarget,
    setTestTarget,
    criteria,
    setCriteria,
    lastRequestRef,
    routingInputError,
    setRoutingInputError,
    routingTestMutation,
    runtimeOutboundsQuery,
    routingDiagnostics,
  } = state

  const content = (
    <>
      <form
        className="space-y-3"
        onSubmit={(event) => {
          event.preventDefault()
          if (routingTestMutation.isPending) {
            return
          }

          const sanitized = sanitizeRoutingTarget(testTarget)
          if (!sanitized) {
            setRoutingInputError(t("overview.routingTest.invalidTarget"))
            return
          }
          setRoutingInputError(null)
          if (sanitized !== testTarget) {
            setTestTarget(sanitized)
          }
          const request = buildRoutingTestRequest(sanitized, criteria)
          lastRequestRef.current = request
          routingTestMutation.mutate({ data: request })
        }}
      >
        <div className="flex flex-col gap-2 sm:flex-row">
          <InputGroup className="h-11 min-w-0 flex-1 sm:h-9">
            <InputGroupAddon>
              <InputGroupText>
                <Search className="h-4 w-4" />
              </InputGroupText>
            </InputGroupAddon>
            <InputGroupInput
              ref={targetInputRef}
              onChange={(event) => setTestTarget(event.target.value)}
              onKeyDown={(event) => {
                if (
                  event.key === "Enter" &&
                  testTarget.trim() &&
                  !routingTestMutation.isPending
                ) {
                  event.preventDefault()
                  const form = event.currentTarget.form
                  form?.requestSubmit()
                }
              }}
              placeholder={t("overview.routingTest.placeholder")}
              value={testTarget}
            />
          </InputGroup>
          <Button
            className="h-11 sm:h-9"
            disabled={routingTestMutation.isPending}
            type="submit"
          >
            {routingTestMutation.isPending ? (
              <Loader2 className="animate-spin" />
            ) : null}
            {t("overview.routingTest.submit")}
          </Button>
        </div>
        <RoutingTestCriteriaFields value={criteria} onChange={setCriteria} />
      </form>

      {routingTestMutation.isPending ? (
        <div className="space-y-2">
          <Skeleton className="h-4 w-2/3" />
          <Skeleton className="h-4 w-1/2" />
        </div>
      ) : null}

      {routingInputError ? (
        <Alert variant="destructive">
          <AlertDescription>{routingInputError}</AlertDescription>
        </Alert>
      ) : null}

      {routingTestMutation.isError ? (
        <Alert variant="destructive">
          <AlertDescription>
            {getApiErrorMessage(routingTestMutation.error as ApiError | null) ||
              t("overview.routingTest.requestFailed")}
          </AlertDescription>
        </Alert>
      ) : null}

      {routingTestMutation.isSuccess &&
      routingDiagnostics &&
      routingDiagnostics.results.length === 0 &&
      routingDiagnostics.rule_diagnostics.length === 0 ? (
        <Empty className="border">
          <EmptyHeader>
            <EmptyTitle>{t("overview.routingTest.emptyTitle")}</EmptyTitle>
            <EmptyDescription>
              {t("overview.routingTest.emptyDescription")}
            </EmptyDescription>
          </EmptyHeader>
        </Empty>
      ) : null}

      {routingDiagnostics ? (
        <div className="space-y-3">
          <RoutingDiagnosticsResult
            diagnostics={routingDiagnostics}
            isRefreshing={routingTestMutation.isPending}
            onRefresh={() => {
              routingTestMutation.mutate({
                data: lastRequestRef.current ?? {
                  target: routingDiagnostics.target,
                },
              })
              void runtimeOutboundsQuery.refetch()
            }}
            runtimeOutbounds={
              runtimeOutboundsQuery.data?.status === 200
                ? runtimeOutboundsQuery.data.data.outbounds
                : []
            }
          />
        </div>
      ) : null}
    </>
  )

  return <div className="min-w-0 space-y-3">{content}</div>
}
