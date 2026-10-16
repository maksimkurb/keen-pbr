import {
  Children,
  cloneElement,
  isValidElement,
  type ComponentProps,
  type ReactNode,
} from "react"
import {
  DropdownMenu,
  DropdownMenuTrigger,
  DropdownMenuContent,
  DropdownMenuItem,
} from "@/components/ui/dropdown-menu"
import { ArrowLeft, CopyCheck, EllipsisVertical } from "lucide-react"
import { useTranslation } from "react-i18next"

import { IconButtonWithTooltip } from "@/components/shared/icon-button-with-tooltip"
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip"
import { Button } from "@/components/ui/button"
import { Checkbox } from "@/components/ui/checkbox"
import type { useRowSelection } from "@/hooks/use-row-selection"

export function BulkSelectionToolbar({
  countLabel,
  children,
  selection,
  disabled = false,
}: {
  countLabel: string
  children: ReactNode
  selection: ReturnType<typeof useRowSelection>
  disabled?: boolean
}) {
  const { t } = useTranslation()
  const actions = Children.toArray(children)
    .filter(isValidElement<ComponentProps<typeof Button>>)
    .map((child) =>
      cloneElement(child, {
        disabled:
          disabled || selection.selectedCount === 0 || child.props.disabled,
      })
    )

  return (
    <div className="contents md:block">
      <div
        className="fixed inset-x-0 top-0 z-40 flex h-[calc(57px+env(safe-area-inset-top))] flex-nowrap items-center gap-1 border-b bg-background px-3 pt-[env(safe-area-inset-top)] pb-0 shadow-md motion-reduce:animate-none max-md:animate-in max-md:duration-200 max-md:slide-in-from-top-full md:static md:h-auto md:flex-wrap md:gap-2 md:rounded-md md:border md:bg-muted/20 md:px-3 md:py-2 md:shadow-none"
        data-testid="bulk-selection-toolbar"
      >
        <IconButtonWithTooltip
          label={t("common.cancel")}
          variant="ghost"
          className="size-11 shrink-0 md:hidden"
          disabled={disabled}
          onClick={selection.clear}
        >
          <ArrowLeft className="size-5" />
        </IconButtonWithTooltip>
        <span
          aria-atomic="true"
          aria-live="polite"
          className="min-w-0 flex-1 truncate text-sm font-medium tabular-nums md:flex-none"
          data-testid="bulk-selection-count"
        >
          <span className="md:hidden">
            {t("common.selection.selectedOfTotal", {
              count: selection.selectedCount,
              total: selection.totalCount,
            })}
          </span>
          <span className="hidden md:inline">{countLabel}</span>
        </span>
        <IconButtonWithTooltip
          label={t("common.selection.selectAll")}
          aria-pressed={selection.allVisibleSelected}
          disabled={disabled}
          variant={selection.allVisibleSelected ? "default" : "ghost"}
          className="size-11 shrink-0 md:hidden"
          onClick={() => selection.setAllVisible(!selection.allVisibleSelected)}
        >
          <CopyCheck className="size-5" />
        </IconButtonWithTooltip>
        <label className="hidden items-center gap-2 text-sm md:flex">
          <Checkbox
            checked={selection.allVisibleSelected}
            disabled={disabled}
            onCheckedChange={(checked) =>
              selection.setAllVisible(checked === true)
            }
          />
          {t("common.selection.selectAll")}
        </label>
        <div className="flex shrink-0 items-center gap-1 md:gap-2">
          <div className="hidden flex-wrap gap-2 md:flex">{actions}</div>
          <div className="md:hidden">
            <DropdownMenu>
              <Tooltip>
                <TooltipTrigger
                  render={
                    <DropdownMenuTrigger
                      render={
                        <Button
                          aria-label={t("common.rowActions")}
                          variant="ghost"
                          className="size-11"
                          disabled={disabled || selection.selectedCount === 0}
                        />
                      }
                    />
                  }
                >
                  <EllipsisVertical className="size-5" />
                </TooltipTrigger>
                <TooltipContent>{t("common.rowActions")}</TooltipContent>
              </Tooltip>
              <DropdownMenuContent>
                {actions.map((action, index) => (
                  <DropdownMenuItem
                    key={action.key ?? index}
                    nativeButton
                    disabled={action.props.disabled}
                    destructive={action.props.variant === "destructive"}
                    render={
                      <button
                        type="button"
                        onClick={action.props.onClick}
                        disabled={action.props.disabled}
                      />
                    }
                  >
                    {action.props.children}
                  </DropdownMenuItem>
                ))}
              </DropdownMenuContent>
            </DropdownMenu>
          </div>
          <Button
            className="hidden md:inline-flex"
            variant="outline"
            size="sm"
            disabled={disabled}
            onClick={selection.clear}
          >
            {t("common.cancel")}
          </Button>
        </div>
      </div>
    </div>
  )
}
