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
import { createPortal } from "react-dom"
import { useTranslation } from "react-i18next"

import { IconButtonWithTooltip } from "@/components/shared/icon-button-with-tooltip"
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip"
import { Button } from "@/components/ui/button"
import type { useRowSelection } from "@/hooks/use-row-selection"

export function BulkSelectionToolbar({
  countLabel,
  children,
  selection,
  disabled = false,
  active = true,
}: {
  countLabel: string
  children: ReactNode
  selection: ReturnType<typeof useRowSelection>
  disabled?: boolean
  active?: boolean
}) {
  const { t } = useTranslation()
  const actions = Children.toArray(children)
    .filter(isValidElement<ComponentProps<typeof Button>>)
    .map((child) =>
      cloneElement(child, {
        size: "xs",
        disabled:
          disabled || selection.selectedCount === 0 || child.props.disabled,
      })
    )

  const desktopBar = (
    <div
      className="flex w-full min-w-0 items-center gap-3 motion-safe:animate-in motion-safe:duration-200 motion-safe:fade-in motion-safe:slide-in-from-bottom-1"
      data-testid="bulk-selection-toolbar-desktop"
    >
      <span
        aria-atomic="true"
        aria-live="polite"
        className="text-sm font-medium whitespace-nowrap tabular-nums"
      >
        {countLabel}
      </span>
      <div className="flex flex-wrap items-center gap-2">{actions}</div>
      <Button
        className="ml-auto"
        variant="outline"
        size="xs"
        disabled={disabled}
        onClick={selection.clear}
      >
        {t("common.cancel")}
      </Button>
    </div>
  )

  return (
    <>
      {active && selection.barSlot
        ? createPortal(desktopBar, selection.barSlot)
        : null}
      {active ? (
        <div
          className="fixed inset-x-0 top-0 z-40 flex h-[calc(57px+env(safe-area-inset-top))] animate-in flex-nowrap items-center gap-1 border-b bg-background px-3 pt-[env(safe-area-inset-top)] pb-0 shadow-md duration-200 slide-in-from-top-full motion-reduce:animate-none md:hidden"
          data-testid="bulk-selection-toolbar"
        >
          <IconButtonWithTooltip
            label={t("common.cancel")}
            variant="ghost"
            className="size-11 shrink-0"
            disabled={disabled}
            onClick={selection.clear}
          >
            <ArrowLeft className="size-5" />
          </IconButtonWithTooltip>
          <span
            aria-atomic="true"
            aria-live="polite"
            className="min-w-0 flex-1 truncate text-sm font-medium tabular-nums"
            data-testid="bulk-selection-count"
          >
            {t("common.selection.selectedOfTotal", {
              count: selection.selectedCount,
              total: selection.totalCount,
            })}
          </span>
          <IconButtonWithTooltip
            label={t("common.selection.selectAll")}
            aria-pressed={selection.allVisibleSelected}
            disabled={disabled}
            variant={selection.allVisibleSelected ? "default" : "ghost"}
            className="size-11 shrink-0"
            onClick={() =>
              selection.setAllVisible(!selection.allVisibleSelected)
            }
          >
            <CopyCheck className="size-5" />
          </IconButtonWithTooltip>
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
      ) : null}
    </>
  )
}
