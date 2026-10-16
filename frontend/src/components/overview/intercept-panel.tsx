import { useTranslation } from "react-i18next"

import type { InterceptHealth } from "@/api/generated/model"
import type { DnsCheckStatus } from "@/hooks/use-dns-check"

import { DnsCheckWidget } from "./dns-check-widget"
import { type DotTone, Panel, PanelHeader, StatusDot } from "./overview-panel"

export function InterceptPanel({
  health,
  serviceRunning,
  requestedDns,
  requestedL7,
  markerDomain,
  onDnsCheckStatusChange,
}: {
  health?: InterceptHealth
  serviceRunning: boolean
  requestedDns: boolean
  requestedL7: boolean
  markerDomain: string
  onDnsCheckStatusChange: (status: DnsCheckStatus) => void
}) {
  const { t } = useTranslation()
  const running = serviceRunning && health?.running === true
  const dnsProbeEnabled =
    requestedDns && running && health?.dns_hold_active === true

  const rows: Array<{ key: string; requested: boolean; active: boolean }> = [
    { key: "dns", requested: requestedDns, active: running },
    {
      key: "dnsHold",
      requested: requestedDns,
      active: running && health?.dns_hold_active === true,
    },
    {
      key: "l7",
      requested: requestedL7,
      active: running && health?.l7_active === true,
    },
  ]

  return (
    <Panel>
      <PanelHeader title={t("overview.intercept.title")} />
      <ul>
        {rows.map((row) => {
          const { tone, label } = rowState(row, serviceRunning, t)
          return (
            <li
              className="grid grid-cols-[minmax(0,1fr)_auto] items-center gap-3 border-t px-3.5 py-2.25 first:border-t-0"
              key={row.key}
            >
              <div className="min-w-0">
                <div className="text-[13px] font-medium">
                  {t(`overview.intercept.rows.${row.key}.title`)}
                </div>
                <div className="text-xs text-muted-foreground">
                  {t(`overview.intercept.rows.${row.key}.description`)}
                </div>
              </div>
              <StatusDot title={label} tone={tone} />
            </li>
          )
        })}
      </ul>
      <DnsCheckWidget
        disabledReason={requestedDns ? "runtime" : "config"}
        dnsProbeEnabled={dnsProbeEnabled}
        markerDomain={markerDomain}
        onStatusChange={onDnsCheckStatusChange}
      />
    </Panel>
  )
}

function rowState(
  row: { requested: boolean; active: boolean },
  serviceRunning: boolean,
  t: (key: string) => string
): { tone: DotTone; label: string } {
  if (!row.requested) {
    return { tone: "off", label: t("overview.intercept.status.disabled") }
  }
  if (!serviceRunning) {
    return { tone: "off", label: t("overview.intercept.status.stopped") }
  }
  return row.active
    ? { tone: "ok", label: t("overview.intercept.status.running") }
    : { tone: "bad", label: t("overview.intercept.status.stopped") }
}
