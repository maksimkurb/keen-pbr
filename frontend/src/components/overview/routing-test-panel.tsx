import { Loader2, Search } from "lucide-react"
import { useState } from "react"

import { usePostRoutingTestMutation } from "@/api/mutations"
import { ButtonGroup } from "@/components/shared/button-group"
import { SectionCard } from "@/components/shared/section-card"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import {
  Empty,
  EmptyDescription,
  EmptyHeader,
  EmptyTitle,
} from "@/components/ui/empty"
import { Input } from "@/components/ui/input"
import { Skeleton } from "@/components/ui/skeleton"

import { RoutingDiagnosticsResult } from "./routing-diagnostics-result"
import { sanitizeRoutingTarget } from "./sanitize-routing-target"

export function RoutingTestPanel() {
  const [testTarget, setTestTarget] = useState("example.com")
  const [routingInputError, setRoutingInputError] = useState<string | null>(null)

  const routingTestMutation = usePostRoutingTestMutation()

  const firstRoutingTestResult =
    routingTestMutation.data?.status === 200
      ? routingTestMutation.data.data.results[0]
      : undefined
  const routingDiagnostics =
    routingTestMutation.data?.status === 200
      ? routingTestMutation.data.data
      : undefined

  return (
    <SectionCard title="Domain/IP routing test">
      <form
        className="space-y-3"
        onSubmit={(event) => {
          event.preventDefault()
          const sanitized = sanitizeRoutingTarget(testTarget)
          if (!sanitized) {
            setRoutingInputError("Please enter a valid domain or IP.")
            return
          }
          setRoutingInputError(null)
          if (sanitized !== testTarget) {
            setTestTarget(sanitized)
          }
          routingTestMutation.mutate({ data: { target: sanitized } })
        }}
      >
        <div className="relative">
          <Search className="pointer-events-none absolute left-3 top-1/2 h-4 w-4 -translate-y-1/2 text-muted-foreground" />
          <Input
            className="pl-9"
            onChange={(event) => setTestTarget(event.target.value)}
            onKeyDown={(event) => {
              if (event.key === "Enter" && testTarget.trim()) {
                event.preventDefault()
                const form = event.currentTarget.form
                form?.requestSubmit()
              }
            }}
            placeholder="Domain, IP or URL"
            value={testTarget}
          />
        </div>

        <ButtonGroup className="flex-wrap gap-2">
          <Button
            className="whitespace-nowrap"
            disabled={routingTestMutation.isPending || !testTarget.trim()}
            type="submit"
          >
            {routingTestMutation.isPending ? (
              <Loader2 className="mr-2 h-4 w-4 animate-spin" />
            ) : null}
            Run routing test
          </Button>
        </ButtonGroup>
      </form>

      {routingTestMutation.isPending ? (
        <div className="space-y-2">
          <Skeleton className="h-4 w-2/3" />
          <Skeleton className="h-4 w-1/2" />
        </div>
      ) : null}

      {routingInputError ? (
        <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription>{routingInputError}</AlertDescription>
        </Alert>
      ) : null}

      {routingTestMutation.isError ? (
        <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription>
            Routing test failed. Please try again.
          </AlertDescription>
        </Alert>
      ) : null}

      {routingTestMutation.isSuccess &&
      routingDiagnostics &&
      !firstRoutingTestResult &&
      routingDiagnostics.rule_diagnostics.length === 0 ? (
        <Empty className="border">
          <EmptyHeader>
            <EmptyTitle>No route matched</EmptyTitle>
            <EmptyDescription>
              Try another domain or IP address.
            </EmptyDescription>
          </EmptyHeader>
        </Empty>
      ) : null}

      {routingDiagnostics ? (
        <RoutingDiagnosticsResult
          diagnostics={routingDiagnostics}
          firstResult={firstRoutingTestResult}
        />
      ) : null}
    </SectionCard>
  )
}
