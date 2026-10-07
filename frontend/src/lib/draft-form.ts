/**
 * useDraftForm: a form is a DRAFT of a config object. The daemon validates the
 * whole config on save and answers with errors addressed by config path; the
 * form maps those to form paths and shows them. There are no client
 * validators (one optional `validate` on submit, see below).
 *
 * Reference implementation: src/pages/outbound-upsert-page.tsx.
 * Core (paths, errors, store) and its tests: draft-form-core.ts,
 * tests/draft-form.test.ts.
 *
 * ## API in one screen
 *
 *   const form = useDraftForm<Draft>(initialDraft, { validate? })
 *   form.values                    current draft (read-only)
 *   form.field("a.b[0].c")         { name, value, onChange, error }  (typed path)
 *   form.setValue("a.b[0].c", v)   typed set; clears errors at/below that path
 *   form.setValues(prev => next)   whole-draft update; clears errors whose value changed
 *   form.errorFor("a.b[0].c")      string | null
 *   form.errors                    { form: string|null, fields, unmapped[] }
 *   form.setApiError(err, resolvePath)   API failure -> field/form/unmapped errors
 *   form.setServerErrors({ form?, fields?, unmapped? })
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
 * ## Migrating a TanStack form page, step by step
 *
 * 1. Imports. Drop `@tanstack/react-form`, `@tanstack/react-store` and
 *    `@/lib/form-api-errors`; add
 *      import { bindInput, type FieldBinding, type Path, useDraftForm } from "@/lib/draft-form"
 *
 * 2. Create the form. The default values become the first argument and its
 *    type the generic (use the existing draft type). All validators, `listeners`
 *    and `validationLogic` go away.
 *      before: const form = useForm({ listeners: clearServerErrorsOnChange,
 *                defaultValues: draft, validationLogic: revalidateLogic(...),
 *                validators: { onSubmitAsync: async ({ value }) => {...} } })
 *      after:  const form = useDraftForm<OutboundDraft>(draft, { validate })
 *      `const { values } = form` replaces `useStore(form.store, s => s.values)`.
 *
 * 3. Submit. The body of `onSubmitAsync` becomes a plain async function taking
 *    the values; wire it with `form.onSubmit`. Drop every `clearFormServerErrors`
 *    (submit clears errors itself) and every `return undefined`.
 *      before: <form onSubmit={(e) => { e.preventDefault(); void form.handleSubmit() }}>
 *      after:  <form onSubmit={form.onSubmit(save)}>
 *      Failure handling inside `save`: catch the API error, then
 *      before: const result = splitFormApiErrors({ error, resolvePath })
 *              setFormServerErrors(form, { form: result.formError ?? undefined,
 *                fields: result.fieldErrors, unmapped: result.unmappedErrors })
 *      after:  form.setApiError(error as ApiError, (path) => resolveXxxFieldPath(path, ...))
 *      Your own message instead of an API call:
 *      before: setFormServerErrors(form, { form: msg, fields: {} })
 *      after:  form.setServerErrors({ form: msg })
 *      On success just navigate/invalidate; do not clear errors by hand.
 *      Do NOT call `form.reset(...)` before `navigate`, it is not needed.
 *
 * 4. Client-side checks. If the old form had `validators.onChange`/`onSubmit`
 *    that the daemon cannot know about (name uniqueness among loaded items),
 *    move them into one `validate(values) => ({ [path]: message })`: it runs on
 *    submit only, blocks the handler, and its messages show via `errorFor`
 *    like server errors (and clear when the field is edited). Delete the rest.
 *      after:  useDraftForm<D>(draft, { validate: (v) => {
 *                const e = getOutboundTagError(v.tag, ...)
 *                return e ? { tag: e } : {}
 *              } })
 *
 * 5. Fields. Replace each `<form.Field name="x">{(field) => ...}</form.Field>`
 *    render prop with `form.field("x")`; no wrapper, no hooks in callbacks.
 *    Components that took a TanStack field take `field: FieldBinding<string>`:
 *      before: <form.Field name="gateway">
 *                {(field) => <TextField field={field} label="..." />}
 *              </form.Field>
 *      after:  <TextField field={form.field("gateway")} label="..." />
 *    Inside such a component:
 *      before: <Input name={field.name} value={field.state.value}
 *                onBlur={field.handleBlur}
 *                onChange={(e) => field.handleChange(e.target.value)} />
 *              and `const error = getFieldError(field)`
 *      after:  <Input {...bindInput(field)} />  and  `field.error`
 *    For non-input controls call `field.value` / `field.onChange(v)` or use
 *    `form.values.x` + `form.setValue("x", v)` directly (selects, radios,
 *    pickers, switches). Errors: `form.errorFor("x")` replaces
 *    `getFieldError(field)`, and `useFormServerErrors(form)` /
 *    `serverErrors.fields[...]` become `form.errors.fields[...]`.
 *      before: <InterfacePicker onChange={field.handleChange} value={field.state.value} />
 *      after:  <InterfacePicker onChange={(v) => form.setValue("interfaceName", v)}
 *                value={values.interfaceName} />
 *    Array items use indexed paths built in template strings, no sub-forms:
 *      form.field(`outboundGroups[${i}].members[${j}].weight`)
 *    Whenever the old code read `form.getFieldValue(x)`, read `form.values`
 *    (or `previous` inside `setValues`); `form.setFieldValue(x, v)` is
 *    `form.setValue(x, v)`.
 *
 * 6. Changing several fields at once: ONE `setValues` call with an updater
 *    (not several setValue calls on stale `values`):
 *      form.setValues((prev) => ({ ...prev, ...getKillSwitchFields(choice) }))
 *    Conditional "swap defaults only if untouched" logic reads `prev` and
 *    returns the new object (see `changeType` in the pilot).
 *
 * 7. Arrays (add/remove/reorder rows): build the whole new array from
 *    `form.values` and call `form.setValue("rows", nextRows)`. That clears the
 *    errors of every row below `rows`, so errors never stick to the wrong row
 *    after rows shift. (Plain field edits keep other rows' errors.)
 *
 * 8. Dirty / submit button.
 *      before: <form.Subscribe selector={(s) => ({ canSubmit: s.canSubmit, isPristine: s.isPristine })}>
 *                {({ canSubmit, isPristine }) => <Button disabled={mutation.isPending || isPristine || !canSubmit} />}
 *              </form.Subscribe>
 *      after:  <Button disabled={form.isSubmitting || !form.isDirty} type="submit" />
 *    Pages that are never "pristine-disabled" simply omit `!form.isDirty`.
 *
 * 9. Server error display: `form.errors.form` for the alert message,
 *    `form.errors.unmapped` for `<ServerValidationAlert errors={...} />`, and
 *    `form.errorFor(path)` for fields. A section that must open on an error
 *    can check `names.some((n) => form.errorFor(n))`.
 *
 * Gotchas
 * - `resolvePath` must return FORM paths (the draft's keys, e.g. `interfaceName`,
 *   `outboundGroups[0].members[1].weight`), or undefined for "unmapped".
 *   Both `a[0].b` and `a.0.b` work.
 * - The form key/remount pattern still applies: pass a stable `initial` and
 *   remount with `key` when switching entity. A changed `initial` is adopted
 *   by itself only while the draft has no unsaved edits.
 * - Do not mutate `form.values`; always go through setValue/setValues.
 * - The draft type must be a plain object of strings/arrays/objects so paths
 *   type-check (drafts in this repo already are).
 * - `bun run typecheck` does not check the app (root tsconfig has no files);
 *   use `bunx tsc -b`.
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
import {
  type ApiPathResolver,
  apiErrorsToServerErrors,
  createDraftStore,
  deepEqual,
  type FormErrors,
  getIn,
  type Path,
  type PathValue,
  type ServerErrors,
} from "@/lib/draft-form-core"

export {
  type ApiPathResolver,
  deepEqual,
  type FormErrors,
  type Path,
  type PathValue,
  type ServerErrors,
  splitFormApiErrors,
} from "@/lib/draft-form-core"

/** What a field control needs: bind it to an input, select, picker, ... */
export type FieldBinding<V> = {
  /** The path, usable as the input `name`. */
  name: string
  value: V
  onChange: (value: V) => void
  /** Server (or `validate`) error addressed to exactly this path. */
  error: string | null
}

