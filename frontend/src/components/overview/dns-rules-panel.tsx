import type { ReactNode } from "react"
import { useTranslation } from "react-i18next"

import type { DnsmasqHealth } from "@/api/generated/model"
import { Badge } from "@/components/ui/badge"

import { Panel, PanelHeader, PanelNote } from "./overview-panel"

export function DnsRulesPanel({
  health,
  dimmed,
}: {
  health?: DnsmasqHealth
  dimmed: boolean
}) {
  const { t, i18n } = useTranslation()
  const state = health?.mode === "dnsmasq" ? health.state : "disabled"
  const badgeVariant = dimmed
    ? "outline"
    : state === "ok"
      ? "success"
      : state === "error"
        ? "destructive"
        : state === "disabled"
          ? "outline"
          : "warning"
  const repairAttempt = health?.repair_attempt ?? 0
  const repairMax = health?.repair_max_attempts ?? 0
  const alive = health?.dnsmasq_alive
  const formatTs = (ts?: number | null) =>
    ts ? formatTimestamp(ts, i18n.language) : undefined

  return (
    <Panel>
      <PanelHeader title={t("overview.dnsRules.title")}>
        <Badge size="xs" variant={badgeVariant}>
          {dimmed
            ? t("overview.dnsRules.inactive")
            : t(`overview.dnsRules.state.${state}`)}
        </Badge>
      </PanelHeader>

      {state === "disabled" ? (
        <div className="p-3.5">
          <PanelNote tone="muted">
            {t("overview.dnsRules.disabledDescription")}
          </PanelNote>
        </div>
      ) : (
        <>
          <dl className="grid grid-cols-[minmax(0,1fr)_auto] gap-x-3 gap-y-2 px-3.5 py-2.5">
            <KeyValue label={t("overview.dnsRules.server")}>
              <span className="font-mono">dnsmasq</span>
            </KeyValue>
            <KeyValue label={t("overview.dnsRules.rulesAndDomains")}>
              {health?.rules ?? 0} / {health?.domains ?? 0}
            </KeyValue>
            <KeyValue label={t("overview.dnsRules.lastSync")}>
              <span title={fullTimestamp(health?.last_apply_ts, i18n.language)}>
                {formatTs(health?.last_apply_ts) ?? "—"}
              </span>
            </KeyValue>
            {health?.loaded_ts ? (
              <KeyValue label={t("overview.dnsRules.loadedAt")}>
                <span title={fullTimestamp(health.loaded_ts, i18n.language)}>
                  {formatTs(health.loaded_ts)}
                </span>
              </KeyValue>
            ) : null}
            {health?.last_external_reload_ts ? (
              <KeyValue label={t("overview.dnsRules.externalReload")}>
                <span
                  title={fullTimestamp(
                    health.last_external_reload_ts,
                    i18n.language
                  )}
                >
                  {formatTs(health.last_external_reload_ts)}
                </span>
              </KeyValue>
            ) : null}
            {(alive === "dead" || alive === "unknown") && state !== "ok" ? (
              <KeyValue label={t("overview.dnsRules.alive.label")}>
                {t(`overview.dnsRules.alive.${alive}`)}
              </KeyValue>
            ) : null}
          </dl>

          {state === "reconciling" && health?.next_repair_ts ? (
            <div className="px-3.5 pb-3.5">
              <PanelNote tone="warn">
                <span>
                  {t("overview.dnsRules.repairScheduled", {
                    n: repairAttempt + 1,
                    max: repairMax,
                    time: new Date(
                      health.next_repair_ts * 1000
                    ).toLocaleTimeString(i18n.language),
                  })}
                  {health.repair_reason ? ` ${health.repair_reason}` : null}
                </span>
              </PanelNote>
            </div>
          ) : null}
          {state === "applying" && repairAttempt > 0 ? (
            <div className="px-3.5 pb-3.5">
              <PanelNote tone="warn">
                <span>
                  {t("overview.dnsRules.repairRestarting", {
                    n: repairAttempt,
                    max: repairMax,
                  })}
                  {health?.repair_reason ? ` ${health.repair_reason}` : null}
                </span>
              </PanelNote>
            </div>
          ) : null}
          {state === "error" &&
          (health?.last_error || health?.repair_paused) ? (
            <div className="px-3.5 pb-3.5">
              <PanelNote tone="bad">
                <span>
                  {health?.repair_paused ? (
                    <>
                      {t("overview.dnsRules.repairPaused", { max: repairMax })}{" "}
                      {t("overview.dnsRules.repairPausedHint")}{" "}
                    </>
                  ) : null}
                  {health?.last_error
                    ? `${t("overview.dnsRules.lastError")}: ${health.last_error}`
                    : null}
                </span>
              </PanelNote>
            </div>
          ) : null}
        </>
      )}
    </Panel>
  )
}

function KeyValue({ label, children }: { label: string; children: ReactNode }) {
  return (
    <>
      <dt className="min-w-0 text-[13px] text-muted-foreground">{label}</dt>
      <dd className="text-right font-medium tabular-nums">{children}</dd>
    </>
  )
}

function formatTimestamp(ts: number, locale: string) {
  const date = new Date(ts * 1000)
  const now = new Date()
  if (date.toDateString() === now.toDateString()) {
    return date.toLocaleTimeString(locale)
  }
  return date.toLocaleString(locale, {
    day: "numeric",
    month: "short",
    hour: "2-digit",
    minute: "2-digit",
  })
}

function fullTimestamp(ts: number | null | undefined, locale: string) {
  return ts ? new Date(ts * 1000).toLocaleString(locale) : undefined
}
