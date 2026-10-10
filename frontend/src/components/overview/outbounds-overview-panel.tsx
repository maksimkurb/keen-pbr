import { Fragment } from "react"
import { useTranslation } from "react-i18next"
import { ArrowRight } from "lucide-react"
import { Link } from "wouter"

import type {
  Outbound,
  RuntimeInterfaceState,
  RuntimeOutboundState,
} from "@/api/generated/model"
import { Badge } from "@/components/ui/badge"
import { Skeleton } from "@/components/ui/skeleton"
import {
  Empty,
  EmptyDescription,
  EmptyHeader,
  EmptyTitle,
} from "@/components/ui/empty"
import { cn } from "@/lib/utils"
import {
  getOrderedOutboundGroups,
  getOutboundGroupMembers,
} from "@/pages/outbounds-utils"

import { type DotTone, Panel, PanelHeader, StatusDot } from "./overview-panel"

const DASH = "—"

type GroupMember = {
  key: string
  tag: string
  interfaceName?: string
  state?: RuntimeInterfaceState
}

export function OutboundsOverviewPanel({
  outbounds,
  runtimeByTag,
  isLoading,
  loadError,
  dimmed,
}: {
  outbounds: Outbound[]
  runtimeByTag: Map<string, RuntimeOutboundState>
  isLoading: boolean
  loadError: boolean
  dimmed: boolean
}) {
  const { t } = useTranslation()
  const groups = outbounds.filter(isGroupOutbound)
  const plain = outbounds.filter((outbound) => !isGroupOutbound(outbound))
  const runtimeStates = outbounds
    .map((outbound) => runtimeByTag.get(outbound.tag))
    .filter((state): state is RuntimeOutboundState => Boolean(state))
  const online = dimmed
    ? 0
    : runtimeStates.filter((state) => state.status === "healthy").length

  return (
    <Panel id="outbounds">
      <PanelHeader
        title={t("overview.outbounds.title")}
        subtitle={
          outbounds.length > 0
            ? t("overview.outbounds.summary", {
                configured: outbounds.length,
                groups: groups.length,
              })
            : undefined
        }
      >
        {outbounds.length > 0 ? (
          <Badge
            size="xs"
            variant={
              dimmed
                ? "outline"
                : online === outbounds.length
                  ? "success"
                  : "warning"
            }
          >
            {t("overview.outbounds.online", { count: online })}
          </Badge>
        ) : null}
        <Link
          className="inline-flex items-center gap-1 text-[13px] font-medium text-primary hover:underline"
          href="/outbounds"
        >
          {t("overview.outbounds.manage")}
          <ArrowRight className="size-3.5" />
        </Link>
      </PanelHeader>

      {isLoading ? (
        <div className="space-y-2 p-3.5">
          <Skeleton className="h-8 w-full" />
          <Skeleton className="h-8 w-full" />
          <Skeleton className="h-8 w-full" />
        </div>
      ) : null}

      {loadError ? (
        <div className="px-3.5 py-3 text-destructive">
          {t("overview.outbounds.loadError")}
        </div>
      ) : null}

      {!isLoading && outbounds.length === 0 ? (
        <Empty className="m-3.5 border">
          <EmptyHeader>
            <EmptyTitle>{t("overview.outbounds.emptyTitle")}</EmptyTitle>
            <EmptyDescription>
              {t("overview.outbounds.emptyDescription")}
            </EmptyDescription>
          </EmptyHeader>
        </Empty>
      ) : null}

      {groups.length > 0 ? (
        <div className="overflow-x-auto">
          <table className="w-full border-collapse border-b border-border [&_td]:border-r [&_td]:border-b [&_td]:border-border [&_td:last-child]:border-r-0 [&_th]:border-r [&_th]:border-b [&_th]:border-border [&_th:last-child]:border-r-0">
            <thead>
              <tr className="border-b bg-muted/40 text-left text-[11px] font-semibold tracking-wider text-muted-foreground uppercase">
                <th className="w-[24%] px-3.5 py-1.75 font-semibold">
                  {t("overview.outbounds.columns.group")}
                </th>
                <th className="px-3.5 py-1.75 font-semibold">
                  {t("overview.outbounds.columns.members")}
                </th>
              </tr>
            </thead>
            <tbody>
              {groups.map((group) => {
                const runtimeState = runtimeByTag.get(group.tag)
                const members = getGroupMembers(group, runtimeState)
                const rowSpan = Math.max(members.length, 1)

                return (
                  <Fragment key={group.tag}>
                    {(members.length > 0 ? members : [null]).map(
                      (member, memberIndex) => (
                        <tr key={member?.key ?? "empty"}>
                          {memberIndex === 0 ? (
                            <>
                              <td
                                className="px-3.5 py-2 align-middle"
                                rowSpan={rowSpan}
                              >
                                <div className="flex items-center gap-1.5 font-semibold">
                                  {!dimmed && runtimeState ? (
                                    <StatusDot
                                      className={
                                        runtimeState.status === "healthy"
                                          ? "relative motion-safe:after:absolute motion-safe:after:inset-0 motion-safe:after:rounded-full motion-safe:after:bg-success motion-safe:after:animate-[outbound-ripple_2s_ease-out_infinite]"
                                          : undefined
                                      }
                                      tone={outboundTone(runtimeState.status)}
                                      title={
                                        runtimeState.status === "healthy"
                                          ? t("overview.outbounds.probePassed")
                                          : t(
                                              `runtime.outboundStatus.${runtimeState.status}`
                                            )
                                      }
                                    />
                                  ) : null}
                                  <span className="truncate">{group.tag}</span>
                                </div>
                                <div className="text-[11px] text-muted-foreground">
                                  {group.type}
                                  <span>
                                    {" · "}
                                    {group.strategy ?? "priority"}
                                  </span>
                                </div>
                              </td>
                            </>
                          ) : null}
                          <td
                            className={cn(
                              "px-3.5 py-2 align-middle"
                            )}
                          >
                            {member ? (
                              <MemberRow
                                dimmed={dimmed}
                                groupType={group.type}
                                member={member}
                              />
                            ) : (
                              <span className="text-muted-foreground">
                                {DASH}
                              </span>
                            )}
                          </td>
                        </tr>
                      )
                    )}
                  </Fragment>
                )
              })}
            </tbody>
          </table>
        </div>
      ) : null}

      {plain.length > 0 ? (
        <>
          {groups.length > 0 ? (
            <div className="bg-muted/40 px-3.5 py-1.75 text-[11px] font-semibold tracking-wider text-muted-foreground uppercase">
              {t("overview.outbounds.plainTitle")}
            </div>
          ) : null}
          <div className="overflow-hidden border-t">
            <div className="-mr-px -mb-px grid grid-cols-1 sm:grid-cols-2 md:grid-cols-3 xl:grid-cols-4">
              {plain.map((outbound) => {
                const runtimeState = runtimeByTag.get(outbound.tag)
                const value = describePlainOutbound(outbound, t)
                return (
                  <div
                    className="flex min-w-0 items-center gap-2 border-r border-b px-3 py-2"
                    key={outbound.tag}
                    title={runtimeState?.detail}
                  >
                    <StatusDot
                      tone={
                        dimmed || !runtimeState
                          ? "off"
                          : runtimeState.status === "healthy"
                            ? "up"
                            : outboundTone(runtimeState.status)
                      }
                      title={
                        !dimmed && runtimeState?.status === "healthy"
                          ? t(
                              outbound.type === "interface"
                                ? "overview.outbounds.interfaceUp"
                                : "overview.outbounds.plainActive"
                            )
                          : t(
                              `runtime.outboundStatus.${dimmed ? "unknown" : (runtimeState?.status ?? "unknown")}`
                            )
                      }
                    />
                    <span className="truncate text-[13px] font-medium">
                      {outbound.tag}
                    </span>
                    {value ? (
                      <span
                        className="ml-auto max-w-[55%] truncate font-mono text-[11px] text-muted-foreground"
                        title={value}
                      >
                        {value}
                      </span>
                    ) : null}
                  </div>
                )
              })}
            </div>
          </div>
        </>
      ) : null}
    </Panel>
  )
}