type ClientErrors = Record<string, string | null | undefined>

export type UseDraftFormOptions<T> = {
  /**
   * OPTIONAL synchronous client check, run on submit only. Return
   * `{ [path]: message }`; any entry stops the submit and is shown through
   * `errorFor` like a server error. Do NOT use it for rules the daemon already
   * enforces; use it only for what the server cannot know (e.g. a duplicate
   * name among already-loaded items).
   */
  validate?: (values: T) => ClientErrors
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
  /** Binding for one path: `{ name, value, onChange, error }`. */
  field: <P extends Path<T>>(path: P) => FieldBinding<PathValue<T, P>>
  /** The error addressed to exactly this path, or null. */
  errorFor: (path: Path<T>) => string | null
  /** All current errors: `errors.form`, `errors.fields`, `errors.unmapped`. */
  errors: FormErrors
  /** Store API validation errors (usually via `setApiError`). Replaces old ones. */
  setServerErrors: (errors: ServerErrors) => void
  /**
   * Map an API failure to field errors with `resolvePath` (API path -> form
   * path, undefined = unmapped) and show it: field errors, `errors.form` for a
   * plain message, `errors.unmapped` for the rest.
   */
  setApiError: (error: ApiError, resolvePath: ApiPathResolver) => void
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

export function useDraftForm<T extends object>(
  initial: T,
  options: UseDraftFormOptions<T> = {}
): DraftForm<T> {
  const [store] = useState(() => createDraftStore<T>(initial))
  const state = useSyncExternalStore(
    store.subscribe,
    store.getState,
    store.getState
  )

  // Latest options/initial are read by the store from its event handlers.
  useEffect(() => {
    store.setValidate(options.validate)
  })
  useEffect(() => {
    store.syncInitial(initial)
  })

  const actions = useMemo(
    () => ({
      setValue: store.setValue as DraftForm<T>["setValue"],
      setValues: store.setValues,
      errorFor: store.errorFor as DraftForm<T>["errorFor"],
      setServerErrors: store.setServerErrors,
      setApiError: (error: ApiError, resolvePath: ApiPathResolver) =>
        store.setServerErrors(apiErrorsToServerErrors(error, resolvePath)),
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
    errors: state.errors,
    isDirty,
    isSubmitting: state.isSubmitting,
    field: (path) =>
      ({
        name: path,
        value: getIn(state.values, path),
        onChange: (value: unknown) => store.setValue(path, value),
        error: store.errorFor(path),
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
