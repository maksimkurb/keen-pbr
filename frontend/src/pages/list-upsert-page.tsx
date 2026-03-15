import { useForm } from "@tanstack/react-form"
import { useQueryClient } from "@tanstack/react-query"
import { useState } from "react"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { ListConfig } from "@/api/generated/model/listConfig"
import { usePostConfigMutation } from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
import { useGetConfig } from "@/api/queries"
import { selectConfig } from "@/api/selectors"
import {
  Field,
  FieldContent,
  FieldGroup,
  FieldHint,
  FieldLabel,
} from "@/components/shared/field"
import { UpsertPage } from "@/components/shared/upsert-page"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Textarea } from "@/components/ui/textarea"
import {
  applyFormApiErrors,
  clearFormServerErrors,
} from "@/lib/form-api-errors"

type ListDraft = {
  name: string
  ttlMs: string
  domains: string
  ipCidrs: string
  url: string
  file: string
}

const sampleNewList: ListDraft = {
  name: "",
  ttlMs: "300000",
  domains: "",
  ipCidrs: "",
  url: "",
  file: "",
}

export function ListUpsertPage({
  mode,
  listId,
}: {
  mode: "create" | "edit"
  listId?: string
}) {
  const [, navigate] = useLocation()
  const queryClient = useQueryClient()
  const configQuery = useGetConfig()
  const loadedConfig = selectConfig(configQuery.data)
  const listsMap = loadedConfig?.lists ?? {}

  const [saveSuccessMessage, setSaveSuccessMessage] = useState<string | null>(
    null
  )
  const [mutationErrorMessage, setMutationErrorMessage] = useState<
    string | null
  >(null)
  const [apiError, setApiError] = useState<ApiError | null>(null)

  const draft =
    mode === "edit"
      ? getDraftFromMapEntry(listId, listId ? listsMap[listId] : undefined)
      : sampleNewList

  const postConfigMutation = usePostConfigMutation({
    mutation: {
      onSuccess: async () => {
        setSaveSuccessMessage(
          mode === "create"
            ? "List staged. Apply config to persist it."
            : "List changes staged. Apply config to persist them."
        )
        setMutationErrorMessage(null)
        setApiError(null)

        await Promise.all([
          queryClient.invalidateQueries({ queryKey: queryKeys.config() }),
          queryClient.invalidateQueries({ queryKey: queryKeys.dnsTest() }),
        ])

        navigate("/lists")
      },
      onError: (error) => {
        setSaveSuccessMessage(null)
        setApiError(error as ApiError)
      },
    },
  })

  if (mode === "edit" && !draft) {
    return (
      <UpsertPage
        cardDescription="The requested list could not be found."
        cardTitle="Missing list"
        description="Return to the lists table and choose a valid entry."
        title="Edit list"
      >
        <div className="flex justify-end">
          <Button onClick={() => navigate("/lists")} variant="outline">
            Back to lists
          </Button>
        </div>
      </UpsertPage>
    )
  }

  return (
    <UpsertPage
      cardDescription="Review the list source, TTL, and matching entries before saving."
      cardTitle={
        mode === "create" ? "Create list" : `Edit ${draft?.name ?? "list"}`
      }
      description="Lists can be backed by files, builtin sources, or remote URLs."
      title={mode === "create" ? "Create list" : "Edit list"}
    >
      {saveSuccessMessage ? (
        <Alert className="border-success/30 bg-success/5 text-success">
          <AlertDescription>{saveSuccessMessage}</AlertDescription>
        </Alert>
      ) : null}

      {mutationErrorMessage ? (
        <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription className="whitespace-pre-wrap">
            {mutationErrorMessage}
          </AlertDescription>
        </Alert>
      ) : null}

      <ListForm
        draft={draft ?? sampleNewList}
        existingListNames={Object.keys(listsMap)}
        isConfigLoaded={Boolean(loadedConfig)}
        isPending={postConfigMutation.isPending}
        mode={mode}
        apiError={apiError}
        onCancel={() => navigate("/lists")}
        onErrorMessageChange={setMutationErrorMessage}
        onSubmit={(nextDraft) => {
          if (!loadedConfig) {
            return
          }

          const updatedConfig = buildUpdatedConfigForListUpsert(
            loadedConfig,
            mode,
            nextDraft,
            listId
          )

          setSaveSuccessMessage(null)
          setMutationErrorMessage(null)
          setApiError(null)
          postConfigMutation.mutate({ data: updatedConfig })
        }}
      />
    </UpsertPage>
  )
}

