import { useState } from "react"
import { useTranslation } from "react-i18next"
import { ChevronDown } from "lucide-react"

import type { InterceptHealth } from "@/api/generated/model"
import { Button } from "@/components/ui/button"
import { cn } from "@/lib/utils"

import { Panel, PanelHeader } from "./overview-panel"

const DASH = "—"

type CounterEntry = { label: string; value?: number }

export function CountersPanel({
  health,
  dimmed,
}: {
  health?: InterceptHealth
  dimmed: boolean
}) {
  const { t, i18n } = useTranslation()
  const [expanded, setExpanded] = useState(false)
  const counters = health?.counters
  const kernelQueue = health?.kernel_queue

  if (!counters && !kernelQueue) {
    return null
  }

  const numberFormat = new Intl.NumberFormat(i18n.language)
  const format = (value?: number) =>
    dimmed ? DASH : numberFormat.format(value ?? 0)
  const counter = (name: string, value?: number): CounterEntry => ({
    label: t(`overview.intercept.counters.${name}`),
    value,
  })
  const queueCounter = (name: string, value?: number): CounterEntry => ({
    label: t(`overview.intercept.kernelQueue.${name}`),
    value,
  })

  const keyStats = [
    ["dnsPackets", counters?.dns_packets],
    ["dnsMatched", counters?.dns_matched],
    ["l7Packets", counters?.l7_packets],
    ["l7Matched", counters?.l7_matched],
  ] as const

  const groups: Array<{ key: string; items: CounterEntry[] }> = [
    {
      key: "dns",
      items: [
        counter("dnsPackets", counters?.dns_packets),
        counter("dnsMatched", counters?.dns_matched),
        counter("dnsParseErrors", counters?.dns_parse_errors),
        counter("dnsHoldTimeouts", counters?.dns_hold_timeouts),
        counter("dnsTcpPartial", counters?.dns_tcp_partial),
      ],
    },
    {
      key: "l7",
      items: [
        counter("l7Packets", counters?.l7_packets),
        counter("l7Matched", counters?.l7_matched),
        counter("markerHits", counters?.marker_hits),
      ],
    },
    {
      key: "sets",
      items: [
        counter("setAdded", counters?.set_added),
        counter("setRefreshed", counters?.set_refreshed),
        counter("setErrors", counters?.set_errors),
      ],
    },
    {
      key: "conntrack",
      items: [
        counter("conntrackRequests", counters?.conntrack_requests),
        counter("conntrackDeleted", counters?.conntrack_deleted),
        counter("conntrackErrors", counters?.conntrack_errors),
      ],
    },
    {
      key: "queue",
      items: [
        counter("queueOverruns", counters?.queue_overruns),
        counter("logOverruns", counters?.log_overruns),
        ...(kernelQueue
          ? [
              queueCounter("queueTotal", kernelQueue.queue_total),
              queueCounter("queueDropped", kernelQueue.queue_dropped),
              queueCounter("userDropped", kernelQueue.user_dropped),
            ]
          : []),
      ],
    },
  ]
  const total = groups.reduce((sum, group) => sum + group.items.length, 0)

  return (
    <Panel>
      <PanelHeader title={t("overview.counters.title")}>
        <Button
          aria-expanded={expanded}
          size="sm"
          type="button"
          variant="ghost"
          onClick={() => setExpanded((value) => !value)}
        >
          {expanded
            ? t("overview.counters.collapse")
            : t("overview.counters.showAll", { count: total })}
          <ChevronDown
            className={cn("transition-transform", expanded && "rotate-180")}
          />
        </Button>
      </PanelHeader>
      <div className="grid grid-cols-2 sm:grid-cols-4">
        {keyStats.map(([name, value], index) => (
          <div
            className={cn(
              "min-w-0 px-3.5 py-2.5",
              index > 0 && "sm:border-l",
              index % 2 === 1 && "border-l",
              index >= 2 && "border-t sm:border-t-0"
            )}
            key={name}
          >
            <div className="truncate text-[11px] text-muted-foreground">
              {t(`overview.counters.short.${name}`)}
            </div>
            <div className="mt-px text-[17px] font-semibold whitespace-nowrap tabular-nums">
              {format(value)}
            </div>
          </div>
        ))}
      </div>
      {expanded ? (
        <div className="grid grid-cols-1 gap-x-5 gap-y-3 border-t px-3.5 py-3 sm:grid-cols-2">
          {groups.map((group) => (
            <div key={group.key}>
              <div className="mb-0.5 text-[11px] font-semibold tracking-wider text-muted-foreground uppercase">
                {t(`overview.counters.groups.${group.key}`)}
              </div>
              {group.items.map((item) => {
                const zero = dimmed || !item.value
                return (
                  <div
                    className="flex justify-between gap-2 py-0.5 text-[13px]"
                    key={item.label}
                  >
                    <span className="text-muted-foreground">{item.label}</span>
                    <span
                      className={cn(
                        "font-semibold tabular-nums",
                        zero && "font-normal text-muted-foreground/70"
                      )}
                    >
                      {format(item.value)}
                    </span>
                  </div>
                )
              })}
            </div>
          ))}
        </div>
      ) : null}
    </Panel>
  )
}
