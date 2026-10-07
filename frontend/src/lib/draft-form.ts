/**
 * useDraftForm: a form is a DRAFT of (a part of) the config document. The
 * daemon validates the whole config on save and reports errors by the EXACT
 * JSON path of the offending value (`outbounds[3].retry.attempts`,
 * `dns.servers[1].detour`, `lists["my.list"].url`). There are no client
 * validators (one optional `validate` on submit) and no path-mapping
 * functions: every control owns the path of its value.
 *
 * Reference: src/pages/outbound-upsert-page.tsx (nested draft + array index
 * prefix), src/pages/general-config-page.tsx (whole config, no prefix).
 * Core and tests: draft-form-core.ts, tests/draft-form.test.ts.
 *
 * ## The two rules
 *
 * 1. NAME THE DRAFT LIKE THE API. A draft key/nesting equals the API field,
 *    relative to the edited object: `interface`, `interval_ms`,
 *    `retry.attempts`, `outbound_groups[0].members[1].weight`. Values may be
 *    strings for text inputs; only names and structure mirror the API (UI-only
 *    fields such as `password_confirmation` are fine).
 * 2. TELL THE HOOK WHERE THE DRAFT LIVES with `apiPrefix`: the path of the
 *    edited object inside the sent config document, e.g.
 *    `outbounds[${index}]` where index is the position in the array that is
 *    SENT (existing item: its index; new item: array length), or a function of
 *    the values (`lists` keyed by name: `formatPath(["lists", values.name])`).
 *    Omit it when the draft is the whole config.
 *
 * `form.field("retry.attempts")` / `form.errorFor("retry.attempts")` then show
 * the server error for `<apiPrefix>.retry.attempts`.
 *
 * ## API in one screen
 *
 *   const form = useDraftForm<Draft>(initialDraft, { validate?, apiPrefix? })
 *   form.values                    current draft (read-only)
 *   form.field("a.b[0].c", opts?)  { name, value, onChange, error }  (typed path)
 *   form.errorFor("a.b[0].c", opts?)   string | null
 *   form.setValue("a.b[0].c", v)   typed set; clears errors at/below that path
 *   form.setValues(prev => next)   whole-draft update; clears errors whose value changed
 *   form.setApiError(err)          failed save -> errors by exact path; returns the form message or null
 *   form.formError                 message for the whole form
 *   form.unmappedErrors()          errors no control showed, with full path
 *   form.errorPaths()              draft paths that have an error (for "show weights" style logic)
 *   form.setServerErrors({ form?, fields? })   manual errors by DRAFT path
 *   form.clearServerErrors()
 *   form.isDirty / form.reset(next?) / form.isSubmitting
 *   form.onSubmit(async (values) => {...})   <form onSubmit> handler
 *   form.submit(async (values) => {...})     same, as a promise
 *   bindInput(field)               props for <Input>: name/value/onChange/aria-invalid
 *
 * Errors are plain state: they never block submitting. Submitting clears them
 * first; editing a path clears its error and everything below it (`a` clears
 * `a[0].b`, never `aExtra`).
 *
 * ## Where an error is shown (claims and ancestor fallback)
 *
 * Calling `form.field(path)` or `form.errorFor(path)` while rendering CLAIMS
 * that path. A server error goes to the control that claims its exact path;
 * if nobody does, to the nearest claimed ANCESTOR (an error on
 * `outbound_groups[0].members` shows at the tier that claimed
 * `outbound_groups[0]`); if there is none it is listed by
 * `form.unmappedErrors()` with its full path. Rules for this to work:
 * - call `field`/`errorFor` in the SAME component that calls `useDraftForm`
 *   (pass the bindings down as props); claims are collected per render and
 *   adopted after the commit (one extra render when the set of controls
 *   changed while errors are showing);
 * - controls that are not rendered (hidden by a condition) claim nothing, so
 *   their errors fall back to an ancestor or the unmapped list;
 * - exceptions are explicit: a control that owns more than one API path passes
 *   `{ alsoClaims: [...] }` (kill switch: `strict_enforcement` +
 *   `strict_enforcement_action`); `""` is the draft root, i.e. errors
 *   addressed to the whole edited object (give it to the tag/name control:
 *   `form.field("tag", { alsoClaims: [""] })`). Keep these rare.
 *
 * ## Migrating a form step by step
 *
 * 1. Rename the draft to the API names (types, `*-utils.ts` mapping/payload,
 *    tests). Delete every `resolve*FieldPath` and `*_FIELD_NAMES`.
 * 2. `const form = useDraftForm<Draft>(draft, { apiPrefix, validate? })`;
 *    `const { values } = form`.
 * 3. Submit: `<form onSubmit={form.onSubmit(save)}>`; `save` does the POST and
 *    on failure `const message = form.setApiError(error as ApiError)` (toast
 *    the message if the old code did). Success: navigate / `form.reset(next)`.
 * 4. Fields: `<TextField field={form.field("retry.attempts")} />` with
 *    `<Input {...bindInput(field)} />` inside; other controls use
 *    `field.value` / `field.onChange(v)` or `form.values` + `form.setValue`.
 *    Errors: `form.errorFor(path)`.
 * 5. Several fields at once: one `form.setValues(prev => ...)`. Array
 *    add/remove/reorder: `form.setValue("rows", nextRows)` (clears the errors
 *    of all rows).
 * 6. Client-only checks (name uniqueness among loaded items) go into
 *    `validate: (values) => ({ [draftPath]: message })`; it runs on submit only.
 * 7. Save button: `disabled={form.isSubmitting || !form.isDirty}`;
 *    alert for `form.formError`; `<ServerValidationAlert errors={form.unmappedErrors()} />`.
 *
 * Gotchas
 * - Keep `apiPrefix` equal to what is really sent: if the payload drops or
 *   reorders array items, indices in the error paths will not match the draft.
 * - A changed `initial` is adopted automatically only while the draft has no
 *   unsaved edits; remount with `key` when switching entity.
 * - Do not mutate `form.values`; always go through setValue/setValues.
 * - The draft type must be a plain object of strings/booleans/arrays/objects
 *   so paths type-check.
 */

