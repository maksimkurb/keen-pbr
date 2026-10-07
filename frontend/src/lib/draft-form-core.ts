/**
 * Framework-free core of `useDraftForm` (see draft-form.ts for the public API
 * and the migration guide): path helpers, error bookkeeping and the store.
 * Kept free of React so it can be unit-tested directly.
 */
import type { ApiError } from "@/api/client"
import {
  getApiValidationErrors,
  type ValidationErrorEntry,
} from "@/lib/api-errors"

// ---------------------------------------------------------------------------
// Typed paths
// ---------------------------------------------------------------------------

type SubPath<T> = T extends readonly (infer U)[]
  ? `[${number}]` | `[${number}]${SubPath<U>}`
  : T extends object
    ? {
        [K in keyof T & string]: `.${K}` | `.${K}${SubPath<T[K]>}`
      }[keyof T & string]
    : never

/**
 * Every valid path into `T`: `"tag"`, `"outboundGroups"`,
 * `"outboundGroups[0].members[1].weight"` (indices are any number).
 */
export type Path<T> = T extends object
  ? { [K in keyof T & string]: K | `${K}${SubPath<T[K]>}` }[keyof T & string]
  : never

/** The type of the value found at path `P` in `T`. */
export type PathValue<T, P extends string> = P extends `[${string}]${infer R}`
  ? T extends readonly (infer U)[]
    ? R extends ""
      ? U
      : PathValue<U, R>
    : never
  : P extends `.${infer R}`
    ? PathValue<T, R>
    : {
        [K in keyof T & string]: P extends K
          ? T[K]
          : P extends `${K}${infer R}`
            ? R extends `.${string}` | `[${string}`
              ? PathValue<T[K], R>
              : never
            : never
      }[keyof T & string]

// ---------------------------------------------------------------------------
// Path runtime helpers
// ---------------------------------------------------------------------------

/** `"a.b[2].c"` -> `["a", "b", "2", "c"]`. `a.2.c` parses the same way. */
export function parsePath(path: string): string[] {
  return path.match(/[^.[\]]+/g) ?? []
}

const isIndex = (segment: string) => /^\d+$/.test(segment)

/** Canonical spelling of a path: `["a","2","c"]` -> `"a[2].c"`. */
export function formatPath(segments: readonly string[]): string {
  return segments.reduce(
    (out, segment, i) =>
      isIndex(segment)
        ? `${out}[${segment}]`
        : i === 0
          ? segment
          : `${out}.${segment}`,
    ""
  )
}

/** `"a.2.c"` and `"a[2].c"` both become `"a[2].c"`. */
export function normalizePath(path: string): string {
  return formatPath(parsePath(path))
}

export function getIn(root: unknown, path: string): unknown {
  let current = root
  for (const segment of parsePath(path)) {
    if (current === null || typeof current !== "object") {
      return undefined
    }
    current = (current as Record<string, unknown>)[segment]
  }
  return current
}

/**
 * Immutable set: returns a new root sharing every untouched branch. Missing
 * containers are created (an array when the next segment is an index).
 * Returns `root` itself when the value is already identical.
 */
export function setIn<T>(root: T, path: string, value: unknown): T {
  const segments = parsePath(path)
  if (segments.length === 0) {
    return value as T
  }
  const write = (node: unknown, depth: number): unknown => {
    const segment = segments[depth]
    const container: Record<string, unknown> | unknown[] =
      node !== null && typeof node === "object"
        ? (node as Record<string, unknown>)
        : isIndex(segment)
          ? []
          : {}
    const child = (container as Record<string, unknown>)[segment]
    const nextChild =
      depth === segments.length - 1 ? value : write(child, depth + 1)
    if (Object.is(child, nextChild)) {
      return container
    }
    if (Array.isArray(container)) {
      const copy = container.slice()
      copy[Number(segment)] = nextChild
      return copy
    }
    return { ...container, [segment]: nextChild }
  }
  return write(root, 0) as T
}

