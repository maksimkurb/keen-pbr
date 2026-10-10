import type { Ref } from "react"
import { useTranslation } from "react-i18next"
import { ArrowRight, CircleCheck, Download } from "lucide-react"
import { Link } from "wouter"

import { Button } from "@/components/ui/button"
import { Checkbox } from "@/components/ui/checkbox"
import { Skeleton } from "@/components/ui/skeleton"
import { cn } from "@/lib/utils"

import type { OverviewHealthySummary, OverviewIssue } from "./overview-issues"
import { Panel, PanelHeader, StatusDot } from "./overview-panel"

export function DiagnosticsPanel({
  issues,
  healthy,
  isLoading,
  showHealthy,
  onShowHealthyChange,
  onDownload,
  highlighted,
  ref,
}: {
  issues: OverviewIssue[]
  healthy: OverviewHealthySummary[]
  isLoading: boolean
  showHealthy: boolean
  onShowHealthyChange: (value: boolean) => void
  onDownload: () => void
  highlighted: boolean
  ref?: Ref<HTMLElement>
}) {
  const { t } = useTranslation()
  const hasProblems = issues.length > 0
  const hasErrors = issues.some((issue) => issue.tone === "bad")

  return (
    <Panel
      className={cn(
        "transition-shadow duration-700",
        highlighted && "shadow-[0_0_0_6px] shadow-warning/20 ring-warning/60"
      )}
      id="diagnostics"
      ref={ref}
      tabIndex={-1}
    >
      <PanelHeader title={t("overview.routing.title")}>
        <label className="hidden cursor-pointer items-center gap-2 text-xs text-muted-foreground md:flex">
          <Checkbox
            checked={showHealthy}
            onCheckedChange={(checked) => onShowHealthyChange(checked === true)}
          />
          {t("overview.diagnostics.showHealthy")}
        </label>
        <Button
          size="sm"
          type="button"
          variant="outline"
          onClick={onDownload}
          className={cn(
            hasProblems &&
              "motion-safe:animate-[diagnostics-ping_1.5s_ease-out_infinite]",
            hasProblems &&
              (hasErrors
                ? "border-destructive/40 bg-destructive/10 text-destructive hover:bg-destructive/20 hover:text-destructive dark:border-destructive/40 dark:bg-destructive/20 dark:hover:bg-destructive/30"
                : "border-warning/40 bg-warning/15 text-warning-foreground hover:bg-warning/25 hover:text-warning-foreground dark:border-warning/40 dark:bg-warning/15 dark:hover:bg-warning/25")
          )}
        >
          <Download />
          {t("overview.diagnosticsDownload.button")}
        </Button>
      </PanelHeader>

      {isLoading && issues.length === 0 ? (
        <div className="space-y-2 p-3.5">
          <Skeleton className="h-9 w-full" />
          <Skeleton className="h-9 w-full" />
        </div>
      ) : issues.length === 0 ? (
        <div className="flex items-center gap-2 p-3.5 text-muted-foreground">
          <CircleCheck className="size-4 text-success" />
          {t("overview.diagnostics.noIssues")}
        </div>
      ) : (
        <ul>
          {issues.map((issue) => (
            <li
              className="flex items-start gap-2.5 border-t px-3.5 py-2.75 first:border-t-0"
              key={issue.key}
            >
              <StatusDot className="mt-1.5" tone={issue.tone} />
              <div className="min-w-0 flex-1">
                <div className="font-medium break-words">{issue.title}</div>
                {issue.detail ? (
                  <div className="mt-0.5 text-xs break-words text-muted-foreground">
                    {issue.detail}
                  </div>
                ) : null}
              </div>
              {issue.href ? (
                <Button
                  aria-label={t("overview.diagnostics.open")}
                  nativeButton={false}
                  render={<Link href={issue.href} />}
                  size="icon-sm"
                  variant="ghost"
                >
                  <ArrowRight />
                </Button>
              ) : null}
            </li>
          ))}
        </ul>
      )}

      {showHealthy && healthy.length > 0 ? (
        <ul className="border-t bg-muted/30">
          {healthy.map((entry) => (
            <li
              className="flex items-start gap-2.5 border-t px-3.5 py-2.75 first:border-t-0"
              key={entry.key}
            >
              <StatusDot className="mt-1.5" tone="ok" />
              <div className="min-w-0 flex-1">
                <div className="font-medium">{entry.title}</div>
                <div className="mt-0.5 text-xs text-muted-foreground">
                  {entry.detail}
                </div>
              </div>
            </li>
          ))}
        </ul>
      ) : null}
    </Panel>
  )
}
