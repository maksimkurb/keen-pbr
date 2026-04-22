import type { AnyFormApi } from "@tanstack/react-form"

import type { ApiError } from "@/api/client"
import { getApiValidationErrors, type ValidationErrorEntry } from "@/lib/api-errors"

export type ApiPathResolver = (
  path: string,
  message: string
) => string | undefined

export type FormServerErrorMap = {
  form?: string
  fields?: Record<string, string>
  unmapped?: ValidationErrorEntry[]
}

type ApplyFormApiErrorsOptions = {
  error: ApiError | null
  form: AnyFormApi
  fieldNames?: readonly string[]
  resolvePath: ApiPathResolver
}

type SplitFormApiErrorsOptions = {
  error: ApiError | null
  fieldNames?: readonly string[]
  resolvePath: ApiPathResolver
}

export type SplitFormApiErrorsResult = {
  fieldErrors: Record<string, string>
  formError: string | null
  unmappedErrors: ValidationErrorEntry[]
}

export function setFormServerErrors(
  form: AnyFormApi,
  options: FormServerErrorMap
) {
  form.setErrorMap({
    onServer: {
      form: options.form,
      fields: options.fields ?? {},
      unmapped: options.unmapped ?? [],
    },
  })
}

export function clearFormServerErrors(form: AnyFormApi) {
  setFormServerErrors(form, {
    form: undefined,
    fields: {},
    unmapped: [],
  })
}

export function splitFormApiErrors({
  error,
  fieldNames,
  resolvePath,
}: SplitFormApiErrorsOptions): SplitFormApiErrorsResult {
  if (!error) {
    return {
      fieldErrors: {},
      formError: null,
      unmappedErrors: [],
    }
  }

  const validationErrors = getApiValidationErrors(error)
  if (validationErrors.length === 0) {
    return {
      fieldErrors: {},
      formError: error.message,
      unmappedErrors: [],
    }
  }

  const fieldErrors: Record<string, string> = {}
  const allowedFieldNames = fieldNames ? new Set(fieldNames) : null
  const unmappedErrors: ValidationErrorEntry[] = []

  for (const item of validationErrors) {
    const fieldPath = resolvePath(item.path, item.message)
    if (!fieldPath || (allowedFieldNames && !allowedFieldNames.has(fieldPath))) {
      unmappedErrors.push(item)
      continue
    }

    fieldErrors[fieldPath] = fieldErrors[fieldPath]
      ? `${fieldErrors[fieldPath]} ${item.message}`
      : item.message
  }

  return {
    fieldErrors,
    formError: null,
    unmappedErrors,
  }
}

export function applyFormApiErrors({
  error,
  form,
  fieldNames,
  resolvePath,
}: ApplyFormApiErrorsOptions): string | null {
  clearFormServerErrors(form)

  const { fieldErrors, formError, unmappedErrors } = splitFormApiErrors({
    error,
    fieldNames,
    resolvePath,
  })

  setFormServerErrors(form, {
    form: formError ?? undefined,
    fields: fieldErrors,
    unmapped: unmappedErrors,
  })

  return formError
}
