import {
  ArrowRightFromLine,
  ArrowRightToLine,
  Network,
  Plus,
  Scroll,
  SquareArrowRightEnter,
  SquareArrowRightExit,
  Tag,
  type LucideIcon,
} from "lucide-react"
import { ChoiceButton } from "@/components/ui/choice-button"
import { Button } from "@/components/ui/button"
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuTrigger,
} from "@/components/ui/dropdown-menu"
import type { RouteConditionKey } from "@/pages/routing-rules-utils"

type Translate = (key: string, options?: Record<string, unknown>) => string

const conditionDetails: Record<
  RouteConditionKey,
  { label: string; icon: LucideIcon }
> = {
  list: { label: "lists", icon: Scroll },
  proto: { label: "protocol", icon: Network },
  dscp: { label: "dscp", icon: Tag },
  src_port: { label: "sourcePort", icon: SquareArrowRightExit },
  dest_port: { label: "destinationPort", icon: SquareArrowRightEnter },
  src_addr: { label: "sourceAddresses", icon: ArrowRightFromLine },
  dest_addr: { label: "destinationAddresses", icon: ArrowRightToLine },
}

// eslint-disable-next-line react-refresh/only-export-components -- used for matching picker and removal labels.
export function routeConditionTitle(key: RouteConditionKey, t: Translate) {
  return t(`pages.routingRuleUpsert.fields.${conditionDetails[key].label}`)
}

export function RoutingRuleConditionPicker({
  mode,
  available,
  onAdd,
  t,
}: {
  mode: "initial" | "additional"
  available: readonly RouteConditionKey[]
  onAdd: (key: RouteConditionKey) => void
  t: Translate
}) {
  const title = (key: RouteConditionKey) => routeConditionTitle(key, t)
  return mode === "initial" ? (
    <div data-slot="initial-condition-picker" className="space-y-3">
      <h3 className="text-sm font-medium">
        {t("pages.routingRuleUpsert.builder.chooseFirst")}
      </h3>
      <div className="grid w-full max-w-3xl min-w-0 gap-3 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4">
        {available.map((key) => {
          const Icon = conditionDetails[key].icon
          return (
            <ChoiceButton key={key} type="button" onClick={() => onAdd(key)}>
              <Icon aria-hidden="true" className="size-5 text-primary" />
              <span className="text-sm font-medium">{title(key)}</span>
              <span className="text-xs text-muted-foreground">
                {t(`pages.routingRuleUpsert.builder.descriptions.${key}`)}
              </span>
            </ChoiceButton>
          )
        })}
      </div>
    </div>
  ) : (
    <div
      data-slot="add-condition-picker"
      className="flex min-w-0 flex-wrap items-center gap-3"
    >
      {available.length > 0 ? (
        <DropdownMenu>
          <DropdownMenuTrigger
            render={
              <Button
                type="button"
                variant="outline"
                className="h-auto min-h-8 max-w-full text-left whitespace-normal"
              />
            }
          >
            <Plus aria-hidden="true" />
            {t("pages.routingRuleUpsert.builder.addAnother")}
          </DropdownMenuTrigger>
          <DropdownMenuContent
            aria-label={t("pages.routingRuleUpsert.builder.available")}
          >
            {available.map((key) => {
              const Icon = conditionDetails[key].icon
              return (
                <DropdownMenuItem key={key} onClick={() => onAdd(key)}>
                  <Icon aria-hidden="true" />
                  {title(key)}
                </DropdownMenuItem>
              )
            })}
          </DropdownMenuContent>
        </DropdownMenu>
      ) : (
        <span className="text-sm text-muted-foreground">
          {t("pages.routingRuleUpsert.builder.allAdded")}
        </span>
      )}
    </div>
  )
}
