import type { ReactNode } from "react"
import { ArrowLeft } from "lucide-react"
import { useTranslation } from "react-i18next"
import { Link } from "wouter"

import { Button } from "@/components/ui/button"
import { cn } from "@/lib/utils"

export function PageHeader({
  title,
  description,
  actions,
  backTo,
  className,
}: {
  title: ReactNode
  description?: string
  actions?: ReactNode
  backTo?: string
  className?: string
}) {
  return (
    <header
      className={cn(
        "mb-6 grid grid-cols-1 gap-x-4 gap-y-2 border-b pb-4 md:mb-8 md:grid-cols-[minmax(0,1fr)_auto] md:items-start",
        className
      )}
    >
      <div className="flex min-w-0 items-start gap-2">
        {backTo ? <PageHeaderBackButton href={backTo} /> : null}
        <h1
          className="min-w-0 text-3xl font-semibold tracking-tight text-balance md:text-2xl"
          id="page-title"
        >
          {title}
        </h1>
      </div>
      {actions ? (
        <div className="order-3 min-w-0 pt-2 md:order-none md:pt-0">
          {actions}
        </div>
      ) : null}
      {description ? (
        <p className="min-w-0 text-base text-pretty text-muted-foreground md:col-span-2 md:text-sm">
          {description}
        </p>
      ) : null}
    </header>
  )
}

function PageHeaderBackButton({ href }: { href: string }) {
  const { t } = useTranslation()
  const label = t("common.back")

  return (
    <Button
      aria-label={label}
      className="mt-0.5 -ml-1.5 shrink-0 md:mt-0"
      render={<Link href={href} />}
      size="icon"
      title={label}
      variant="ghost"
    >
      <ArrowLeft aria-hidden="true" className="size-5" />
    </Button>
  )
}
