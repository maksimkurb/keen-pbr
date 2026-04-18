import type { AnyFormApi } from "@tanstack/react-form"

import type { ApiError } from "@/api/client"
import {
  formatValidationErrors,
  getApiValidationErrors,
} from "@/lib/api-errors"

export type ApiPathResolver = (path: string) => string | undefined

type ApplyFormApiErrorsOptions = {
  error: ApiError | null
  form: AnyFormApi
  resolvePath: ApiPathResolver
}

export function setFormServerErrors(
  form: AnyFormApi,
  options: {
    form?: string
    fields?: Record<string, string>
  }
) {
  form.setErrorMap({
    onServer: {
      form: options.form,
      fields: options.fields ?? {},
    },
  })
}

export function clearFormServerErrors(form: AnyFormApi) {
  setFormServerErrors(form, {
    form: undefined,
    fields: {},
  })
}

export function applyFormApiErrors({
  error,
  form,
  resolvePath,
}: ApplyFormApiErrorsOptions): string | null {
  clearFormServerErrors(form)

  if (!error) {
    return null
  }

  const validationErrors = getApiValidationErrors(error)
  if (validationErrors.length === 0) {
    setFormServerErrors(form, {
      form: error.message,
      fields: {},
    })
    return error.message
  }

  const fieldErrors: Record<string, string> = {}
  const globalErrors: typeof validationErrors = []

  for (const item of validationErrors) {
    const fieldPath = resolvePath(item.path)
    if (!fieldPath) {
      globalErrors.push(item)
      continue
    }

    fieldErrors[fieldPath] = fieldErrors[fieldPath]
      ? `${fieldErrors[fieldPath]} ${item.message}`
      : item.message
  }

  const formError =
    globalErrors.length === 0 ? undefined : formatValidationErrors(globalErrors)

  setFormServerErrors(form, {
    form: formError,
    fields: fieldErrors,
  })

  if (!formError) {
    return null
  }

  return formError
}
