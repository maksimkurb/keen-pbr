import type { ApiError } from "@/api/client"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { getApiErrorMessage } from "@/lib/api-errors"

export function ConfigSaveErrorAlert({ error }: { error: unknown }) {
  const message = getApiErrorMessage(error as ApiError | null)

  if (!message) {
    return null
  }

  return (
    <Alert variant="destructive">
      <AlertDescription className="whitespace-pre-wrap">
        {message}
      </AlertDescription>
    </Alert>
  )
}