function MemberRow({
  member,
  groupType,
  dimmed,
}: {
  member: GroupMember
  groupType: Outbound["type"]
  dimmed: boolean
}) {
  const { t } = useTranslation()
  const state = member.state
  const latency =
    !dimmed && typeof state?.latency_ms === "number"
      ? t("overview.outbounds.latency", { value: state.latency_ms })
      : DASH
  const packets =
    groupType === "icmptest" && typeof state?.packets_attempted === "number"
      ? `${state.packets_received ?? 0}/${state.packets_attempted}`
      : undefined
  const slow = !dimmed && state?.status === "degraded"

  return (
    <div
      className="flex min-h-6 items-center justify-between gap-2"
      title={state?.detail}
    >
      <div className="flex min-w-0 flex-wrap items-center gap-x-2 gap-y-0.5">
        <span className="font-medium">{member.tag}</span>
        {member.interfaceName && member.interfaceName !== member.tag ? (
          <span className="font-mono text-[11px] text-muted-foreground">
            {member.interfaceName}
          </span>
        ) : null}
        {!dimmed && state ? (
          <Badge size="xs" variant={memberBadgeVariant(state.status)}>
            {t(`runtime.interfaceStatus.${state.status}`).toLowerCase()}
          </Badge>
        ) : null}
        {packets ? (
          <span
            className={cn(
              "font-mono text-[11px] text-muted-foreground",
              dimmed && "opacity-60"
            )}
            title={t("overview.outbounds.packetsTitle", {
              received: state?.packets_received ?? 0,
              attempted: state?.packets_attempted ?? 0,
            })}
          >
            {dimmed ? DASH : packets}
          </span>
        ) : null}
      </div>
      <span
        className={cn(
          "font-mono text-xs whitespace-nowrap text-muted-foreground",
          slow && "font-semibold text-warning-foreground",
          dimmed && "opacity-60"
        )}
      >
        {latency}
      </span>
    </div>
  )
}

