import type { ReactNode } from "react"

export function PageHeader({
  title,
  description,
  actions,
}: {
  title: string
  description: string
  actions?: ReactNode
}) {
  return (
    <div className="mb-6 flex flex-col gap-4 md:flex-row md:items-start md:justify-between">
      <div>
        <h1 className="text-3xl font-semibold tracking-tight md:text-2xl">{title}</h1>
        <p className="mt-1 text-base text-muted-foreground md:text-sm">{description}</p>
      </div>
      {actions}
    </div>
  )
}