function ListForm({
  mode,
  draft,
  existingListNames,
  isConfigLoaded,
  isPending,
  onCancel,
  apiError,
  onErrorMessageChange,
  onSubmit,
}: {
  mode: "create" | "edit"
  draft: ListDraft
  existingListNames: string[]
  isConfigLoaded: boolean
  isPending: boolean
  onCancel: () => void
  apiError: ApiError | null
  onErrorMessageChange: (message: string | null) => void
  onSubmit: (draft: ListDraft) => void
}) {
  const form = useForm({
    defaultValues: draft,
    onSubmit: ({ value }) => {
      clearFormServerErrors(form)
      onErrorMessageChange(null)
      onSubmit(value)
    },
  })

  useEffect(() => {
    form.reset(draft)
    clearFormServerErrors(form)
    onErrorMessageChange(null)
  }, [draft, form])

  useEffect(() => {
    onErrorMessageChange(
      applyFormApiErrors({
        error: apiError,
        form,
        resolvePath: (path) =>
          resolveListFieldPath(path, form.state.values.name || draft.name),
      }) ?? null
    )
  }, [apiError, draft.name, form, onErrorMessageChange])

  const isCreate = mode === "create"

  return (
    <form
      className="space-y-6"
      onSubmit={(event) => {
        event.preventDefault()
        form.handleSubmit()
      }}
    >
      <FieldGroup>
        <form.Field
          name="name"
          validators={{
            onMount: ({ value }) =>
              getListNameError(
                value,
                existingListNames,
                isCreate ? undefined : draft.name
              ) ?? undefined,
            onChange: ({ value }) =>
              getListNameError(
                value,
                existingListNames,
                isCreate ? undefined : draft.name
              ) ?? undefined,
          }}
        >
          {(field) => {
            const error = getFirstFieldError(field.state.meta.errors)

            return (
              <Field invalid={Boolean(error)}>
                <FieldLabel htmlFor="list-name">Name</FieldLabel>
                <FieldContent>
                  <Input
                    aria-invalid={Boolean(error)}
                    id="list-name"
                    onBlur={field.handleBlur}
                    onChange={(event) => field.handleChange(event.target.value)}
                    value={field.state.value}
                  />
                  <FieldHint
                    description="Use a stable identifier so rules and references remain easy to follow."
                    error={error ?? null}
                  />
                </FieldContent>
              </Field>
            )
          }}
        </form.Field>

        <form.Field
          name="ttlMs"
          validators={{
            onMount: ({ value }) => getTtlError(value) ?? undefined,
            onChange: ({ value }) => getTtlError(value) ?? undefined,
          }}
        >
          {(field) => {
            const error = getFirstFieldError(field.state.meta.errors)

            return (
              <Field invalid={Boolean(error)}>
                <FieldLabel htmlFor="list-ttl-ms">TTL ms</FieldLabel>
                <FieldContent>
                  <Input
                    aria-invalid={Boolean(error)}
                    id="list-ttl-ms"
                    onBlur={field.handleBlur}
                    onChange={(event) => field.handleChange(event.target.value)}
                    value={field.state.value}
                  />
                  <FieldHint
                    description="How long resolved IPs from domains in this list stay in the IP set; 0 means no timeout."
                    error={error ?? null}
                  />
                </FieldContent>
              </Field>
            )
          }}
        </form.Field>

        <form.Field name="url">
          {(field) => (
            <Field>
              <FieldLabel htmlFor="list-url">Remote URL</FieldLabel>
              <FieldContent>
                <Input
                  id="list-url"
                  onBlur={field.handleBlur}
                  onChange={(event) => field.handleChange(event.target.value)}
                  value={field.state.value}
                />
                <FieldHint description="Optional remote source loaded over HTTP or HTTPS and merged into the list." />
              </FieldContent>
            </Field>
          )}
        </form.Field>

        <form.Field name="file">
          {(field) => (
            <Field>
              <FieldLabel htmlFor="list-file">Local file</FieldLabel>
              <FieldContent>
                <Input
                  id="list-file"
                  onBlur={field.handleBlur}
                  onChange={(event) => field.handleChange(event.target.value)}
                  value={field.state.value}
                />
                <FieldHint description="Optional local file path. File entries are merged with any inline domains, IPs, and remote URL data." />
              </FieldContent>
            </Field>
          )}
        </form.Field>

        <form.Field
          name="domains"
          validators={{
            onMount: ({ value, fieldApi }) =>
              getListContentError({
                url: fieldApi.form.getFieldValue("url"),
                file: fieldApi.form.getFieldValue("file"),
                domains: value,
                ipCidrs: fieldApi.form.getFieldValue("ipCidrs"),
              }) ?? undefined,
            onChangeListenTo: ["url", "file", "ipCidrs"],
            onChange: ({ value, fieldApi }) =>
              getListContentError({
                url: fieldApi.form.getFieldValue("url"),
                file: fieldApi.form.getFieldValue("file"),
                domains: value,
                ipCidrs: fieldApi.form.getFieldValue("ipCidrs"),
              }) ?? undefined,
          }}
        >
          {(field) => {
            const error = getFirstFieldError(field.state.meta.errors)

            return (
              <Field invalid={Boolean(error)}>
                <FieldLabel htmlFor="list-domains">Domains</FieldLabel>
                <FieldContent>
                  <Textarea
                    aria-invalid={Boolean(error)}
                    className="min-h-24"
                    id="list-domains"
                    onBlur={field.handleBlur}
                    onChange={(event) => field.handleChange(event.target.value)}
                    value={field.state.value}
                  />
                  <FieldHint
                    description="Inline domain patterns. Writing example.com automatically includes all subdomains."
                    error={error ?? null}
                  />
                </FieldContent>
              </Field>
            )
          }}
        </form.Field>

        <form.Field
          name="ipCidrs"
          validators={{
            onMount: ({ value, fieldApi }) =>
              getListContentError({
                url: fieldApi.form.getFieldValue("url"),
                file: fieldApi.form.getFieldValue("file"),
                domains: fieldApi.form.getFieldValue("domains"),
                ipCidrs: value,
              }) ?? undefined,
            onChangeListenTo: ["url", "file", "domains"],
            onChange: ({ value, fieldApi }) =>
              getListContentError({
                url: fieldApi.form.getFieldValue("url"),
                file: fieldApi.form.getFieldValue("file"),
                domains: fieldApi.form.getFieldValue("domains"),
                ipCidrs: value,
              }) ?? undefined,
          }}
        >
          {(field) => {
            const error = getFirstFieldError(field.state.meta.errors)

            return (
              <Field invalid={Boolean(error)}>
                <FieldLabel htmlFor="list-ip-cidrs">IP CIDRs</FieldLabel>
                <FieldContent>
                  <Textarea
                    aria-invalid={Boolean(error)}
                    className="min-h-24"
                    id="list-ip-cidrs"
                    onBlur={field.handleBlur}
                    onChange={(event) => field.handleChange(event.target.value)}
                    value={field.state.value}
                  />
                  <FieldHint
                    description="Inline IP addresses or CIDR ranges, for example 93.184.216.34, 10.0.0.0/8, or 2001:db8::/32."
                    error={error ?? null}
                  />
                </FieldContent>
              </Field>
            )
          }}
        </form.Field>
      </FieldGroup>
      <div className="flex justify-end gap-3">
        <Button onClick={onCancel} size="xl" type="button" variant="outline">
          Cancel
        </Button>
        <form.Subscribe
          selector={(state) => ({
            canSubmit: state.canSubmit,
          })}
        >
          {({ canSubmit }) => (
            <Button
              disabled={!isConfigLoaded || isPending || !canSubmit}
              size="xl"
              type="submit"
            >
              {isPending
                ? "Saving..."
                : mode === "create"
                  ? "Create list"
                  : "Save list"}
            </Button>
          )}
        </form.Subscribe>
      </div>
    </form>
  )
}

