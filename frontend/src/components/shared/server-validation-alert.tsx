import type { ValidationErrorEntry } from "@/lib/api-errors"
import { Alert, AlertDescription } from "@/components/ui/alert"

export function ServerValidationAlert({
  errors,
  message,
}: {
  errors: ValidationErrorEntry[]
  message?: string | null
}) {
  if (errors.length === 0 && !message) {
    return null
  }

  return (
    <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
      <AlertDescription>
        <div className="space-y-2">
          {message ? <div className="whitespace-pre-wrap">{message}</div> : null}
          {errors.length > 0 ? (
            <ul className="list-disc space-y-1 pl-5">
              {errors.map((error, index) => (
                <li key={`${error.path}-${error.message}-${index}`}>
                  {error.path ? `${error.path}: ${error.message}` : error.message}
                </li>
              ))}
            </ul>
          ) : null}
        </div>
      </AlertDescription>
    </Alert>
  )
}
