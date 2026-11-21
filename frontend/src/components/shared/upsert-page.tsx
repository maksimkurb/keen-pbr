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
  withCard = false,
  backTo,
}: {
  title: ReactNode
  description: string
  cardTitle?: string
  cardDescription?: string
  children: ReactNode
  withCard?: boolean
  backTo?: string
}) {
  const isMobile = useIsMobile()

  return (
    <div className="space-y-5 md:space-y-6">
      <PageHeader backTo={backTo} description={description} title={title} />
      {withCard ? (
        <Card size={isMobile ? "sm" : "default"}>
          {cardTitle || cardDescription ? (
            <CardHeader>
              {cardTitle ? <CardTitle>{cardTitle}</CardTitle> : null}
              {cardDescription ? (
                <CardDescription>{cardDescription}</CardDescription>
              ) : null}
            </CardHeader>
          ) : null}
          <CardContent>{children}</CardContent>
        </Card>
      ) : (
        children
      )}
    </div>
  )
}
