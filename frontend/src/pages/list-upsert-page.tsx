import { DeleteImpactDialog } from "@/components/shared/delete-impact-dialog"
import { validateListSource } from "@/lib/list-source-validation"
import { RadioGroup } from "@/components/ui/radio-group"
import { useQueryClient } from "@tanstack/react-query"
import { CloudIcon, FileTextIcon, ScrollTextIcon } from "lucide-react"
import { useState } from "react"
import { Trans, useTranslation } from "react-i18next"
import { useLocation } from "wouter"
import { toast } from "sonner"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { ListConfig } from "@/api/generated/model/listConfig"
import type { Outbound } from "@/api/generated/model/outbound"
import { usePostConfigMutation } from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
import { useGetConfig } from "@/api/queries"
import { selectConfig } from "@/api/selectors"
import { OutboundSelect } from "@/components/shared/outbound-select"
import {
  Field,
  FieldContent,
  FieldGroup,
  FieldHint,
  FieldLabel,
} from "@/components/shared/field"
import { ServerValidationAlert } from "@/components/shared/server-validation-alert"
import { UpsertPage } from "@/components/shared/upsert-page"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Textarea } from "@/components/ui/textarea"
import { formatPath, useDraftForm } from "@/lib/draft-form"
import { getTagNameValidationError } from "@/lib/tag-name-validation"
import { ChoiceButton } from "@/components/ui/choice-button"

type ListDraft = {
  name: string
  source: ListSourceGroup | ""
  ttl_ms: string
  detour: string
  domains: string
  ip_cidrs: string
  url: string
  file: string
}

type ListSourceGroup = "url" | "file" | "inline"

const LIST_SOURCE_GROUPS: ListSourceGroup[] = ["url", "file", "inline"]
const LIST_SOURCE_GROUP_ICONS = {
  url: CloudIcon,
  file: FileTextIcon,
  inline: ScrollTextIcon,
} satisfies Record<ListSourceGroup, typeof CloudIcon>
const LIST_SOURCE_GROUP_FIELDS = {
  url: ["url"],
  file: ["file"],
  inline: ["domains", "ip_cidrs"],
} satisfies Record<ListSourceGroup, (keyof ListDraft)[]>

const sampleNewList: ListDraft = {
  name: "",
  source: "",
  ttl_ms: "7200000",
  detour: "",
  domains: "",
  ip_cidrs: "",
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
  const configQuery = useGetConfig()
  const loadedConfig = selectConfig(configQuery.data)

  if (!loadedConfig) {
    return (
      <UpsertPage
        cardDescription={t("pages.listUpsert.cardDescription")}
        cardTitle={
          mode === "create"
            ? t("pages.listUpsert.createTitle")
            : t("pages.listUpsert.editTitle")
        }
        description={t("pages.listUpsert.description")}
        title={
          mode === "create"
            ? t("pages.listUpsert.createTitle")
            : t("pages.listUpsert.editTitle")
        }
      >
        <div className="space-y-3">
          <div className="h-8 rounded-lg bg-muted" />
          <div className="h-24 rounded-lg bg-muted" />
          <div className="h-8 rounded-lg bg-muted" />
          <div className="h-8 rounded-lg bg-muted" />
        </div>
      </UpsertPage>
    )
  }

  const listsMap = loadedConfig.lists ?? {}
  const draft =
    mode === "edit"
      ? getDraftFromMapEntry(listId, listId ? listsMap[listId] : undefined)
      : sampleNewList

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
        mode === "create" ? (
          t("pages.listUpsert.createTitle")
        ) : (
          <Trans
            i18nKey="pages.listUpsert.editCardTitle"
            values={{ name: draft?.name ?? listId }}
            components={{ entity: <span className="text-primary" /> }}
          />
        )
      }
    >
      <ListForm
        key={`${mode}:${listId ?? "new"}`}
        outbounds={loadedConfig.outbounds ?? []}
        draft={draft ?? sampleNewList}
        existingListNames={Object.keys(listsMap)}
        listId={listId}
        loadedConfig={loadedConfig}
        mode={mode}
      />
    </UpsertPage>
  )
}

