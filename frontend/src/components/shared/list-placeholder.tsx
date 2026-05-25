import { Inbox, TriangleAlert } from "lucide-react"

import {
  Empty,
  EmptyDescription,
  EmptyHeader,
  EmptyMedia,
  EmptyTitle,
} from "@/components/ui/empty"

export function ListPlaceholder({
  title,
  description,
  variant = "empty",
}: {
  title: string
  description: string
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
    </Empty>
  )
}
