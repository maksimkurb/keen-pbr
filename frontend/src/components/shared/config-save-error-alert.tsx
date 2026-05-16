import type { ApiError } from "@/api/client"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { getApiErrorMessage } from "@/lib/api-errors"

/** Inline alert when staged config save fails (complements toast feedback). */
export function ConfigSaveErrorAlert({ error }: { error: unknown }) {
  const message = getApiErrorMessage(error as ApiError | null)
  if (!message) {
    return null
  }

  return (
    <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
      <AlertDescription className="whitespace-pre-wrap">{message}</AlertDescription>
    </Alert>
  )
}
