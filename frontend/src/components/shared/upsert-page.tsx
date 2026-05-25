import type { ReactNode } from "react"

import { PageHeader } from "@/components/shared/page-header"
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card"
import { useIsMobile } from "@/hooks/use-mobile"

export function UpsertPage({
  title,
  description,
  cardTitle,
  cardDescription,
  children,
}: {
  title: string
  description: string
  cardTitle: string
  cardDescription: string
  children: ReactNode
}) {
  const isMobile = useIsMobile()

  return (
    <div className="space-y-5 md:space-y-6">
      <PageHeader description={description} title={title} />
      <Card size={isMobile ? "sm" : "default"}>
        <CardHeader>
          <CardTitle>{cardTitle}</CardTitle>
          <CardDescription>{cardDescription}</CardDescription>
        </CardHeader>
        <CardContent>{children}</CardContent>
      </Card>
    </div>
  )
}
