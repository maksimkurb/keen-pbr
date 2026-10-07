import type { AnyFormApi } from "@tanstack/react-form"
import { Store, useStore } from "@tanstack/react-store"

import type { ApiError } from "@/api/client"
import {
  getApiValidationErrors,
  type ValidationErrorEntry,
} from "@/lib/api-errors"

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

type FormServerErrorState = {
  form: string | null
  fields: Record<string, string>
  unmapped: ValidationErrorEntry[]
}

const EMPTY_SERVER_ERRORS: FormServerErrorState = {
  form: null,
  fields: {},
  unmapped: [],
}

// Server-side validation errors live next to the form, not in TanStack's
// errorMap: there they would only reach fields mounted at that moment (a
// collapsed section loses its error) and, since nothing clears a field's
// onServer entry, would keep `canSubmit` false so the fixed form could never
// be sent again. Fields look their error up by name instead (getFieldError).
const serverErrorStores = new WeakMap<AnyFormApi, Store<FormServerErrorState>>()

function serverErrorStore(form: AnyFormApi) {
  let store = serverErrorStores.get(form)
  if (!store) {
    store = new Store<FormServerErrorState>(EMPTY_SERVER_ERRORS)
    serverErrorStores.set(form, store)
  }
  return store
}

export function setFormServerErrors(
  form: AnyFormApi,
  options: FormServerErrorMap
) {
  serverErrorStore(form).setState(() => ({
    form: options.form ?? null,
    fields: options.fields ?? {},
    unmapped: options.unmapped ?? [],
  }))
}

export function clearFormServerErrors(form: AnyFormApi) {
  serverErrorStore(form).setState(() => EMPTY_SERVER_ERRORS)
}

/**
 * Drops the server errors of a field and of everything below it
 * (`groups` also clears `groups[0].name`): an edited value is no longer what
 * the server rejected.
 */
export function clearFieldServerErrors(form: AnyFormApi, name: string) {
  const store = serverErrorStore(form)
  const fields = store.state.fields
  const remaining = Object.fromEntries(
    Object.entries(fields).filter(
      ([key]) =>
        key !== name &&
        !key.startsWith(`${name}[`) &&
        !key.startsWith(`${name}.`)
    )
  )
  if (Object.keys(remaining).length !== Object.keys(fields).length) {
    store.setState((state) => ({ ...state, fields: remaining }))
  }
}

/** Form `listeners` that clear a field's server error when the user edits it. */
export const clearServerErrorsOnChange = {
  onChange: ({ fieldApi }: { fieldApi: { form: AnyFormApi; name: string } }) =>
    clearFieldServerErrors(fieldApi.form, fieldApi.name),
}

/** Subscribes a component to the form's server errors. */
export function useFormServerErrors(form: AnyFormApi): FormServerErrorState {
  return useStore(serverErrorStore(form), (state) => state)
}

/** The server error of one field by name (null when there is none). */
export function getServerFieldError(
  form: AnyFormApi,
  name: string
): string | null {
  return serverErrorStore(form).state.fields[name] ?? null
}

/**
 * The error to show for a field: its own (client) validation error first,
 * then the server error addressed to its name. The page must subscribe with
 * useFormServerErrors so it re-renders when server errors change.
 */
export function getFieldError(field: {
  form: AnyFormApi
  name: string
  state: { meta: { errors: unknown[] } }
}): string | null {
  const clientError = field.state.meta.errors.find(
    (error): error is string => typeof error === "string" && error.length > 0
  )
  return clientError ?? getServerFieldError(field.form, field.name)
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
    if (
      !fieldPath ||
      (allowedFieldNames && !allowedFieldNames.has(fieldPath))
    ) {
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