import {
  type ChangeEvent,
  type FormEvent,
  useEffect,
  useMemo,
  useState,
  useSyncExternalStore,
} from "react"

import type { ApiError } from "@/api/client"
import type { ValidationErrorEntry } from "@/lib/api-errors"
import {
  type ClaimOptions,
  createDraftStore,
  deepEqual,
  getIn,
  type Path,
  type PathValue,
  type ServerErrors,
} from "@/lib/draft-form-core"

export {
  deepEqual,
  formatPath,
  getIn,
  type Path,
  type PathValue,
  type ServerErrors,
  setIn,
} from "@/lib/draft-form-core"

/** What a field control needs: bind it to an input, select, picker, ... */
export type FieldBinding<V> = {
  /** The path, usable as the input `name`. */
  name: string
  value: V
  onChange: (value: V) => void
  /** Server (or `validate`) error shown at exactly this control. */
  error: string | null
}

type ClientErrors = Record<string, string | null | undefined>

export type UseDraftFormOptions<T> = {
  /**
   * OPTIONAL synchronous client check, run on submit only. Return
   * `{ [draftPath]: message }`; any entry stops the submit and is shown through
   * `errorFor` like a server error. Do NOT use it for rules the daemon already
   * enforces; use it only for what the server cannot know (e.g. a duplicate
   * name among already-loaded items).
   */
  validate?: (values: T) => ClientErrors
  /**
   * Where the draft lives inside the config document the API validates, e.g.
   * `"outbounds[3]"` (the index the item has in the array that is SENT; for a
   * new item the index it will get) or a function of the draft. Draft paths
   * mirror the API, so `errorFor("interface")` shows the server error for
   * `outbounds[3].interface`. Omit it when the draft is the whole config.
   */
  apiPrefix?: string | ((values: T) => string)
}

