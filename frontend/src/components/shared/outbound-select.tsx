import { useMemo } from "react"
import { useTranslation } from "react-i18next"

import type { Outbound } from "@/api/generated/model/outbound"
import type { RuntimeOutboundState } from "@/api/generated/model/runtimeOutboundState"
import { useGetRuntimeOutbounds } from "@/api/queries"
import { Badge } from "@/components/ui/badge"
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectLabel,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"

type OutboundSelectProps = {
  value: string
  onValueChange: (value: string) => void
  outbounds: Outbound[]
  allowEmpty?: boolean
  emptyLabel?: string
  placeholder?: string
  groupLabel?: string
  ariaInvalid?: boolean
  disabled?: boolean
}

export function OutboundSelect({
  value,
  onValueChange,
  outbounds,
  allowEmpty = false,
  emptyLabel,
  placeholder,
  groupLabel,
  ariaInvalid,
  disabled,
}: OutboundSelectProps) {
  const { t } = useTranslation()
  const runtimeOutboundsQuery = useGetRuntimeOutbounds({
    query: {
      refetchInterval: 10_000,
      refetchIntervalInBackground: false,
    },
  })

  const runtimeOutboundsByTag = useMemo(
    () =>
      new Map(
        (runtimeOutboundsQuery.data?.status === 200
          ? runtimeOutboundsQuery.data.data.outbounds
          : []
        ).map((runtimeOutbound) => [runtimeOutbound.tag, runtimeOutbound])
      ),
    [runtimeOutboundsQuery.data]
  )

  const selectedValue = value || null
  const resolvedEmptyLabel = emptyLabel ?? t("pages.dnsServerUpsert.fields.detourEmpty")
  const resolvedPlaceholder =
    placeholder ?? t("pages.routingRuleUpsert.fields.selectOutbound")
  const resolvedGroupLabel =
    groupLabel ?? t("pages.routingRuleUpsert.fields.configuredOutbounds")

  return (
    <Select
      disabled={disabled}
      onValueChange={(nextValue) => onValueChange(nextValue ?? "")}
      value={selectedValue}
    >
      <SelectTrigger aria-invalid={ariaInvalid}>
        <SelectValue placeholder={resolvedPlaceholder}>
          {(selected) => {
            if (!selected) {
              return allowEmpty ? resolvedEmptyLabel : resolvedPlaceholder
            }

            return selected
          }}
        </SelectValue>
      </SelectTrigger>
      <SelectContent>
        <SelectGroup>
          <SelectLabel>{resolvedGroupLabel}</SelectLabel>
          {allowEmpty ? (
            <SelectItem value={null}>
              <span className="text-muted-foreground">{resolvedEmptyLabel}</span>
            </SelectItem>
          ) : null}
          {outbounds.map((outbound) => (
            <SelectItem key={outbound.tag} value={outbound.tag}>
              <OutboundSelectOption
                outbound={outbound}
                runtimeState={runtimeOutboundsByTag.get(outbound.tag)}
                t={t}
              />
            </SelectItem>
          ))}
        </SelectGroup>
      </SelectContent>
    </Select>
  )
}

function OutboundSelectOption({
  outbound,
  runtimeState,
  t,
}: {
  outbound: Outbound
  runtimeState?: RuntimeOutboundState
  t: (key: string, options?: Record<string, unknown>) => string
}) {
  return (
    <div className="flex min-w-0 items-center justify-between gap-3">
      <span className="truncate font-medium">{outbound.tag}</span>
      <span className="flex shrink-0 items-center gap-2">
        <Badge size="xs" variant="outline">
          {outbound.type}
        </Badge>
        {runtimeState ? (
          <Badge
            size="xs"
            variant={runtimeState.status === "healthy" ? "success" : "warning"}
          >
            {runtimeState.status === "healthy"
              ? t("runtime.healthy")
              : t("runtime.notHealthy")}
          </Badge>
        ) : null}
      </span>
    </div>
  )
}