function getDraftFromMapEntry(
  name: string | undefined,
  listConfig?: ListConfig
): ListDraft | null {
  if (!name || !listConfig) {
    return null
  }

  return {
    name,
    ttlMs: String(listConfig.ttl_ms ?? 300000),
    domains: (listConfig.domains ?? []).join("\n"),
    ipCidrs: (listConfig.ip_cidrs ?? []).join("\n"),
    url: listConfig.url ?? "",
    file: listConfig.file ?? "",
  }
}

function buildUpdatedConfigForListUpsert(
  config: ConfigObject,
  mode: "create" | "edit",
  nextDraft: ListDraft,
  originalName?: string
): ConfigObject {
  const nextLists = { ...(config.lists ?? {}) }
  const trimmedName = nextDraft.name.trim()
  const trimmedOriginalName = originalName?.trim()
  const nextListConfig = getListConfigFromDraft(nextDraft)

  if (
    mode === "edit" &&
    trimmedOriginalName &&
    trimmedOriginalName !== trimmedName
  ) {
    delete nextLists[trimmedOriginalName]
    nextLists[trimmedName] = nextListConfig

    return {
      ...config,
      lists: nextLists,
      route: {
        ...config.route,
        rules: (config.route?.rules ?? []).map((rule) => ({
          ...rule,
          list: rule.list.map((name) =>
            name === trimmedOriginalName ? trimmedName : name
          ),
        })),
      },
      dns: {
        ...config.dns,
        rules: (config.dns?.rules ?? []).map((rule) => ({
          ...rule,
          list: rule.list.map((name) =>
            name === trimmedOriginalName ? trimmedName : name
          ),
        })),
      },
    }
  }

  nextLists[trimmedName] = nextListConfig

  return {
    ...config,
    lists: nextLists,
  }
}