export type DraftForm<T> = {
  /** The current draft. Never mutate it; use `setValue` / `setValues`. */
  values: T
  /** Set one value by typed path. Clears that path's error and those below it. */
  setValue: <P extends Path<T>>(path: P, value: PathValue<T, P>) => void
  /**
   * Replace the whole draft, from a new object or from the previous one.
   * Errors of every path whose value changed are cleared. For array
   * add/remove/reorder prefer `setValue("rows", nextRows)`, which clears all
   * errors below `rows`.
   */
  setValues: (next: T | ((previous: T) => T)) => void
  /**
   * Binding for one path: `{ name, value, onChange, error }`. Rendering it
   * claims the path: server errors for it (or, failing a more specific
   * control, for paths below it) show up as `error`. Pass `alsoClaims` when
   * one control owns several API paths.
   */
  field: <P extends Path<T>>(
    path: P,
    options?: DraftClaimOptions<T>
  ) => FieldBinding<PathValue<T, P>>
  /** `field(path).error` without a binding (also claims the path). */
  errorFor: (path: Path<T>, options?: DraftClaimOptions<T>) => string | null
  /** Draft paths (relative to `apiPrefix`) that currently have an error. */
  errorPaths: () => string[]
  /**
   * Errors that no rendered control claimed (by exact or ancestor path),
   * with their full API path:
   * `<ServerValidationAlert errors={form.unmappedErrors()} />`.
   */
  unmappedErrors: () => ValidationErrorEntry[]
  /** Message for the whole form (non-validation failures, manual errors). */
  formError: string | null
  /**
   * Store validation errors from a failed save (exact API paths). Returns the
   * form message when the failure had no validation details (e.g. for a toast).
   */
  setApiError: (error: ApiError) => string | null
  /** Manual errors by DRAFT path, plus an optional form message. */
  setServerErrors: (errors: ServerErrors) => void
  clearServerErrors: () => void
  /** Differs from the initial value (deep compare). */
  isDirty: boolean
  /** Reset the draft to `next` (which becomes the new "clean" value) or the current clean value. */
  reset: (next?: T) => void
  isSubmitting: boolean
  /**
   * Clears errors, runs `validate`, then `handler(values)` with `isSubmitting`
   * set. Returns a promise. Errors never block it.
   */
  submit: (handler: (values: T) => void | Promise<void>) => Promise<void>
  /** `submit` as a `<form onSubmit>` handler (calls preventDefault). */
  onSubmit: (
    handler: (values: T) => void | Promise<void>
  ) => (event?: FormEvent) => void
}

/**
 * `alsoClaims` lists further draft paths the control shows errors for; `""`
 * is the draft root, i.e. errors addressed to the whole edited object.
 */
export type DraftClaimOptions<T> = {
  alsoClaims?: ReadonlyArray<Path<T> | "">
} & ClaimOptions

export function useDraftForm<T extends object>(
  initial: T,
  options: UseDraftFormOptions<T> = {}
): DraftForm<T> {
  const { apiPrefix } = options
  const [store] = useState(() =>
    createDraftStore<T>(initial, (values) =>
      typeof apiPrefix === "function" ? apiPrefix(values) : (apiPrefix ?? "")
    )
  )
  const state = useSyncExternalStore(
    store.subscribe,
    store.getState,
    store.getState
  )

  // Controls rendered below register their paths while this component renders.
  store.beginRender()

  // Latest options/initial are read by the store from its event handlers.
  useEffect(() => {
    store.setValidate(options.validate)
    store.setPrefix((values) =>
      typeof apiPrefix === "function" ? apiPrefix(values) : (apiPrefix ?? "")
    )
  })
  useEffect(() => {
    store.syncInitial(initial)
  })
  useEffect(() => {
    store.commitClaims()
  })

  const actions = useMemo(
    () => ({
      setValue: store.setValue as DraftForm<T>["setValue"],
      setValues: store.setValues,
      errorFor: store.errorFor as DraftForm<T>["errorFor"],
      errorPaths: store.errorPaths,
      unmappedErrors: store.unmapped,
      setServerErrors: store.setServerErrors,
      setApiError: store.setApiError,
      clearServerErrors: store.clearServerErrors,
      reset: store.reset,
      submit: store.submit,
      onSubmit: (handler: (values: T) => void | Promise<void>) => {
        return (event?: FormEvent) => {
          event?.preventDefault()
          void store.submit(handler)
        }
      },
    }),
    [store]
  )

  const isDirty = useMemo(
    () => !deepEqual(state.values, state.baseline),
    [state.values, state.baseline]
  )

  return {
    ...actions,
    values: state.values,
    formError: state.errors.form,
    isDirty,
    isSubmitting: state.isSubmitting,
    field: (path, claimOptions) =>
      ({
        name: path,
        value: getIn(state.values, path),
        onChange: (value: unknown) => store.setValue(path, value),
        error: store.errorFor(path, claimOptions),
      }) as unknown as FieldBinding<never>,
  } as DraftForm<T>
}

/**
 * Props for a text `<Input>` (or any element with `name`, `value`, `onChange`
 * taking an event, and `aria-invalid`) from a string field binding:
 * `<Input {...bindInput(form.field("tag"))} />`.
 */
export function bindInput(field: FieldBinding<string>) {
  return {
    name: field.name,
    value: field.value ?? "",
    onChange: (event: ChangeEvent<HTMLInputElement>) =>
      field.onChange(event.target.value),
    "aria-invalid": Boolean(field.error),
  }
}
