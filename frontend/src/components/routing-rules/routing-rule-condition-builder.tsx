import type { ReactNode } from "react"
import { useRef } from "react"
import { useTranslation } from "react-i18next"
import { ArrowDown, ArrowRight, Plus, X } from "lucide-react"

import { Button } from "@/components/ui/button"
import {
  RoutingRuleConditionPicker,
  routeConditionTitle,
} from "./routing-rule-condition-picker"
import {
  routeConditionKeys,
  type RouteConditionKey,
} from "@/pages/routing-rules-utils"

export function RoutingRuleConditionBuilder({
  activeConditions,
  controls,
  outbound,
  isNormalRule,
  onAdd,
  onRemove,
}: {
  activeConditions: readonly RouteConditionKey[]
  controls: Partial<Record<RouteConditionKey, ReactNode>>
  outbound: ReactNode
  isNormalRule: boolean
  onAdd: (key: RouteConditionKey) => void
  onRemove: (key: RouteConditionKey) => void
}) {
  const { t } = useTranslation()
  const root = useRef<HTMLDivElement>(null)
  const available = routeConditionKeys.filter(
    (key) => !activeConditions.includes(key)
  )
  const title = (key: RouteConditionKey) => routeConditionTitle(key, t)
  const add = (key: RouteConditionKey) => {
    if (!available.includes(key)) return
    onAdd(key)
    // Wait for the new editor and the menu focus restoration before focusing its input.
    requestAnimationFrame(() => {
      root.current
        ?.querySelector<HTMLElement>(
          `[data-condition="${key}"] input, [data-condition="${key}"] [role="combobox"]`
        )
        ?.focus()
    })
  }

  const outboundAction = (
    <div
      data-slot="routing-outbound-action"
      className="min-w-0 rounded-xl border bg-primary/5 p-3 sm:p-4 [&_[data-slot=field-content]]:min-w-0 [&_[data-slot=select-trigger]]:bg-card"
    >
      {outbound}
    </div>
  )

  return (
    <div ref={root} className="min-w-0 space-y-4">
      {isNormalRule ? (
        <div className="space-y-1">
          <h2 className="text-base font-semibold">
            {t("pages.routingRuleUpsert.builder.title")}
          </h2>
          <p className="text-sm text-muted-foreground">
            {t("pages.routingRuleUpsert.builder.description")}
          </p>
        </div>
      ) : (
        <p className="text-sm text-muted-foreground">
          {t("pages.routingRuleUpsert.fields.modeHint")}
        </p>
      )}
      {isNormalRule && activeConditions.length === 0 ? (
        <RoutingRuleConditionPicker
          mode="initial"
          available={available}
          onAdd={add}
          t={t}
        />
      ) : (
        <div data-slot="condition-flow" className="min-w-0">
          {isNormalRule ? (
            <>
              {activeConditions.map((key, index) => (
                <FlowRow
                  key={key}
                  label={t(
                    index === 0
                      ? "pages.routingRuleUpsert.builder.if"
                      : "pages.routingRuleUpsert.builder.and"
                  )}
                >
                  <div
                    data-condition={key}
                    className="relative min-w-0 rounded-xl border bg-card p-3 sm:p-4 [&_[data-slot=field-content]]:min-w-0 [&_[data-slot=field-label]]:pr-10 [&_[data-slot=input-group-addon]]:min-w-0 [&_[data-slot=input-group-addon]]:break-all"
                  >
                    {controls[key]}
                    <Button
                      aria-label={t("pages.routingRuleUpsert.builder.remove", {
                        condition: title(key),
                      })}
                      className="absolute top-1 right-1 size-11"
                      type="button"
                      variant="ghost"
                      size="icon"
                      onClick={() => {
                        onRemove(key)
                        requestAnimationFrame(() =>
                          root.current
                            ?.querySelector<HTMLElement>(
                              "[data-slot=initial-condition-picker] button, [data-slot=add-condition-picker] button"
                            )
                            ?.focus()
                        )
                      }}
                    >
                      <X aria-hidden="true" />
                    </Button>
                  </div>
                </FlowRow>
              ))}
              <FlowRow
                step={<Plus aria-hidden="true" className="size-4" />}
                label=""
              >
                <RoutingRuleConditionPicker
                  mode="additional"
                  available={available}
                  onAdd={add}
                  t={t}
                />
              </FlowRow>
            </>
          ) : null}
          {isNormalRule ? (
            <FlowRow
              step={<ArrowRight aria-hidden="true" className="size-4" />}
              label={t("pages.routingRuleUpsert.builder.then")}
              last
            >
              {outboundAction}
            </FlowRow>
          ) : (
            outboundAction
          )}
        </div>
      )}
    </div>
  )
}

function FlowRow({
  step,
  label,
  children,
  last = false,
}: {
  step?: ReactNode
  label: string
  children: ReactNode
  last?: boolean
}) {
  return (
    <div className="grid min-w-0 grid-cols-[2.25rem_minmax(0,1fr)] gap-2 sm:grid-cols-[3rem_minmax(0,1fr)] sm:gap-3">
      <div className="flex flex-col items-center gap-1 pt-2 text-xs text-muted-foreground">
        {step ? (
          <span className="flex size-6 shrink-0 items-center justify-center rounded-full border bg-muted/50 font-medium text-foreground">
            {step}
          </span>
        ) : null}
        {label ? <span>{label}</span> : null}
        {!last ? (
          <>
            <span
              aria-hidden="true"
              className="min-h-4 w-px flex-1 bg-border"
            />
            <ArrowDown aria-hidden="true" className="mb-2 size-3.5" />
          </>
        ) : null}
      </div>
      <div className={last ? "min-w-0" : "min-w-0 pb-4"}>{children}</div>
    </div>
  )
}