export function deepEqual(a: unknown, b: unknown): boolean {
  if (Object.is(a, b)) {
    return true
  }
  if (
    typeof a !== "object" ||
    typeof b !== "object" ||
    a === null ||
    b === null
  ) {
    return false
  }
  if (Array.isArray(a) !== Array.isArray(b)) {
    return false
  }
  const keysA = Object.keys(a)
  const keysB = Object.keys(b)
  return (
    keysA.length === keysB.length &&
    keysA.every(
      (key) =>
        Object.prototype.hasOwnProperty.call(b, key) &&
        deepEqual(
          (a as Record<string, unknown>)[key],
          (b as Record<string, unknown>)[key]
        )
    )
  )
}

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

export type ServerErrors = {
  /** A message for the whole form (e.g. a non-validation API failure). */
  form?: string | null
  /** Messages by field path (`"tag"`, `"outboundGroups[0].members[1].weight"`). */
  fields?: Record<string, string>
  /** Validation errors that could not be mapped to a field. */
  unmapped?: ValidationErrorEntry[]
}

export type FormErrors = {
  form: string | null
  fields: Record<string, string>
  unmapped: ValidationErrorEntry[]
}

export const NO_ERRORS: FormErrors = { form: null, fields: {}, unmapped: [] }

export type ApiPathResolver = (
  path: string,
  message: string
) => string | undefined

/**
 * Splits an API failure into field errors (by form path), a form message (for
 * errors that carry no validation details) and the validation errors that
 * `resolvePath` could not map to a field. `fieldNames` optionally restricts
 * which resolved paths count as fields.
 */
