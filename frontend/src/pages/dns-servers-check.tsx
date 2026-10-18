import {
  CheckCircle2,
  ChevronDown,
  CircleAlert,
  Loader2,
  ShieldCheck,
  TriangleAlert,
} from "lucide-react"
import { useTranslation } from "react-i18next"

import type { DnsCheckResponse } from "@/api/generated/model/dnsCheckResponse"
import type { DnsCheckServer } from "@/api/generated/model/dnsCheckServer"
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { ButtonGroup } from "@/components/ui/button-group"
import {
  DropdownMenu,
  DropdownMenuCheckboxItem,
  DropdownMenuContent,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu"
import { Input } from "@/components/ui/input"
import {
  Popover,
  PopoverContent,
  PopoverTrigger,
} from "@/components/ui/popover"
import {
  MAX_CHECK_DOMAINS,
  PRESET_DOMAINS,
  selectionProblem,
  serverLevel,
  summarizeCheck,
  type CheckProblem,
  type CheckSelection,
} from "@/pages/dns-servers-check-utils"

type TranslateFn = (key: string, options?: Record<string, unknown>) => string

/** The text for a status or reason code, or the code itself when unknown. */
function statusText(code: string, t: TranslateFn, fallback = code): string {
  return t(`pages.dnsServers.check.status.${code}`, { defaultValue: fallback })
}

function describeProblem(problem: CheckProblem, t: TranslateFn): string {
  const where = problem.address
    ? `${problem.tag} (${problem.address})`
    : problem.tag
  const what = statusText(problem.status, t)
  return problem.domain
    ? `${where}, ${problem.domain}: ${what}`
    : `${where}: ${what}`
}

export function DnsCheckButton({
  selection,
  onSelectionChange,
  onRun,
  isRunning,
  disabled,
}: {
  selection: CheckSelection
  onSelectionChange: (selection: CheckSelection) => void
  onRun: () => void
  isRunning: boolean
  disabled: boolean
}) {
  const { t } = useTranslation()
  const problem = selectionProblem(selection)
  const custom = selection.custom.trim()
  const chosen = selection.presets.length + (custom !== "" ? 1 : 0)
  const togglePreset = (domain: string, checked: boolean) => {
    onSelectionChange({
      ...selection,
      presets: checked
        ? [...selection.presets, domain]
        : selection.presets.filter((item) => item !== domain),
    })
  }

  return (
    <ButtonGroup>
      <Button
        disabled={disabled || isRunning || problem !== null}
        onClick={onRun}
        variant="outline"
      >
        {isRunning ? (
          <Loader2 className="mr-1 h-4 w-4 animate-spin" />
        ) : (
          <ShieldCheck className="mr-1 h-4 w-4" />
        )}
        {isRunning
          ? t("pages.dnsServers.check.running")
          : t("pages.dnsServers.check.button")}
      </Button>
      <DropdownMenu>
        <DropdownMenuTrigger
          render={
            <Button
              aria-label={t("pages.dnsServers.check.chooseSites")}
              disabled={disabled || isRunning}
              size="icon"
              variant="outline"
            />
          }
        >
          <ChevronDown className="h-4 w-4" />
        </DropdownMenuTrigger>
        <DropdownMenuContent className="w-72">
          <div className="px-3 py-2 text-xs font-medium text-muted-foreground">
            {t("pages.dnsServers.check.sites")}
          </div>
          {PRESET_DOMAINS.map((domain) => {
            const checked = selection.presets.includes(domain)
            return (
              <DropdownMenuCheckboxItem
                checked={checked}
                disabled={!checked && chosen >= MAX_CHECK_DOMAINS}
                key={domain}
                onCheckedChange={(next) => togglePreset(domain, next)}
              >
                {domain}
              </DropdownMenuCheckboxItem>
            )
          })}
          <div className="space-y-1 px-3 pt-2 pb-2">
            <label
              className="text-xs font-medium text-muted-foreground"
              htmlFor="dns-check-custom"
            >
              {t("pages.dnsServers.check.customLabel")}
            </label>
            <Input
              aria-invalid={problem === "invalidCustom"}
              id="dns-check-custom"
              inputMode="url"
              onChange={(event) =>
                onSelectionChange({ ...selection, custom: event.target.value })
              }
              onKeyDown={(event) => {
                // Typing must not be taken for menu navigation.
                event.stopPropagation()
                if (event.key === "Enter" && problem === null && !isRunning) {
                  onRun()
                }
              }}
              placeholder="example.org"
              value={selection.custom}
            />
            {problem ? (
              <p className="text-xs text-destructive">
                {t(`pages.dnsServers.check.problems.${problem}`, {
                  count: MAX_CHECK_DOMAINS,
                })}
              </p>
            ) : null}
          </div>
        </DropdownMenuContent>
      </DropdownMenu>
    </ButtonGroup>
  )
}

export function DnsCheckResultAlert({
  response,
  draft,
}: {
  response: DnsCheckResponse
  draft: boolean
}) {
  const { t } = useTranslation()
  const summary = summarizeCheck(response)
  const variant =
    summary.level === "error"
      ? "destructive"
      : summary.level === "warning"
        ? "warning"
        : "default"
  const Icon =
    summary.level === "error"
      ? CircleAlert
      : summary.level === "warning"
        ? TriangleAlert
        : CheckCircle2

  return (
    <Alert variant={variant}>
      <Icon />
      <AlertTitle>
        {t(`pages.dnsServers.check.result.${summary.level}`)}
      </AlertTitle>
      <AlertDescription>
        {summary.errors.length > 0 ? (
          <div>
            <p>{t("pages.dnsServers.check.result.notAnswering")}</p>
            <ul className="list-disc pl-5">
              {summary.errors.map((problem, index) => (
                <li key={`error-${index}`}>{describeProblem(problem, t)}</li>
              ))}
            </ul>
          </div>
        ) : null}
        {summary.warnings.length > 0 ? (
          <div>
            {summary.errors.length > 0 ? (
              <p>{t("pages.dnsServers.check.result.alsoSuspicious")}</p>
            ) : null}
            <ul className="list-disc pl-5">
              {summary.warnings.map((problem, index) => (
                <li key={`warning-${index}`}>{describeProblem(problem, t)}</li>
              ))}
            </ul>
            <p className="mt-1">{t("pages.dnsServers.check.result.advice")}</p>
          </div>
        ) : null}
        {draft ? (
          <p className="mt-1">{t("pages.dnsServers.check.draftHint")}</p>
        ) : null}
      </AlertDescription>
    </Alert>
  )
}

/** The status of one server in the table; a click shows the details. */
export function DnsCheckBadge({ server }: { server: DnsCheckServer }) {
  const { t } = useTranslation()
  const level = serverLevel(server)
  const variant =
    level === "error"
      ? "destructive"
      : level === "warning"
        ? "outline"
        : "secondary"
  return (
    <Popover>
      <PopoverTrigger
        render={
          <button
            className="cursor-pointer text-left"
            type="button"
            aria-label={t("pages.dnsServers.check.details", {
              server: server.tag,
            })}
          />
        }
      >
        <Badge
          className={
            level === "warning"
              ? "border-warning/50 text-warning-foreground"
              : undefined
          }
          variant={variant}
        >
          {t(`pages.dnsServers.check.badge.${level}`)}
        </Badge>
      </PopoverTrigger>
      <PopoverContent className="w-80 max-w-[calc(100vw-2rem)] space-y-3 text-sm">
        {server.error ? (
          <p className="text-destructive">
            {statusText(server.error_code ?? "error", t, server.error)}
          </p>
        ) : null}
        {server.endpoints.map((endpoint) => (
          <div className="space-y-1" key={endpoint.address}>
            <div className="font-medium break-all">{endpoint.address}</div>
            {endpoint.ips && endpoint.ips.length > 0 ? (
              <div className="text-xs break-all text-muted-foreground">
                {endpoint.ips.join(", ")}
              </div>
            ) : null}
            {endpoint.status !== "ok" ? (
              <p className="text-muted-foreground">
                {statusText(
                  endpoint.reason_code ?? endpoint.status,
                  t,
                  endpoint.reason ?? endpoint.status
                )}
              </p>
            ) : null}
            {endpoint.results.map((item) => (
              <div className="flex flex-wrap gap-x-2" key={item.domain}>
                <span className="font-medium">{item.domain}</span>
                <span
                  className={
                    item.severity === "error"
                      ? "text-destructive"
                      : item.severity === "warning"
                        ? "text-warning-foreground"
                        : "text-muted-foreground"
                  }
                >
                  {statusText(item.status, t)}
                </span>
                {item.addresses.length > 0 ? (
                  <span className="break-all text-muted-foreground">
                    {item.addresses.join(", ")}
                  </span>
                ) : null}
                <span className="text-muted-foreground">
                  {t("pages.dnsServers.check.milliseconds", {
                    count: Math.round(item.duration_ms),
                  })}
                </span>
              </div>
            ))}
            {endpoint.nxdomain_hijack ? (
              <p className="text-warning-foreground">
                {statusText("nxdomain_hijack", t)}
              </p>
            ) : null}
          </div>
        ))}
      </PopoverContent>
    </Popover>
  )
}
