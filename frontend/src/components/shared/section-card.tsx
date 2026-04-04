import type { ReactNode } from "react"

import {
  Card,
  CardDescription,
  CardContent,
  CardHeader,
  CardTitle,
} from "@/components/ui/card"
import { cn } from "@/lib/utils"

export function SectionCard({
  title,
  children,
  action,
  description,
  className,
  contentClassName,
}: {
  title: string
  children: ReactNode
  action?: ReactNode
  description?: ReactNode
  className?: string
  contentClassName?: string
}) {
  return (
    <Card className={cn(className)}>
      <CardHeader>
        <div className="flex items-center justify-between gap-3">
          <div className="space-y-1">
            <CardTitle>{title}</CardTitle>
            {description ? (
              <CardDescription>{description}</CardDescription>
            ) : null}
          </div>
          {action}
        </div>
      </CardHeader>
      <CardContent className={cn("space-y-3", contentClassName)}>
        {children}
      </CardContent>
    </Card>
  )
}