function getListConfigFromDraft(draft: ListDraft): ListConfig {
  const domains = splitLines(draft.domains)
  const ipCidrs = splitLines(draft.ipCidrs)

  const listConfig: ListConfig = {
    ttl_ms: Number.parseInt(draft.ttlMs.trim(), 10),
  }

  if (draft.url.trim()) {
    listConfig.url = draft.url.trim()
  }

  if (draft.file.trim()) {
    listConfig.file = draft.file.trim()
  }

  if (domains.length > 0) {
    listConfig.domains = domains
  }

  if (ipCidrs.length > 0) {
    listConfig.ip_cidrs = ipCidrs
  }

  return listConfig
}

function splitLines(value: string) {
  return value
    .split("\n")
    .map((entry) => entry.trim())
    .filter(Boolean)
}

function getFirstFieldError(errors: unknown[]) {
  const firstError = errors[0]
  return typeof firstError === "string" ? firstError : null
}

function getListNameError(
  value: string,
  existingListNames: string[],
  currentName?: string
) {
  const trimmedName = value.trim()
  if (!trimmedName) {
    return "Name is required."
  }

  if (existingListNames.includes(trimmedName) && trimmedName !== currentName) {
    return "A list with this name already exists."
  }

  return null
}

function getTtlError(value: string) {
  const trimmed = value.trim()
  if (!/^\d+$/.test(trimmed)) {
    return "TTL must be a non-negative integer."
  }

  return null
}

function getListContentError({
  url,
  file,
  domains,
  ipCidrs,
}: {
  url: string
  file: string
  domains: string
  ipCidrs: string
}) {
  const hasUrl = url.trim().length > 0
  const hasFile = file.trim().length > 0
  const hasDomains = splitLines(domains).length > 0
  const hasIpCidrs = splitLines(ipCidrs).length > 0

  if (!hasUrl && !hasFile && !hasDomains && !hasIpCidrs) {
    return "At least one source is required: remote URL, local file, domains, or IP CIDRs."
  }

  return null
}

function resolveListFieldPath(path: string, name: string) {
  const normalizedName = name.trim()

  if (path === "lists") {
    return "name"
  }

  if (normalizedName && path === `lists.${normalizedName}`) {
    return "name"
  }

  if (normalizedName && path === `lists.${normalizedName}.ttl_ms`) {
    return "ttlMs"
  }

  if (normalizedName && path === `lists.${normalizedName}.domains`) {
    return "domains"
  }

  if (normalizedName && path === `lists.${normalizedName}.ip_cidrs`) {
    return "ipCidrs"
  }

  if (normalizedName && path === `lists.${normalizedName}.url`) {
    return "url"
  }

  if (normalizedName && path === `lists.${normalizedName}.file`) {
    return "file"
  }

  if (path.startsWith("lists.") && !path.includes(".", "lists.".length)) {
    return "name"
  }

  return undefined
}
