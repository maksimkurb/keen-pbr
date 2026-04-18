import { useForm } from "@tanstack/react-form"
import { useQueryClient } from "@tanstack/react-query"
import { useStore } from "@tanstack/react-store"
import { CloudIcon, FileTextIcon, ScrollTextIcon } from "lucide-react"
import { useEffect, useState } from "react"
import { useTranslation } from "react-i18next"
import { useLocation } from "wouter"
import { toast } from "sonner"

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
import { ButtonGroup } from "@/components/shared/button-group"
import { UpsertPage } from "@/components/shared/upsert-page"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card"
import { Input } from "@/components/ui/input"
import { Textarea } from "@/components/ui/textarea"
import { getApiValidationErrors } from "@/lib/api-errors"
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

type ListSourceGroup = "url" | "file" | "inline"

const LIST_SOURCE_GROUPS: ListSourceGroup[] = ["url", "file", "inline"]
const DEFAULT_SOURCE_GROUP: ListSourceGroup = "url"
const LIST_SOURCE_GROUP_ICONS = {
  url: CloudIcon,
  file: FileTextIcon,
  inline: ScrollTextIcon,
} satisfies Record<ListSourceGroup, typeof CloudIcon>
const LIST_SOURCE_GROUP_FIELDS = {
  url: ["url"],
  file: ["file"],
  inline: ["domains", "ipCidrs"],
} satisfies Record<ListSourceGroup, (keyof ListDraft)[]>

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
  const { t } = useTranslation()
  const [, navigate] = useLocation()
  const queryClient = useQueryClient()
  const configQuery = useGetConfig()
  const loadedConfig = selectConfig(configQuery.data)
  const listsMap = loadedConfig?.lists ?? {}

  // use toasts instead of inline messages
  const [apiError, setApiError] = useState<ApiError | null>(null)

  const draft =
    mode === "edit"
      ? getDraftFromMapEntry(listId, listId ? listsMap[listId] : undefined)
      : sampleNewList

  const postConfigMutation = usePostConfigMutation({
    mutation: {
      onSuccess: async () => {
        toast.success(
          mode === "create"
            ? t("pages.listUpsert.messages.created")
            : t("pages.listUpsert.messages.updated")
        )
        setApiError(null)

        await Promise.all([
          queryClient.invalidateQueries({ queryKey: queryKeys.config() }),
          queryClient.invalidateQueries({ queryKey: queryKeys.dnsTest() }),
        ])

        navigate("/lists")
      },
      onError: (error) => {
        const apiErr = error as ApiError
        setApiError(apiErr)
        const validationErrors = getApiValidationErrors(apiErr)
        if (validationErrors.length === 0) {
          const msg = apiErr?.message ?? t("pages.listUpsert.messages.saveError")
          toast.error(msg, { richColors: true })
        }
      },
    },
  })

  if (mode === "edit" && !draft) {
    return (
      <UpsertPage
        cardDescription={t("pages.listUpsert.missing.cardDescription")}
        cardTitle={t("pages.listUpsert.missing.cardTitle")}
        description={t("pages.listUpsert.missing.description")}
        title={t("pages.listUpsert.editTitle")}
      >
        <div className="flex justify-end">
          <Button onClick={() => navigate("/lists")} variant="outline">
            {t("pages.listUpsert.missing.back")}
          </Button>
        </div>
      </UpsertPage>
    )
  }

  return (
    <UpsertPage
      cardDescription={t("pages.listUpsert.cardDescription")}
      cardTitle={
        mode === "create"
          ? t("pages.listUpsert.createTitle")
          : t("pages.listUpsert.editCardTitle", {
              name: draft?.name ?? t("pages.listUpsert.fallbackName"),
            })
      }
      description={t("pages.listUpsert.description")}
      title={
        mode === "create"
          ? t("pages.listUpsert.createTitle")
          : t("pages.listUpsert.editTitle")
      }
    >
      <ListForm
        draft={draft ?? sampleNewList}
        existingListNames={Object.keys(listsMap)}
        isConfigLoaded={Boolean(loadedConfig)}
        isPending={postConfigMutation.isPending}
        mode={mode}
        apiError={apiError}
        onCancel={() => navigate("/lists")}
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
  onSubmit,
}: {
  mode: "create" | "edit"
  draft: ListDraft
  existingListNames: string[]
  isConfigLoaded: boolean
  isPending: boolean
  onCancel: () => void
  apiError: ApiError | null
  onSubmit: (draft: ListDraft) => void
}) {
  const { t } = useTranslation()
  const [activeSourceGroups, setActiveSourceGroups] = useState<ListSourceGroup[]>(
    () => getActiveSourceGroupsFromDraft(draft)
  )
  const form = useForm({
    defaultValues: draft,
    onSubmit: ({ value }) => {
      clearFormServerErrors(form)
      onSubmit(value)
    },
  })

  const apiErrorMessage = useStore(
    form.store,
    (state) =>
      ((state.errorMap.onServer as { form?: string } | undefined)?.form ?? null)
  )

  useEffect(() => {
    applyFormApiErrors({
      error: apiError,
      form,
      resolvePath: (path) =>
        resolveListFieldPath(path, form.state.values.name || draft.name),
    })
  }, [apiError, draft.name, form])

  const isCreate = mode === "create"

  const handleSourceGroupSelect = (group: ListSourceGroup) => {
    const currentValues = form.state.values
    const filledActiveGroups = activeSourceGroups.filter((sourceGroup) =>
      isSourceGroupPopulated(sourceGroup, currentValues)
    )
    const groupsToClear = filledActiveGroups.filter(
      (sourceGroup) => sourceGroup !== group
    )

    if (
      groupsToClear.length === 0 &&
      activeSourceGroups.length === 1 &&
      activeSourceGroups[0] === group
    ) {
      return
    }

    if (
      groupsToClear.length > 0 &&
      !window.confirm(t("pages.listUpsert.sourceSwitcher.confirmChange"))
    ) {
      return
    }

    setActiveSourceGroups([group])
    clearFormServerErrors(form)

    for (const sourceGroup of LIST_SOURCE_GROUPS) {
      if (sourceGroup === group) {
        continue
      }

      for (const fieldName of LIST_SOURCE_GROUP_FIELDS[sourceGroup]) {
        form.setFieldValue(fieldName, "")
      }
    }

    if (group !== "inline") {
      form.setFieldValue("domains", "")
      form.setFieldValue("ipCidrs", "")
    }

  }

  return (
    <form
      className="space-y-6"
      onSubmit={(event) => {
        event.preventDefault()
        form.handleSubmit()
      }}
    >
      <Card>
        <CardHeader>
          <CardTitle>{t("pages.listUpsert.common.title")}</CardTitle>
          <CardDescription>
            {t("pages.listUpsert.common.description")}
          </CardDescription>
        </CardHeader>
        <CardContent>
          <FieldGroup>
            <form.Field
              name="name"
              validators={{
                onMount: ({ value }) =>
                  getListNameError(
                    value,
                    existingListNames,
                    isCreate ? undefined : draft.name,
                    t
                  ) ?? undefined,
                onChange: ({ value }) =>
                  getListNameError(
                    value,
                    existingListNames,
                    isCreate ? undefined : draft.name,
                    t
                  ) ?? undefined,
              }}
            >
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)

                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor="list-name">
                      {t("pages.listUpsert.fields.name")}
                    </FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        disabled={!isCreate}
                        id="list-name"
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t("pages.listUpsert.fields.nameHint")}
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
                onMount: ({ value }) => getTtlError(value, t) ?? undefined,
                onChange: ({ value }) => getTtlError(value, t) ?? undefined,
              }}
            >
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)

                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor="list-ttl-ms">
                      {t("pages.listUpsert.fields.ttlMs")}
                    </FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id="list-ttl-ms"
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t("pages.listUpsert.fields.ttlMsHint")}
                        error={error ?? null}
                      />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>
          </FieldGroup>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>{t("pages.listUpsert.sourceSwitcher.title")}</CardTitle>
          <CardDescription>
            {t("pages.listUpsert.sourceSwitcher.description")}
          </CardDescription>
        </CardHeader>
        <CardContent>
          <ButtonGroup className="[&>[data-slot=button]]:flex-1">
            {LIST_SOURCE_GROUPS.map((group) => {
              const Icon = LIST_SOURCE_GROUP_ICONS[group]

              return (
                <Button
                  key={group}
                  onClick={() => handleSourceGroupSelect(group)}
                  size="sm"
                  type="button"
                  variant={
                    activeSourceGroups.includes(group) ? "secondary" : "outline"
                  }
                >
                  <Icon className="size-4" />
                  {t(`pages.listUpsert.sourceGroups.${group}.button`)}
                </Button>
              )
            })}
          </ButtonGroup>
        </CardContent>
      </Card>

      {activeSourceGroups.includes("url") ? (
        <Card>
          <CardHeader>
            <CardTitle>{t("pages.listUpsert.sourceGroups.url.title")}</CardTitle>
            <CardDescription>
              {t("pages.listUpsert.sourceGroups.url.description")}
            </CardDescription>
          </CardHeader>
          <CardContent>
            <FieldGroup>
              <form.Field name="url">
                {(field) => (
                  <Field>
                    <FieldLabel htmlFor="list-url">
                      {t("pages.listUpsert.fields.url")}
                    </FieldLabel>
                    <FieldContent>
                      <Input
                        id="list-url"
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t("pages.listUpsert.fields.urlHint")}
                      />
                    </FieldContent>
                  </Field>
                )}
              </form.Field>

            </FieldGroup>
          </CardContent>
        </Card>
      ) : null}

      {activeSourceGroups.includes("file") ? (
        <Card>
          <CardHeader>
            <CardTitle>
              {t("pages.listUpsert.sourceGroups.file.title")}
            </CardTitle>
            <CardDescription>
              {t("pages.listUpsert.sourceGroups.file.description")}
            </CardDescription>
          </CardHeader>
          <CardContent>
            <FieldGroup>
              <form.Field name="file">
                {(field) => (
                  <Field>
                    <FieldLabel htmlFor="list-file">
                      {t("pages.listUpsert.fields.file")}
                    </FieldLabel>
                    <FieldContent>
                      <Input
                        id="list-file"
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t("pages.listUpsert.fields.fileHint")}
                      />
                    </FieldContent>
                  </Field>
                )}
              </form.Field>
            </FieldGroup>
          </CardContent>
        </Card>
      ) : null}

      {activeSourceGroups.includes("inline") ? (
        <Card>
          <CardHeader>
            <CardTitle>
              {t("pages.listUpsert.sourceGroups.inline.title")}
            </CardTitle>
            <CardDescription>
              {t("pages.listUpsert.sourceGroups.inline.description")}
            </CardDescription>
          </CardHeader>
          <CardContent>
            <FieldGroup>
              <form.Field name="domains">
                {(field) => (
                  <Field>
                    <FieldLabel htmlFor="list-domains">
                      {t("pages.listUpsert.fields.domains")}
                    </FieldLabel>
                    <FieldContent>
                      <Textarea
                        className="min-h-24"
                        id="list-domains"
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t("pages.listUpsert.fields.domainsHint")}
                      />
                    </FieldContent>
                  </Field>
                )}
              </form.Field>
              <form.Field name="ipCidrs">
                {(field) => (
                  <Field>
                    <FieldLabel htmlFor="list-ip-cidrs">
                      {t("pages.listUpsert.fields.ipCidrs")}
                    </FieldLabel>
                    <FieldContent>
                      <Textarea
                        className="min-h-24"
                        id="list-ip-cidrs"
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t("pages.listUpsert.fields.ipCidrsHint")}
                      />
                    </FieldContent>
                  </Field>
                )}
              </form.Field>
            </FieldGroup>
          </CardContent>
        </Card>
      ) : null}

      {apiErrorMessage ? (
        <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription className="whitespace-pre-wrap">
            {apiErrorMessage}
          </AlertDescription>
        </Alert>
      ) : null}

      <div className="flex justify-end gap-3">
        <Button onClick={onCancel} size="xl" type="button" variant="outline">
          {t("common.cancel")}
        </Button>
        <form.Subscribe
          selector={(state) => ({
            canSubmit: state.canSubmit,
            isPristine: state.isPristine,
          })}
        >
          {({ canSubmit, isPristine }) => (
            <Button
              disabled={!isConfigLoaded || isPending || isPristine || !canSubmit}
              size="xl"
              type="submit"
            >
              {isPending
                ? t("pages.listUpsert.actions.saving")
                : mode === "create"
                  ? t("pages.listUpsert.actions.create")
                  : t("pages.listUpsert.actions.save")}
            </Button>
          )}
        </form.Subscribe>
      </div>
    </form>
  )
}

