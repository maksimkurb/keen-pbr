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

const PATH_TOKEN = /\["((?:[^"\\]|\\.)*)"\]|\[(\d+)\]|[^.[\]]+/g
const PLAIN_KEY = /^[A-Za-z0-9_-]+$/
const isIndex = (segment: string) => /^\d+$/.test(segment)

/**
 * Splits a path into segments. Understands `a.b`, `a[2]` (and `a.2`) and
 * `a["any key"]`: `"a.b[2].c"` -> `["a", "b", "2", "c"]`.
 */
export function parsePath(path: string): string[] {
  const segments: string[] = []
  for (const match of path.matchAll(PATH_TOKEN)) {
    segments.push(
      match[1] !== undefined
        ? match[1].replace(/\\(.)/g, "$1")
        : (match[2] ?? match[0])
    )
  }
  return segments
}

/** Canonical spelling: `a.b[2].c`, keys that are not plain use `["key"]`. */
export function formatPath(segments: readonly string[]): string {
  return segments.reduce((out, segment, i) => {
    if (isIndex(segment)) return `${out}[${segment}]`
    if (PLAIN_KEY.test(segment)) return i === 0 ? segment : `${out}.${segment}`
    return `${out}["${segment.replace(/(["\\])/g, "\\$1")}"]`
  }, "")
}

/** `"a.2.c"`, `"a[2].c"` and `'a["2"].c'` all become `"a[2].c"`. */
export function normalizePath(path: string): string {
  return formatPath(parsePath(path))
}

/** Normalized `prefix` + `path` (either may be empty). */
export function joinPath(prefix: string, path: string): string {
  if (!prefix) return normalizePath(path)
  if (!path) return normalizePath(prefix)
  return normalizePath(
    path.startsWith("[") ? `${prefix}${path}` : `${prefix}.${path}`
  )
}

