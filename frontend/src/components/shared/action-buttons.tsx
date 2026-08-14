import type { ComponentProps, ReactNode } from "react"

import { IconButtonWithTooltip } from "@/components/shared/icon-button-with-tooltip"
import { ButtonGroup } from "@/components/ui/button-group"

type ActionButton = Omit<
  ComponentProps<typeof IconButtonWithTooltip>,
  "children"
> & {
  group?: string
  icon?: ReactNode
}

export function ActionButtons({ actions }: { actions: ActionButton[] }) {
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
    <div className="ml-auto inline-flex justify-end gap-2">
      {renderedActions}
    </div>
  )
}

function renderAction(action: ActionButton, actionIndex: number) {
  const {
    group: _group,
    icon,
    label,
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
      size={size}
      variant={variant}
    >
      {icon}
    </IconButtonWithTooltip>
  )
}