function isGroupOutbound(outbound: Outbound) {
  return outbound.type === "urltest" || outbound.type === "icmptest"
}

function getGroupMembers(
  group: Outbound,
  runtimeState: RuntimeOutboundState | undefined
): GroupMember[] {
  if (runtimeState && runtimeState.interfaces.length > 0) {
    return runtimeState.interfaces.map((state, index) => ({
      key: `${state.outbound_tag}-${index}`,
      tag: state.outbound_tag,
      interfaceName: state.interface_name,
      state,
    }))
  }

  return getOrderedOutboundGroups(group).flatMap((outboundGroup, groupIndex) =>
    getOutboundGroupMembers(outboundGroup).map((member, index) => ({
      key: `${groupIndex}-${member.outbound}-${member.target ?? ""}-${index}`,
      tag: member.outbound,
    }))
  )
}

function describePlainOutbound(
  outbound: Outbound,
  t: (key: string, options?: Record<string, unknown>) => string
) {
  switch (outbound.type) {
    case "interface":
      return outbound.interface
    case "table":
      return typeof outbound.table === "number"
        ? t("overview.routing.tableLabel", { value: outbound.table })
        : outbound.type
    default:
      return outbound.type
  }
}

function outboundTone(status: RuntimeOutboundState["status"]): DotTone {
  switch (status) {
    case "healthy":
      return "ok"
    case "degraded":
      return "warn"
    case "unavailable":
      return "bad"
    default:
      return "off"
  }
}

function memberBadgeVariant(status: RuntimeInterfaceState["status"]) {
  switch (status) {
    case "active":
      return "success" as const
    case "degraded":
      return "warning" as const
    case "unavailable":
      return "destructive" as const
    default:
      return "outline" as const
  }
}