/** `path` relative to `prefix`, or null when it is not at/below it. */
export function relativePath(prefix: string, path: string): string | null {
  const base = parsePath(prefix)
  const segments = parsePath(path)
  if (
    base.length > segments.length ||
    !base.every((segment, i) => segments[i] === segment)
  ) {
    return null
  }
  return formatPath(segments.slice(base.length))
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

/** Errors for `setServerErrors`. Field paths are relative to the draft. */
export type ServerErrors = {
  /** A message for the whole form (e.g. a non-validation API failure). */
  form?: string | null
  /** Messages by draft path (`"tag"`, `"outbound_groups[0].members[1].weight"`). */
  fields?: Record<string, string | null | undefined>
  /** Extra entries shown in the unmapped list as they are. */
  unmapped?: ValidationErrorEntry[]
}

export type FormErrors = {
  form: string | null
  /** By ABSOLUTE API path (`apiPrefix` included), normalized. */
  fields: Record<string, string>
  extra: ValidationErrorEntry[]
}

export const NO_ERRORS: FormErrors = { form: null, fields: {}, extra: [] }

/**
 * API validation errors of a failed save: every entry keeps its exact path
 * (normalized, messages on the same path are joined). A failure without
 * validation details becomes the form message.
 */
export function apiErrorsToFormErrors(error: ApiError | null): FormErrors {
  if (!error) {
    return NO_ERRORS
  }
  const entries = getApiValidationErrors(error)
  if (entries.length === 0) {
    return { form: error.message, fields: {}, extra: [] }
  }
  const fields: Record<string, string> = {}
  for (const { path, message } of entries) {
    const key = normalizePath(path)
    fields[key] = fields[key] ? `${fields[key]} ${message}` : message
  }
  return { form: null, fields, extra: [] }
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

/** The closest claimed path that is `path` or one of its ancestors. */
export function nearestClaimed(
  path: string,
  claims: ReadonlySet<string>
): string | null {
  const segments = parsePath(path)
  for (let length = segments.length; length > 0; length--) {
    const candidate = formatPath(segments.slice(0, length))
    if (claims.has(candidate)) {
      return candidate
    }
  }
  return claims.has("") ? "" : null
}

export type RoutedErrors = {
  /** Messages by the claimed path that shows them. */
  byClaim: Record<string, string>
  /** Errors no claimed path is responsible for. */
  unmapped: ValidationErrorEntry[]
}

/**
 * Routes every error to the closest claimed path (the path itself, or the
 * nearest ancestor a rendered control claimed); the rest is unmapped.
 */
export function routeErrors(
  fields: Record<string, string>,
  claims: ReadonlySet<string>
): RoutedErrors {
  const byClaim: Record<string, string> = {}
  const unmapped: ValidationErrorEntry[] = []
  for (const [path, message] of Object.entries(fields)) {
    const claim = nearestClaimed(path, claims)
    if (claim === null) {
      unmapped.push({ path, message })
    } else {
      byClaim[claim] = byClaim[claim] ? `${byClaim[claim]} ${message}` : message
    }
  }
  return { byClaim, unmapped }
}

function toAbsolute(
  prefix: string,
  fields: Record<string, string | null | undefined> | undefined
): Record<string, string> {
  const out: Record<string, string> = {}
  for (const [path, message] of Object.entries(fields ?? {})) {
    if (message) {
      const key = joinPath(prefix, path)
      out[key] = out[key] ? `${out[key]} ${message}` : message
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

type ClientErrors = Record<string, string | null | undefined>

/** Options of `errorFor` / `field` for controls that own more than one path. */
export type ClaimOptions = {
  /** Additional draft paths whose errors this control shows too. */
  alsoClaims?: readonly string[]
}

/**
 * Holds the draft synchronously (reads are never stale, so several
 * `setValue` calls in one handler compose) and notifies subscribers.
 *
 * Errors are kept by absolute API path. A path is shown by the control that
 * claims it (`errorFor` / `field` during render) or, failing that, by the
 * nearest claimed ancestor; unclaimed errors are `unmapped()`. Claims come
 * from the previous committed render (`commitClaims`), because an error can
 * only be routed once every control of the current render is known.
 */
export function createDraftStore<T>(
  initial: T,
  getPrefix: (values: T) => string = () => ""
) {
  let state: DraftState<T> = {
    values: initial,
    baseline: initial,
    errors: NO_ERRORS,
    isSubmitting: false,
  }
  let seenInitial = initial
  let validate: ((values: T) => ClientErrors) | undefined
  let prefixOf = getPrefix
  let claims: ReadonlySet<string> = new Set()
  let pending = new Set<string>()
  let routed: {
    fields: Record<string, string>
    claims: ReadonlySet<string>
    value: RoutedErrors
  } | null = null
  const listeners = new Set<() => void>()

  const set = (next: Partial<DraftState<T>>) => {
    state = { ...state, ...next }
    listeners.forEach((listener) => listener())
  }
  const prefix = () => normalizePath(prefixOf(state.values))
  const absolute = (path: string) => joinPath(prefix(), path)
  const routes = (): RoutedErrors => {
    if (
      !routed ||
      routed.fields !== state.errors.fields ||
      routed.claims !== claims
    ) {
      routed = {
        fields: state.errors.fields,
        claims,
        value: routeErrors(state.errors.fields, claims),
      }
    }
    return routed.value
  }
  const withFields = (fields: Record<string, string>): FormErrors =>
    fields === state.errors.fields ? state.errors : { ...state.errors, fields }

  const setValues = (next: T | ((previous: T) => T)) => {
    const previous = state.values
    const values =
      typeof next === "function" ? (next as (v: T) => T)(previous) : next
    if (Object.is(values, previous)) {
      return
    }
    // Drop the errors of everything that changed (an edited, removed or
    // shifted array row no longer is what the server rejected).
    const base = prefix()
    const fields = filterFields(state.errors.fields, (path) => {
      const rel = relativePath(base, path)
      return rel === null || deepEqual(getIn(previous, rel), getIn(values, rel))
    })
    set({ values, errors: withFields(fields) })
  }

  const api = {
    getState: () => state,
    subscribe(listener: () => void) {
      listeners.add(listener)
      return () => {
        listeners.delete(listener)
      }
    },

    setValidate(next: ((values: T) => ClientErrors) | undefined) {
      validate = next
    },

    setPrefix(next: (values: T) => string) {
      prefixOf = next
    },

    /** Sets one path and clears its error and the errors below it. */
    setValue(path: string, value: unknown) {
      const values = setIn(state.values, path, value)
      const target = absolute(path)
      const fields = filterFields(
        state.errors.fields,
        (errorPath) => !isAtOrBelow(errorPath, target)
      )
      set({ values, errors: withFields(fields) })
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

    /** Errors by DRAFT path (relative to `apiPrefix`). */
    setServerErrors(errors: ServerErrors) {
      set({
        errors: {
          form: errors.form ?? null,
          fields: toAbsolute(prefix(), errors.fields),
          extra: errors.unmapped ?? [],
        },
      })
    },

    /** Errors as the API reported them (absolute paths). */
    setApiError(error: ApiError): string | null {
      const errors = apiErrorsToFormErrors(error)
      set({ errors })
      return errors.form
    },

    clearServerErrors() {
      if (state.errors !== NO_ERRORS) {
        set({ errors: NO_ERRORS })
      }
    },

    // -- reading errors (claims the path for the current render) ----------

    /** Call at the start of every render of the form's component. */
    beginRender() {
      pending = new Set()
    },

    /** Claims draft paths for the current render. */
    claim(path: string, options?: ClaimOptions) {
      pending.add(absolute(path))
      for (const extra of options?.alsoClaims ?? []) {
        pending.add(absolute(extra))
      }
    },

    /** Call after every commit: adopts this render's claims. */
    commitClaims() {
      const changed =
        pending.size !== claims.size ||
        [...pending].some((path) => !claims.has(path))
      if (!changed) {
        return
      }
      claims = pending
      // Errors may now belong to different controls.
      if (Object.keys(state.errors.fields).length > 0) {
        listeners.forEach((listener) => listener())
      }
    },

    errorFor(path: string, options?: ClaimOptions): string | null {
      api.claim(path, options)
      const { byClaim } = routes()
      const own = byClaim[absolute(path)]
      const extra = (options?.alsoClaims ?? [])
        .map((other) => byClaim[absolute(other)])
        .filter(Boolean)
      const all = [own, ...extra].filter(Boolean)
      return all.length > 0 ? all.join(" ") : null
    },

    /** Errors no rendered control claimed (read it after the controls rendered). */
    unmapped(): ValidationErrorEntry[] {
      return [...routes().unmapped, ...state.errors.extra]
    },

    /** Draft paths (relative to `apiPrefix`) of all current field errors. */
    errorPaths(): string[] {
      const base = prefix()
      return Object.keys(state.errors.fields)
        .map((path) => relativePath(base, path))
        .filter((path): path is string => path !== null && path !== "")
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
      const fields = toAbsolute(prefix(), validate?.(values))
      if (Object.keys(fields).length > 0) {
        set({ errors: { form: null, fields, extra: [] } })
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
  return api
}

export type DraftStore<T> = ReturnType<typeof createDraftStore<T>>
