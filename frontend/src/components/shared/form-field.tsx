import type { ReactNode } from "react"

import { Label } from "@/components/ui/label"

export function FormField({
  label,
  description,
  htmlFor,
  children,
}: {
  label: string
  description?: ReactNode
  htmlFor?: string
  children: ReactNode
}) {
  return (
    <div className="space-y-2">
      <Label htmlFor={htmlFor}>{label}</Label>
      {children}
      {description ? (
        <p className="text-sm text-slate-500">{description}</p>
      ) : null}
    </div>
  )
}