function getActiveSourceGroupsFromDraft(draft: ListDraft): ListSourceGroup[] {
  const populatedGroups: ListSourceGroup[] = []

  if (draft.url.trim()) {
    populatedGroups.push("url")
  }

  if (draft.file.trim()) {
    populatedGroups.push("file")
  }

  if (splitLines(draft.domains).length > 0 || splitLines(draft.ipCidrs).length > 0) {
    populatedGroups.push("inline")
  }

  return populatedGroups.length > 0 ? populatedGroups : [DEFAULT_SOURCE_GROUP]
}

function isSourceGroupPopulated(group: ListSourceGroup, draft: ListDraft) {
  if (group === "inline") {
    return (
      splitLines(draft.domains).length > 0 || splitLines(draft.ipCidrs).length > 0
    )
  }

  return draft[group].trim().length > 0
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
  const resolvedName =
    mode === "edit" ? (originalName?.trim() ?? trimmedName) : trimmedName
  const nextListConfig = getListConfigFromDraft(nextDraft)

  nextLists[resolvedName] = nextListConfig

  return {
    ...config,
    lists: nextLists,
  }
}

function getListConfigFromDraft(draft: ListDraft): ListConfig {
  const domains = splitLines(draft.domains)
  const ipCidrs = splitLines(draft.ipCidrs)
  const trimmedUrl = draft.url.trim()
  const trimmedFile = draft.file.trim()

  const listConfig: ListConfig = {}

  if (trimmedUrl) {
    listConfig.url = trimmedUrl
    listConfig.ttl_ms = Number.parseInt(draft.ttlMs.trim(), 10)
  }

  if (trimmedFile) {
    listConfig.file = trimmedFile
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
  currentName?: string,
  t?: (key: string) => string
) {
  const trimmedName = value.trim()
  if (!trimmedName) {
    return t?.("pages.listUpsert.validation.nameRequired") ?? "Name is required."
  }

  if (existingListNames.includes(trimmedName) && trimmedName !== currentName) {
    return (
      t?.("pages.listUpsert.validation.duplicateName") ??
      "A list with this name already exists."
    )
  }

  return null
}

function getTtlError(value: string, t?: (key: string) => string) {
  const trimmed = value.trim()
  if (!/^\d+$/.test(trimmed)) {
    return (
      t?.("pages.listUpsert.validation.invalidTtl") ??
      "TTL must be a non-negative integer."
    )
  }

  return null
}

function resolveListFieldPath(path: string, name: string) {
  const normalizedName = name.trim()

  if (path === "lists") {
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

  return undefined
}
