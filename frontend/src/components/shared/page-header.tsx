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
    <div className="mb-4 flex items-start justify-between gap-3">
      <div>
        <h1 className="text-5xl font-semibold tracking-tight">{title}</h1>
        <p className="mt-1 text-xl text-slate-600">{description}</p>
      </div>
      {actions}
    </div>
  )
}