export function splitFormApiErrors({
  error,
  fieldNames,
  resolvePath,
}: {
  error: ApiError | null
  fieldNames?: readonly string[]
  resolvePath: ApiPathResolver
}): {
  fieldErrors: Record<string, string>
  formError: string | null
  unmappedErrors: ValidationErrorEntry[]
} {
  if (!error) {
    return { fieldErrors: {}, formError: null, unmappedErrors: [] }
  }

  const validationErrors = getApiValidationErrors(error)
  if (validationErrors.length === 0) {
    return { fieldErrors: {}, formError: error.message, unmappedErrors: [] }
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

  return { fieldErrors, formError: null, unmappedErrors }
}

/** `splitFormApiErrors` shaped as `ServerErrors`, ready for `setServerErrors`. */
export function apiErrorsToServerErrors(
  error: ApiError | null,
  resolvePath: ApiPathResolver,
  fieldNames?: readonly string[]
): ServerErrors {
  const { fieldErrors, formError, unmappedErrors } = splitFormApiErrors({
    error,
    fieldNames,
    resolvePath,
  })
  return { form: formError, fields: fieldErrors, unmapped: unmappedErrors }
}

/** True when `path` is `ancestor` itself or lies below it (`a` -> `a[0].b`, not `aExtra`). */
export function isAtOrBelow(path: string, ancestor: string): boolean {
  const segments = parsePath(path)
  const base = parsePath(ancestor)
  return (
    base.length <= segments.length &&
    base.every((segment, i) => segments[i] === segment)
  )
}

function normalizeFields(
  fields: Record<string, string | null | undefined> | undefined
): Record<string, string> {
  const out: Record<string, string> = {}
  for (const [path, message] of Object.entries(fields ?? {})) {
    if (message) {
      out[normalizePath(path)] = message
    }
  }
  return out
}

function filterFields(
  fields: Record<string, string>,
  keep: (path: string) => boolean
): Record<string, string> {
  const entries = Object.entries(fields)
  const kept = entries.filter(([path]) => keep(path))
  return kept.length === entries.length ? fields : Object.fromEntries(kept)
}

// ---------------------------------------------------------------------------
// Store
// ---------------------------------------------------------------------------

export type DraftState<T> = {
  values: T
  /** What `isDirty` compares against (the last initial/reset value). */
  baseline: T
  errors: FormErrors
  isSubmitting: boolean
}

/**
 * Holds the draft synchronously (reads are never stale, so several
 * `setValue` calls in one handler compose) and notifies subscribers.
 */
export function createDraftStore<T>(initial: T) {
  let state: DraftState<T> = {
    values: initial,
    baseline: initial,
    errors: NO_ERRORS,
    isSubmitting: false,
  }
  let seenInitial = initial
  let validate:
    | ((values: T) => Record<string, string | null | undefined>)
    | undefined
  const listeners = new Set<() => void>()

  const set = (next: Partial<DraftState<T>>) => {
    state = { ...state, ...next }
    listeners.forEach((listener) => listener())
  }

  const setValues = (next: T | ((previous: T) => T)) => {
    const previous = state.values
    const values =
      typeof next === "function" ? (next as (v: T) => T)(previous) : next
    if (Object.is(values, previous)) {
      return
    }
    // Drop the errors of everything that changed (an edited, removed or
    // shifted array row no longer is what the server rejected).
    const fields = filterFields(state.errors.fields, (path) =>
      deepEqual(getIn(previous, path), getIn(values, path))
    )
    set({
      values,
      errors:
        fields === state.errors.fields
          ? state.errors
          : { ...state.errors, fields },
    })
  }

  return {
    getState: () => state,
    subscribe(listener: () => void) {
      listeners.add(listener)
      return () => {
        listeners.delete(listener)
      }
    },

    setValidate(
      next:
        | ((values: T) => Record<string, string | null | undefined>)
        | undefined
    ) {
      validate = next
    },

    /** Sets one path and clears its error and the errors below it. */
    setValue(path: string, value: unknown) {
      const values = setIn(state.values, path, value)
      const fields = filterFields(
        state.errors.fields,
        (errorPath) => !isAtOrBelow(errorPath, path)
      )
      set({
        values,
        errors:
          fields === state.errors.fields
            ? state.errors
            : { ...state.errors, fields },
      })
    },

    setValues,

    /** Replaces the draft (and what `isDirty` compares to) with `next`, default: the current baseline. */
    reset(next?: T) {
      const baseline = next ?? state.baseline
      set({ values: baseline, baseline, errors: NO_ERRORS })
    },

    /**
     * Called with the caller's `initial` on every render: a changed initial
     * (e.g. refetched config) replaces the draft, but only while it has no
     * unsaved edits.
     */
    syncInitial(initialNow: T) {
      if (deepEqual(initialNow, seenInitial)) {
        return
      }
      seenInitial = initialNow
      if (!deepEqual(state.values, state.baseline)) {
        return
      }
      set({ values: initialNow, baseline: initialNow, errors: NO_ERRORS })
    },

    setServerErrors(errors: ServerErrors) {
      set({
        errors: {
          form: errors.form ?? null,
          fields: normalizeFields(errors.fields),
          unmapped: errors.unmapped ?? [],
        },
      })
    },

    clearServerErrors() {
      if (state.errors !== NO_ERRORS) {
        set({ errors: NO_ERRORS })
      }
    },

    errorFor(path: string): string | null {
      return state.errors.fields[normalizePath(path)] ?? null
    },

    /**
     * Clears errors, runs the optional client `validate` (a failure shows its
     * messages and skips `handler`), then awaits `handler(values)` with
     * `isSubmitting` set. Ignored while a submit is already running.
     */
    async submit(handler: (values: T) => void | Promise<void>) {
      if (state.isSubmitting) {
        return
      }
      const values = state.values
      const clientErrors = validate?.(values) ?? {}
      const fields = normalizeFields(clientErrors)
      if (Object.keys(fields).length > 0) {
        set({ errors: { form: null, fields, unmapped: [] } })
        return
      }
      set({ errors: NO_ERRORS, isSubmitting: true })
      try {
        await handler(values)
      } finally {
        set({ isSubmitting: false })
      }
    },
  }
}

export type DraftStore<T> = ReturnType<typeof createDraftStore<T>>
