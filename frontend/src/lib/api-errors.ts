import type { ApiError } from "@/api/client"

export type ValidationErrorEntry = {
  path: string
  message: string
}

function getValidationErrors(details: unknown): ValidationErrorEntry[] {
  if (!details || typeof details !== "object") {
    return []
  }

  const validationErrors = (details as { validation_errors?: unknown })
    .validation_errors

  if (!Array.isArray(validationErrors)) {
    return []
  }

  return validationErrors.filter(
    (item): item is ValidationErrorEntry =>
      Boolean(item) &&
      typeof item === "object" &&
      typeof (item as { path?: unknown }).path === "string" &&
      typeof (item as { message?: unknown }).message === "string"
  )
}

export function getApiValidationErrors(error: ApiError | null): ValidationErrorEntry[] {
  if (!error) {
    return []
  }

  return getValidationErrors(error.details).map((item) => ({
    path: item.path.trim(),
    message: item.message.trim(),
  }))
}

export function formatValidationErrors(errors: ValidationErrorEntry[]): string {
  return errors
    .map((item) =>
      item.path ? `- ${item.path}: ${item.message}` : `- ${item.message}`
    )
    .join("\n")
}

export function getApiErrorMessage(error: ApiError | null): string {
  if (!error) {
    return ""
  }

  const validationErrors = getApiValidationErrors(error)
  if (validationErrors.length === 0) {
    return error.message
  }

  return [error.message, formatValidationErrors(validationErrors)].join("\n")
}