function ListForm({
  mode,
  outbounds,
  draft,
  existingListNames,
  listId,
  loadedConfig,
}: {
  mode: "create" | "edit"
  outbounds: Outbound[]
  draft: ListDraft
  existingListNames: string[]
  listId?: string
  loadedConfig: ConfigObject
}) {
  const { t } = useTranslation()
  const queryClient = useQueryClient()
  const [, navigate] = useLocation()
  const [activeSourceGroups, setActiveSourceGroups] = useState<
    ListSourceGroup[]
  >(() => getActiveSourceGroupsFromDraft(draft))
  const [pendingSource, setPendingSource] = useState<ListSourceGroup | null>(
    null
  )
  const postConfigMutation = usePostConfigMutation()
  const isCreate = mode === "create"

  const form = useDraftForm<ListDraft>(draft, {
    // The list lives under its name in the `lists` object.
    apiPrefix: (value) =>
      formatPath([
        "lists",
        isCreate ? value.name.trim() : (listId ?? draft.name).trim(),
      ]),
    validate: (value) => {
      const errors: Record<string, string> = validateListSource(value, t)

      const nameError = getListNameError(
        value.name,
        existingListNames,
        isCreate ? undefined : draft.name,
        t
      )
      if (nameError) {
        errors.name = nameError
      }

      if (!value.source && activeSourceGroups.length === 0) {
        errors.source = t("common.validation.required")
      }

      const ttlError = getTtlError(value.ttl_ms, t)
      if (ttlError) {
        errors.ttl_ms = ttlError
      }

      return errors
    },
  })
  const { values } = form

  const save = async (value: ListDraft) => {
    const updatedConfig = buildUpdatedConfigForListUpsert(
      loadedConfig,
      mode,
      value,
      listId
    )

    try {
      await postConfigMutation.mutateAsync({ data: updatedConfig })
      toast.success(
        mode === "create"
          ? t("pages.listUpsert.messages.created")
          : t("pages.listUpsert.messages.updated")
      )
      await Promise.all([
        queryClient.invalidateQueries({ queryKey: queryKeys.config() }),
        queryClient.invalidateQueries({ queryKey: queryKeys.dnsTest() }),
      ])
      navigate("/lists")
    } catch (error) {
      const message = form.setApiError(error as ApiError)
      if (message) {
        toast.error(message, { richColors: true })
      }
    }
  }

  // The name control also shows errors addressed to the whole list entry.
  const nameError = form.errorFor("name", { alsoClaims: [""] })

  const handleSourceGroupSelect = (
    group: ListSourceGroup,
    confirmed = false
  ) => {
    const filledActiveGroups = activeSourceGroups.filter((sourceGroup) =>
      isSourceGroupPopulated(sourceGroup, values)
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

    if (groupsToClear.length > 0 && !confirmed) {
      setPendingSource(group)
      return
    }
    setPendingSource(null)

    setActiveSourceGroups([group])

    form.setValue("source", group)
    for (const sourceGroup of LIST_SOURCE_GROUPS) {
      if (sourceGroup === group) continue
      for (const fieldName of LIST_SOURCE_GROUP_FIELDS[sourceGroup]) {
        // Clear errors even when the hidden source value was already empty.
        form.setValue(fieldName, "")
      }
    }
  }

  return (
    <form className="space-y-6" onSubmit={form.onSubmit(save)}>
      <DeleteImpactDialog
        open={pendingSource !== null}
        onOpenChange={(open) => {
          if (!open) setPendingSource(null)
        }}
        title={t("pages.listUpsert.sourceSwitcher.confirmTitle")}
        description={t("pages.listUpsert.sourceSwitcher.confirmChange")}
        confirmLabel={t("pages.listUpsert.sourceSwitcher.confirmAction")}
        onConfirm={() => {
          if (pendingSource) handleSourceGroupSelect(pendingSource, true)
        }}
        impactItems={activeSourceGroups
          .filter(
            (group) =>
              group !== pendingSource && isSourceGroupPopulated(group, values)
          )
          .map((group) => ({
            label: t(`pages.listUpsert.sourceGroups.${group}.button`),
          }))}
      />
      <section className="space-y-4">
        <div>
          <FieldGroup>
            <Field invalid={Boolean(nameError)}>
              <FieldLabel htmlFor="list-name">
                {t("pages.listUpsert.fields.name")}
              </FieldLabel>
              <FieldContent>
                <Input
                  aria-invalid={Boolean(nameError)}
                  disabled={!isCreate}
                  id="list-name"
                  className="max-w-sm"
                  onChange={(event) =>
                    form.setValue("name", event.target.value)
                  }
                  value={values.name}
                />
                <FieldHint error={nameError} />
              </FieldContent>
            </Field>

            <Field invalid={Boolean(form.errorFor("ttl_ms"))}>
              <FieldLabel htmlFor="list-ttl-ms">
                {t("pages.listUpsert.fields.ttlMs")}
              </FieldLabel>
              <FieldContent>
                <Input
                  aria-invalid={Boolean(form.errorFor("ttl_ms"))}
                  id="list-ttl-ms"
                  inputMode="numeric"
                  onChange={(event) =>
                    form.setValue("ttl_ms", event.target.value)
                  }
                  value={values.ttl_ms}
                />
                <FieldHint
                  description={t("pages.listUpsert.fields.ttlMsHint")}
                  error={form.errorFor("ttl_ms")}
                />
              </FieldContent>
            </Field>
          </FieldGroup>
        </div>
      </section>

      <Field invalid={Boolean(form.errorFor("source"))}>
        <FieldLabel id="list-source-label">
          {t("pages.listUpsert.sourceSwitcher.title")}
        </FieldLabel>
        <FieldContent>
          <RadioGroup
            aria-labelledby="list-source-label"
            value={activeSourceGroups.length === 1 ? activeSourceGroups[0] : ""}
            onValueChange={(value) =>
              handleSourceGroupSelect(value as ListSourceGroup)
            }
            className="grid w-full max-w-3xl min-w-0 gap-3 sm:grid-cols-2 lg:grid-cols-3"
          >
            {LIST_SOURCE_GROUPS.map((group) => {
              const Icon = LIST_SOURCE_GROUP_ICONS[group]
              return (
                <ChoiceButton value={group} key={group}>
                  <Icon aria-hidden="true" className="size-5 text-primary" />
                  <span className="text-sm font-medium">
                    {t(`pages.listUpsert.sourceGroups.${group}.button`)}
                  </span>
                  <span className="text-xs text-muted-foreground">
                    {t(`pages.listUpsert.sourceGroups.${group}.description`)}
                  </span>
                </ChoiceButton>
              )
            })}
          </RadioGroup>
          <FieldHint error={form.errorFor("source")} />
        </FieldContent>
      </Field>

      {activeSourceGroups.includes("url") ? (
        <section className="space-y-4">
          <div>
            <FieldGroup>
              <Field invalid={Boolean(form.errorFor("url"))}>
                <FieldLabel htmlFor="list-url">
                  {t("pages.listUpsert.fields.url")}
                </FieldLabel>
                <FieldContent>
                  <Input
                    aria-invalid={Boolean(form.errorFor("url"))}
                    id="list-url"
                    onChange={(event) =>
                      form.setValue("url", event.target.value)
                    }
                    value={values.url}
                  />
                  <FieldHint
                    description={t("pages.listUpsert.fields.urlHint")}
                    error={form.errorFor("url")}
                  />
                </FieldContent>
              </Field>

              <Field invalid={Boolean(form.errorFor("detour"))}>
                <FieldLabel>{t("pages.listUpsert.fields.detour")}</FieldLabel>
                <FieldContent>
                  <OutboundSelect
                    allowEmpty
                    ariaInvalid={Boolean(form.errorFor("detour"))}
                    emptyLabel={t("pages.listUpsert.fields.detourEmpty")}
                    onValueChange={(value) => form.setValue("detour", value)}
                    outbounds={outbounds}
                    placeholder={t("pages.listUpsert.fields.detourPlaceholder")}
                    value={values.detour}
                  />
                  <FieldHint
                    description={t("pages.listUpsert.fields.detourHint")}
                    error={form.errorFor("detour")}
                  />
                </FieldContent>
              </Field>
            </FieldGroup>
          </div>
        </section>
      ) : null}

      {activeSourceGroups.includes("file") ? (
        <section className="space-y-4">
          <div>
            <FieldGroup>
              <Field invalid={Boolean(form.errorFor("file"))}>
                <FieldLabel htmlFor="list-file">
                  {t("pages.listUpsert.fields.file")}
                </FieldLabel>
                <FieldContent>
                  <Input
                    aria-invalid={Boolean(form.errorFor("file"))}
                    id="list-file"
                    onChange={(event) =>
                      form.setValue("file", event.target.value)
                    }
                    value={values.file}
                  />
                  <FieldHint
                    description={t("pages.listUpsert.fields.fileHint")}
                    error={form.errorFor("file")}
                  />
                </FieldContent>
              </Field>
            </FieldGroup>
          </div>
        </section>
      ) : null}

      {activeSourceGroups.includes("inline") ? (
        <section className="space-y-4">
          <div>
            <FieldGroup className="grid min-w-0 items-start gap-6 md:grid-cols-2">
              <Field invalid={Boolean(form.errorFor("domains"))}>
                <FieldLabel htmlFor="list-domains">
                  {t("pages.listUpsert.fields.domains")}
                </FieldLabel>
                <FieldContent>
                  <Textarea
                    className="min-h-24"
                    aria-invalid={Boolean(form.errorFor("domains"))}
                    id="list-domains"
                    onChange={(event) =>
                      form.setValue("domains", event.target.value)
                    }
                    value={values.domains}
                  />
                  <FieldHint
                    description={t("pages.listUpsert.fields.domainsHint")}
                    error={form.errorFor("domains")}
                  />
                </FieldContent>
              </Field>
              <Field invalid={Boolean(form.errorFor("ip_cidrs"))}>
                <FieldLabel htmlFor="list-ip-cidrs">
                  {t("pages.listUpsert.fields.ipCidrs")}
                </FieldLabel>
                <FieldContent>
                  <Textarea
                    className="min-h-24"
                    aria-invalid={Boolean(form.errorFor("ip_cidrs"))}
                    id="list-ip-cidrs"
                    onChange={(event) =>
                      form.setValue("ip_cidrs", event.target.value)
                    }
                    value={values.ip_cidrs}
                  />
                  <FieldHint
                    error={form.errorFor("ip_cidrs")}
                    description={
                      <Trans
                        i18nKey="pages.listUpsert.fields.ipCidrsHint"
                        components={{
                          code: (
                            <code className="rounded border bg-muted px-1.5 py-0.5 font-mono text-xs text-foreground" />
                          ),
                        }}
                      />
                    }
                  />
                </FieldContent>
              </Field>
            </FieldGroup>
          </div>
        </section>
      ) : null}

      {form.formError ? (
        <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription className="whitespace-pre-wrap">
            {form.formError}
          </AlertDescription>
        </Alert>
      ) : null}

      <ServerValidationAlert errors={form.unmappedErrors()} />

      <div className="flex justify-end gap-3">
        <Button
          onClick={() => navigate("/lists")}
          size="xl"
          type="button"
          variant="outline"
        >
          {t("common.cancel")}
        </Button>
        <Button
          disabled={
            postConfigMutation.isPending ||
            (mode === "edit" && !form.isDirty) ||
            form.isSubmitting
          }
          size="xl"
          type="submit"
        >
          {postConfigMutation.isPending
            ? t("pages.listUpsert.actions.saving")
            : mode === "create"
              ? t("pages.listUpsert.actions.create")
              : t("pages.listUpsert.actions.save")}
        </Button>
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

  if (
    splitLines(draft.domains).length > 0 ||
    splitLines(draft.ip_cidrs).length > 0
  ) {
    populatedGroups.push("inline")
  }

  return populatedGroups
}

function isSourceGroupPopulated(group: ListSourceGroup, draft: ListDraft) {
  if (group === "inline") {
    return (
      splitLines(draft.domains).length > 0 ||
      splitLines(draft.ip_cidrs).length > 0
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
    source: "",
    ttl_ms: String(listConfig.ttl_ms ?? 0),
    detour: listConfig.detour ?? "",
    domains: (listConfig.domains ?? []).join("\n"),
    ip_cidrs: (listConfig.ip_cidrs ?? []).join("\n"),
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
  const ipCidrs = splitLines(draft.ip_cidrs)
  const trimmedUrl = draft.url.trim()
  const trimmedFile = draft.file.trim()
  const trimmedDetour = draft.detour.trim()
  const ttlMs = Number.parseInt(draft.ttl_ms.trim(), 10)

  const listConfig: ListConfig = {}
  listConfig.ttl_ms = Number.isNaN(ttlMs) ? 0 : ttlMs

  if (trimmedUrl) {
    listConfig.url = trimmedUrl
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

  if (trimmedDetour) {
    listConfig.detour = trimmedDetour
  }

  return listConfig
}

function splitLines(value: string) {
  return value
    .split("\n")
    .map((entry) => entry.trim())
    .filter(Boolean)
}

function getListNameError(
  value: string,
  existingListNames: string[],
  currentName: string | undefined,
  t: (key: string) => string
) {
  const trimmedName = value.trim()
  const duplicateError =
    existingListNames.includes(trimmedName) && trimmedName !== currentName
      ? t("pages.listUpsert.validation.duplicateName")
      : null

  return getTagNameValidationError(value, {
    requiredError: t("common.validation.required"),
    invalidError: t("common.validation.tagNamePattern"),
    duplicateError,
  })
}

function getTtlError(value: string, t: (key: string) => string) {
  const trimmed = value.trim()
  if (!/^\d+$/.test(trimmed) || Number(trimmed) > 4294967295999) {
    return t("pages.listUpsert.validation.invalidTtl")
  }

  return null
}
