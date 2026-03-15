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
    <Empty className="border">
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
