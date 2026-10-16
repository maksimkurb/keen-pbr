import { Inbox, TriangleAlert } from "lucide-react"
import type { ReactNode } from "react"

import {
  Empty,
  EmptyContent,
  EmptyDescription,
  EmptyHeader,
  EmptyMedia,
  EmptyTitle,
} from "@/components/ui/empty"

export function ListPlaceholder({
  title,
  description,
  action,
  variant = "empty",
}: {
  title: string
  description: string
  action?: ReactNode
  variant?: "empty" | "error"
}) {
  const Icon = variant === "error" ? TriangleAlert : Inbox

  return (
    <Empty
      className="min-h-56 justify-center border sm:min-h-0"
      data-testid={`list-placeholder-${variant}`}
    >
      <EmptyHeader>
        <EmptyMedia variant="icon">
          <Icon />
        </EmptyMedia>
        <EmptyTitle>{title}</EmptyTitle>
        <EmptyDescription>{description}</EmptyDescription>
      </EmptyHeader>
      {action ? <EmptyContent>{action}</EmptyContent> : null}
    </Empty>
  )
}
