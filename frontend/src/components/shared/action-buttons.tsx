import type { ComponentProps, ReactNode } from "react"

import { IconButtonWithTooltip } from "@/components/shared/icon-button-with-tooltip"
import { ButtonGroup } from "@/components/ui/button-group"

type ActionButton = Omit<
  ComponentProps<typeof IconButtonWithTooltip>,
  "children"
> & {
  destructive?: boolean
  group?: string
  icon?: ReactNode
}

export function ActionButtons({
  actions,
  mobileIcons = false,
}: {
  actions: ActionButton[]
  mobileIcons?: boolean
}) {
  const renderedActions: ReactNode[] = []

  for (let actionIndex = 0; actionIndex < actions.length; ) {
    const action = actions[actionIndex]
    const group = action.group

    if (!group) {
      renderedActions.push(renderAction(action, actionIndex))
      actionIndex += 1
      continue
    }

    const groupedActions: ReactNode[] = []
    let groupedActionIndex = actionIndex
    while (
      groupedActionIndex < actions.length &&
      actions[groupedActionIndex].group === group
    ) {
      groupedActions.push(
        renderAction(actions[groupedActionIndex], groupedActionIndex)
      )
      groupedActionIndex += 1
    }

    renderedActions.push(
      <ButtonGroup key={`${group}-${actionIndex}`}>
        {groupedActions}
      </ButtonGroup>
    )
    actionIndex = groupedActionIndex
  }

  return (
    <>
      {mobileIcons ? (
        <div className="inline-flex justify-end md:hidden">
          {actions
            .filter((action) => !action.onDragStart)
            .map((action, index) => renderAction(action, index, true))}
        </div>
      ) : null}
      <div
        className={
          mobileIcons
            ? "ml-auto hidden justify-end gap-2 md:inline-flex"
            : "ml-auto inline-flex justify-end gap-2"
        }
      >
        {renderedActions}
      </div>
    </>
  )
}

function renderAction(
  action: ActionButton,
  actionIndex: number,
  mobile = false
) {
  const {
    destructive,
    group: _group,
    icon,
    label,
    className,
    size = "icon-sm",
    variant = "outline",
    ...props
  } = action
  void _group

  return (
    <IconButtonWithTooltip
      {...props}
      key={`${label}-${actionIndex}`}
      label={label}
      size={mobile ? "icon" : size}
      variant={mobile ? "ghost" : variant}
      className={
        mobile
          ? `size-11 ${destructive ? "text-destructive hover:text-destructive" : ""} ${className ?? ""}`
          : className
      }
    >
      {icon}
    </IconButtonWithTooltip>
  )
}
