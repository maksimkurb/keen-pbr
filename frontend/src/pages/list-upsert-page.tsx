import { useQueryClient } from "@tanstack/react-query"
import {
  CheckCircle2Icon,
  CircleIcon,
  CloudIcon,
  FileTextIcon,
  ScrollTextIcon,
} from "lucide-react"
import { useState } from "react"
import { useTranslation } from "react-i18next"
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
import { ButtonGroup } from "@/components/ui/button-group"
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card"
import { Input } from "@/components/ui/input"
import { Textarea } from "@/components/ui/textarea"
import { splitFormApiErrors, useDraftForm } from "@/lib/draft-form"
import { cn } from "@/lib/utils"
import { getTagNameValidationError } from "@/lib/tag-name-validation"
import { useIsMobile } from "@/hooks/use-mobile"

type ListDraft = {
  name: string
  ttlMs: string
  detour: string
  domains: string
  ipCidrs: string
  url: string
  file: string
}

type ListSourceGroup = "url" | "file" | "inline"

const LIST_FIELD_NAMES = [
  "name",
  "ttlMs",
  "detour",
  "domains",
  "ipCidrs",
  "url",
  "file",
] as const
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
  ttlMs: "7200000",
  detour: "",
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
        mode === "create"
          ? t("pages.listUpsert.createTitle")
          : t("pages.listUpsert.editTitle")
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
  const postConfigMutation = usePostConfigMutation()
  const isMobile = useIsMobile()
  const isCreate = mode === "create"

  const form = useDraftForm<ListDraft>(draft, {
    validate: (value) => {
      const errors: Record<string, string> = {}

      const nameError = getListNameError(
        value.name,
        existingListNames,
        isCreate ? undefined : draft.name,
        t
      )
      if (nameError) {
        errors.name = nameError
      }

      const ttlError = getTtlError(value.ttlMs, t)
      if (ttlError) {
        errors.ttlMs = ttlError
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
      const result = splitFormApiErrors({
        error: error as ApiError,
        fieldNames: LIST_FIELD_NAMES,
        resolvePath: (path) =>
          resolveListFieldPath(path, value.name || draft.name),
      })
      form.setServerErrors({
        form: result.formError,
        fields: result.fieldErrors,
        unmapped: result.unmappedErrors,
      })
      if (result.formError) {
        toast.error(result.formError, { richColors: true })
      }
    }
  }

  const handleSourceGroupSelect = (group: ListSourceGroup) => {
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

    if (
      groupsToClear.length > 0 &&
      !window.confirm(t("pages.listUpsert.sourceSwitcher.confirmChange"))
    ) {
      return
    }

    setActiveSourceGroups([group])

    form.setValues((prev) => {
      const next = { ...prev }
      for (const sourceGroup of LIST_SOURCE_GROUPS) {
        if (sourceGroup === group) {
          continue
        }

        for (const fieldName of LIST_SOURCE_GROUP_FIELDS[sourceGroup]) {
          next[fieldName] = ""
        }
      }

      if (group !== "inline") {
        next.domains = ""
        next.ipCidrs = ""
      }

      return next
    })
  }

  return (
    <form className="space-y-6" onSubmit={form.onSubmit(save)}>
      <Card>
        <CardHeader>
          <CardTitle>{t("pages.listUpsert.common.title")}</CardTitle>
          <CardDescription>
            {t("pages.listUpsert.common.description")}
          </CardDescription>
        </CardHeader>
        <CardContent>
          <FieldGroup>
            <Field invalid={Boolean(form.errorFor("name"))}>
              <FieldLabel htmlFor="list-name">
                {t("pages.listUpsert.fields.name")}
              </FieldLabel>
              <FieldContent>
                <Input
                  aria-invalid={Boolean(form.errorFor("name"))}
                  disabled={!isCreate}
                  id="list-name"
                  onChange={(event) => form.setValue("name", event.target.value)}
                  value={values.name}
                />
                <FieldHint
                  description={t("pages.listUpsert.fields.nameHint")}
                  error={form.errorFor("name")}
                />
              </FieldContent>
            </Field>

            <Field invalid={Boolean(form.errorFor("ttlMs"))}>
              <FieldLabel htmlFor="list-ttl-ms">
                {t("pages.listUpsert.fields.ttlMs")}
              </FieldLabel>
              <FieldContent>
                <Input
                  aria-invalid={Boolean(form.errorFor("ttlMs"))}
                  id="list-ttl-ms"
                  onChange={(event) =>
                    form.setValue("ttlMs", event.target.value)
                  }
                  value={values.ttlMs}
                />
                <FieldHint
                  description={t("pages.listUpsert.fields.ttlMsHint")}
                  error={form.errorFor("ttlMs")}
                />
              </FieldContent>
            </Field>
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
          <ButtonGroup
            className="w-full [&>[data-slot=button]]:flex-1 data-[orientation=vertical]:h-fit data-[orientation=vertical]:[&>[data-slot=button]]:w-full data-[orientation=vertical]:[&>[data-slot=button]]:justify-start data-[orientation=vertical]:[&>[data-slot=button]]:px-3"
            orientation={isMobile ? "vertical" : "horizontal"}
          >
            {LIST_SOURCE_GROUPS.map((group) => {
              const Icon = LIST_SOURCE_GROUP_ICONS[group]
              const active = activeSourceGroups.includes(group)

              return (
                <Button
                  aria-pressed={active}
                  className={cn(
                    isMobile && "h-auto min-h-11 py-2.5",
                    active &&
                      "border-border bg-secondary text-secondary-foreground hover:bg-secondary/80"
                  )}
                  key={group}
                  onClick={() => handleSourceGroupSelect(group)}
                  size={isMobile ? "default" : "sm"}
                  type="button"
                  variant="outline"
                >
                  {isMobile ? (
                    active ? (
                      <CheckCircle2Icon className="size-4 text-primary" />
                    ) : (
                      <CircleIcon className="size-4 text-muted-foreground" />
                    )
                  ) : null}
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
            <CardTitle>
              {t("pages.listUpsert.sourceGroups.url.title")}
            </CardTitle>
            <CardDescription>
              {t("pages.listUpsert.sourceGroups.url.description")}
            </CardDescription>
          </CardHeader>
          <CardContent>
            <FieldGroup>
              <Field>
                <FieldLabel htmlFor="list-url">
                  {t("pages.listUpsert.fields.url")}
                </FieldLabel>
                <FieldContent>
                  <Input
                    id="list-url"
                    onChange={(event) =>
                      form.setValue("url", event.target.value)
                    }
                    value={values.url}
                  />
                  <FieldHint
                    description={t("pages.listUpsert.fields.urlHint")}
                  />
                </FieldContent>
              </Field>

              <Field invalid={Boolean(form.errorFor("detour"))}>
                <FieldLabel>
                  {t("pages.listUpsert.fields.detour")}
                </FieldLabel>
                <FieldContent>
                  <OutboundSelect
                    allowEmpty
                    ariaInvalid={Boolean(form.errorFor("detour"))}
                    emptyLabel={t("pages.listUpsert.fields.detourEmpty")}
                    onValueChange={(value) => form.setValue("detour", value)}
                    outbounds={outbounds}
                    placeholder={t(
                      "pages.listUpsert.fields.detourPlaceholder"
                    )}
                    value={values.detour}
                  />
                  <FieldHint
                    description={t("pages.listUpsert.fields.detourHint")}
                    error={form.errorFor("detour")}
                  />
                </FieldContent>
              </Field>
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
              <Field>
                <FieldLabel htmlFor="list-file">
                  {t("pages.listUpsert.fields.file")}
                </FieldLabel>
                <FieldContent>
                  <Input
                    id="list-file"
                    onChange={(event) =>
                      form.setValue("file", event.target.value)
                    }
                    value={values.file}
                  />
                  <FieldHint
                    description={t("pages.listUpsert.fields.fileHint")}
                  />
                </FieldContent>
              </Field>
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
              <Field>
                <FieldLabel htmlFor="list-domains">
                  {t("pages.listUpsert.fields.domains")}
                </FieldLabel>
                <FieldContent>
                  <Textarea
                    className="min-h-24"
                    id="list-domains"
                    onChange={(event) =>
                      form.setValue("domains", event.target.value)
                    }
                    value={values.domains}
                  />
                  <FieldHint
                    description={t("pages.listUpsert.fields.domainsHint")}
                  />
                </FieldContent>
              </Field>
              <Field>
                <FieldLabel htmlFor="list-ip-cidrs">
                  {t("pages.listUpsert.fields.ipCidrs")}
                </FieldLabel>
                <FieldContent>
                  <Textarea
                    className="min-h-24"
                    id="list-ip-cidrs"
                    onChange={(event) =>
                      form.setValue("ipCidrs", event.target.value)
                    }
                    value={values.ipCidrs}
                  />
                  <FieldHint
                    description={t("pages.listUpsert.fields.ipCidrsHint")}
                  />
                </FieldContent>
              </Field>
            </FieldGroup>
          </CardContent>
        </Card>
      ) : null}

      {form.errors.form ? (
        <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription className="whitespace-pre-wrap">
            {form.errors.form}
          </AlertDescription>
        </Alert>
      ) : null}

      <ServerValidationAlert errors={form.errors.unmapped} />

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
            postConfigMutation.isPending || !form.isDirty || form.isSubmitting
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
    splitLines(draft.ipCidrs).length > 0
  ) {
    populatedGroups.push("inline")
  }

  return populatedGroups.length > 0 ? populatedGroups : [DEFAULT_SOURCE_GROUP]
}

function isSourceGroupPopulated(group: ListSourceGroup, draft: ListDraft) {
  if (group === "inline") {
    return (
      splitLines(draft.domains).length > 0 ||
      splitLines(draft.ipCidrs).length > 0
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
    ttlMs: String(listConfig.ttl_ms ?? 0),
    detour: listConfig.detour ?? "",
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
  const trimmedDetour = draft.detour.trim()
  const ttlMs = Number.parseInt(draft.ttlMs.trim(), 10)

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
  currentName?: string,
  t?: (key: string) => string
) {
  const trimmedName = value.trim()
  const duplicateError =
    existingListNames.includes(trimmedName) && trimmedName !== currentName
      ? (t?.("pages.listUpsert.validation.duplicateName") ??
        "A list with this name already exists.")
      : null

  return getTagNameValidationError(value, {
    requiredError:
      t?.("pages.listUpsert.validation.nameRequired") ?? "Name is required.",
    invalidError:
      t?.("common.validation.tagNamePattern") ??
      "Must match [a-z][a-z0-9_]{0,23}.",
    duplicateError,
  })
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

function resolveListFieldPath(path: string, name: string): string | undefined {
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

  if (normalizedName && path === `lists.${normalizedName}.detour`) {
    return "detour"
  }

  return undefined
}
