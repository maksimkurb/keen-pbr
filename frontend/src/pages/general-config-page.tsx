import { useEffect, useRef, useState } from "react"
import { useTranslation } from "react-i18next"

import { useForm } from "@tanstack/react-form"
import { useQueryClient } from "@tanstack/react-query"
import { useStore } from "@tanstack/react-store"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
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
  FieldSeparator,
} from "@/components/shared/field"
import { ListPlaceholder } from "@/components/shared/list-placeholder"
import { PageHeader } from "@/components/shared/page-header"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card"
import { Checkbox } from "@/components/ui/checkbox"
import { Input } from "@/components/ui/input"
import { Skeleton } from "@/components/ui/skeleton"
import { applyFormApiErrors, clearFormServerErrors } from "@/lib/form-api-errors"

type SettingsDraft = {
  strictEnforcement: boolean
  listsAutoupdateEnabled: boolean
  cron: string
  fwmarkStart: string
  fwmarkMask: string
  tableStart: string
}

const fallbackDraft: SettingsDraft = {
  strictEnforcement: true,
  listsAutoupdateEnabled: true,
  cron: "0 */6 * * *",
  fwmarkStart: "0x00010000",
  fwmarkMask: "0xffff0000",
  tableStart: "150",
}

export function GeneralConfigPage() {
  const { t } = useTranslation()
  const configQuery = useGetConfig()
  const loadedConfig = selectConfig(configQuery.data)

  return (
    <div className="space-y-6">
      <PageHeader
        description={t("pages.settings.description")}
        title={t("pages.settings.title")}
      />

      {configQuery.isLoading ? (
        <GeneralConfigPageSkeleton />
      ) : configQuery.isError || !loadedConfig ? (
        <ListPlaceholder
          description="We can't load settings right now. Try refreshing the page."
          title="Unable to load data"
          variant="error"
        />
      ) : (
        <LoadedGeneralConfigPage
          loadedConfig={loadedConfig}
        />
      )}
    </div>
  )
}

type LoadedGeneralConfigPageProps = {
  loadedConfig: ConfigObject
}

function LoadedGeneralConfigPage({
  loadedConfig,
}: LoadedGeneralConfigPageProps) {
  const { t } = useTranslation()
  const queryClient = useQueryClient()

  const [savedDraft, setSavedDraft] = useState<SettingsDraft>(() =>
    getDraftFromConfig(loadedConfig)
  )
  const [saveSuccessMessage, setSaveSuccessMessage] = useState<string | null>(
    null
  )

  const postConfigMutation = usePostConfigMutation({
    mutation: {
      onSuccess: async (_response, variables) => {
        setSaveSuccessMessage(t("pages.settings.saved"))
        clearFormServerErrors(form)

        await Promise.all([
          queryClient.invalidateQueries({ queryKey: queryKeys.config() }),
          queryClient.invalidateQueries({
            queryKey: queryKeys.healthService(),
          }),
          queryClient.invalidateQueries({
            queryKey: queryKeys.healthRouting(),
          }),
        ])

        const nextSavedDraft = getDraftFromConfig(variables.data)
        setSavedDraft(nextSavedDraft)
        form.reset(nextSavedDraft)
      },
      onError: (error) => {
        const apiError = error as ApiError
        setSaveSuccessMessage(null)
        applyFormApiErrors({
          error: apiError,
          form,
          resolvePath: resolveSettingsFieldPath,
        })
      },
    },
  })

  const form = useForm({
    defaultValues: savedDraft,
    onSubmit: ({ value }) => {
      const updatedConfig = buildUpdatedConfig(loadedConfig, value)
      setSaveSuccessMessage(null)
      clearFormServerErrors(form)
      postConfigMutation.mutate({ data: updatedConfig })
    },
  })

  const formValues = useStore(form.store, (state) => state.values)
  const mutationErrorMessage = useStore(
    form.store,
    (state) =>
      (
        state.errorMap as {
          onServer?: {
            form?: string
          }
        }
      ).onServer?.form
  )
  const hasServerErrors = useStore(
    form.store,
    (state) =>
      state.errorMap.onServer !== undefined ||
      Object.values(state.fieldMetaBase).some(
        (fieldMeta) => fieldMeta?.errorMap?.onServer !== undefined
      )
  )
  const previousValuesRef = useRef(formValues)

  useEffect(() => {
    const previousValues = previousValuesRef.current
    previousValuesRef.current = formValues

    if (previousValues === formValues || !hasServerErrors) {
      return
    }

    clearFormServerErrors(form)
  }, [form, formValues, hasServerErrors])

  const isPending = postConfigMutation.isPending

  const handleCancel = () => {
    form.reset(savedDraft)
    clearFormServerErrors(form)
    setSaveSuccessMessage(null)
  }

  return (
    <>
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

      <Card>
        <CardHeader>
          <CardTitle>{t("pages.settings.general.title")}</CardTitle>
          <CardDescription>
            {t("pages.settings.general.description")}
          </CardDescription>
        </CardHeader>
        <CardContent>
          <FieldGroup>
            <form.Field name="strictEnforcement">
              {(field) => (
                <Field>
                  <FieldContent>
                    <div className="flex items-center space-x-3">
                      <Checkbox
                        checked={field.state.value}
                        id="strict-enforcement"
                        onCheckedChange={(checked) =>
                          field.handleChange(checked === true)
                        }
                      />
                      <FieldLabel
                        className="cursor-pointer flex-col items-start gap-0"
                        htmlFor="strict-enforcement"
                      >
                        {t("pages.settings.general.strictEnforcementLabel")}
                      </FieldLabel>
                    </div>
                    <FieldHint
                      description={t("pages.settings.general.strictEnforcementHint")}
                    />
                  </FieldContent>
                </Field>
              )}
            </form.Field>
          </FieldGroup>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>{t("pages.settings.autoupdate.title")}</CardTitle>
          <CardDescription>
            {t("pages.settings.autoupdate.description")}
          </CardDescription>
        </CardHeader>
        <CardContent>
          <FieldGroup>
            <form.Field name="listsAutoupdateEnabled">
              {(field) => (
                <Field>
                  <FieldContent>
                    <div className="flex items-center space-x-3">
                      <Checkbox
                        checked={field.state.value}
                        id="autoupdate-lists"
                        onCheckedChange={(checked) =>
                          field.handleChange(checked === true)
                        }
                      />
                      <FieldLabel
                        className="cursor-pointer flex-col items-start gap-0"
                        htmlFor="autoupdate-lists"
                      >
                        {t("pages.settings.autoupdate.enabledLabel")}
                      </FieldLabel>
                    </div>
                    <FieldHint
                      description={t("pages.settings.autoupdate.enabledHint")}
                    />
                  </FieldContent>
                </Field>
              )}
            </form.Field>

            <FieldSeparator />

            <form.Field name="cron">
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)

                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor="general-cron">
                      {t("pages.settings.autoupdate.cronLabel")}
                    </FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id="general-cron"
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={
                          <>
                            {t("pages.settings.autoupdate.cronHintPrefix")}{" "}
                            <a
                              className="underline underline-offset-3 hover:text-foreground"
                              href={getCrontabGuruUrl(field.state.value)}
                              rel="noreferrer"
                              target="_blank"
                            >
                              Crontab Guru
                            </a>
                            {" "}
                            {t("pages.settings.autoupdate.cronHintSuffix")}
                          </>
                        }
                        error={
                          error ? (
                            <>
                              {error}{" "}
                              <a
                                className="underline underline-offset-3 hover:text-foreground"
                                href={getCrontabGuruUrl(field.state.value)}
                                rel="noreferrer"
                                target="_blank"
                              >
                                {t("pages.settings.autoupdate.openInGuru")}
                              </a>
                              .
                            </>
                          ) : null
                        }
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
          <CardTitle>{t("pages.settings.advanced.title")}</CardTitle>
          <CardDescription>
            {t("pages.settings.advanced.description")}
          </CardDescription>
        </CardHeader>
        <CardContent>
          <FieldGroup>
            <form.Field name="fwmarkStart">
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)

                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor="fwmark-start">
                      {t("pages.settings.advanced.fwmarkStartLabel")}
                    </FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id="fwmark-start"
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t("pages.settings.advanced.fwmarkStartHint")}
                        error={error ?? null}
                      />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>

            <FieldSeparator />

            <form.Field name="fwmarkMask">
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)

                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor="fwmark-mask">
                      {t("pages.settings.advanced.fwmarkMaskLabel")}
                    </FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id="fwmark-mask"
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={
                          <>
                            {t("pages.settings.advanced.fwmarkMaskHintPrefix")}{" "}
                            <code>f</code> {t("pages.settings.advanced.fwmarkMaskHintSuffix")}{" "}
                            <code>0x00ff0000</code>.
                          </>
                        }
                        error={error ?? null}
                      />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>

            <FieldSeparator />

            <form.Field name="tableStart">
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)

                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor="table-start">
                      {t("pages.settings.advanced.tableStartLabel")}
                    </FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id="table-start"
                        onBlur={field.handleBlur}
                        onChange={(event) =>
                          field.handleChange(event.target.value)
                        }
                        value={field.state.value}
                      />
                      <FieldHint
                        description={t("pages.settings.advanced.tableStartHint")}
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

      <div className="flex justify-end gap-2">
        <Button
          disabled={isPending}
          onClick={handleCancel}
          size="xl"
          variant="outline"
        >
          Cancel
        </Button>
        <form.Subscribe
          selector={(state) => ({
            canSubmit: state.canSubmit,
            isPristine: state.isPristine,
          })}
        >
          {({ canSubmit, isPristine }) => (
            <Button
              disabled={isPending || !canSubmit || isPristine}
              onClick={() => form.handleSubmit()}
              size="xl"
            >
              {isPending
                ? t("pages.settings.actions.saving")
                : t("pages.settings.actions.save")}
            </Button>
          )}
        </form.Subscribe>
      </div>
    </>
  )
}

function GeneralConfigPageSkeleton() {
  return (
    <>
      <Card>
        <CardHeader>
          <Skeleton className="h-6 w-28" />
          <Skeleton className="h-4 w-56" />
        </CardHeader>
        <CardContent>
          <div className="space-y-3">
            <div className="flex items-start gap-3">
              <Skeleton className="mt-0.5 h-4 w-4 rounded-sm" />
              <div className="space-y-2">
                <Skeleton className="h-4 w-48" />
                <Skeleton className="h-4 w-full max-w-3xl" />
                <Skeleton className="h-4 w-5/6 max-w-2xl" />
              </div>
            </div>
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <Skeleton className="h-6 w-40" />
          <Skeleton className="h-4 w-64" />
        </CardHeader>
        <CardContent>
          <div className="space-y-6">
            <div className="flex items-start gap-3">
              <Skeleton className="mt-0.5 h-4 w-4 rounded-sm" />
              <div className="space-y-2">
                <Skeleton className="h-4 w-44" />
                <Skeleton className="h-4 w-full max-w-3xl" />
              </div>
            </div>
            <Skeleton className="h-px w-full" />
            <div className="space-y-3">
              <Skeleton className="h-4 w-14" />
              <Skeleton className="h-10 w-full" />
              <Skeleton className="h-4 w-80" />
            </div>
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <Skeleton className="h-6 w-56" />
          <Skeleton className="h-4 w-72" />
        </CardHeader>
        <CardContent>
          <div className="space-y-6">
            {[0, 1, 2].map((index) => (
              <div className="space-y-6" key={index}>
                {index > 0 ? <Skeleton className="h-px w-full" /> : null}
                <div className="space-y-3">
                  <Skeleton className="h-4 w-52" />
                  <Skeleton className="h-10 w-full" />
                  <Skeleton className="h-4 w-full max-w-2xl" />
                </div>
              </div>
            ))}
          </div>
        </CardContent>
      </Card>

      <div className="flex justify-end gap-2">
        <Skeleton className="h-11 w-24" />
        <Skeleton className="h-11 w-24" />
      </div>
    </>
  )
}

function getFirstFieldError(errors: unknown[]) {
  const firstError = errors[0]
  return typeof firstError === "string" ? firstError : null
}

function getDraftFromConfig(config: ConfigObject): SettingsDraft {
  return {
    strictEnforcement:
      config.daemon?.strict_enforcement ?? fallbackDraft.strictEnforcement,
    listsAutoupdateEnabled:
      config.lists_autoupdate?.enabled ?? fallbackDraft.listsAutoupdateEnabled,
    cron: config.lists_autoupdate?.cron ?? fallbackDraft.cron,
    fwmarkStart: toHex32(config.fwmark?.start, fallbackDraft.fwmarkStart),
    fwmarkMask: toHex32(config.fwmark?.mask, fallbackDraft.fwmarkMask),
    tableStart: toStringInt(
      config.iproute?.table_start,
      fallbackDraft.tableStart
    ),
  }
}

function buildUpdatedConfig(
  config: ConfigObject,
  draft: SettingsDraft
): ConfigObject {
  const tableStart = parseStrictDecimalToNumber(draft.tableStart)

  return {
    ...config,
    daemon: {
      ...config.daemon,
      strict_enforcement: draft.strictEnforcement,
    },
    fwmark: {
      ...config.fwmark,
      start: draft.fwmarkStart.trim(),
      mask: draft.fwmarkMask.trim(),
    },
    iproute: {
      ...config.iproute,
      table_start: toBackendIntegerValue(tableStart, draft.tableStart.trim()),
    },
    lists_autoupdate: {
      ...config.lists_autoupdate,
      enabled: draft.listsAutoupdateEnabled,
      cron: draft.cron.trim(),
    },
  }
}

function toHex32(value: string | undefined, fallback: string) {
  if (!value) {
    return fallback
  }

  const trimmed = value.trim()
  if (!/^0x[0-9a-fA-F]+$/.test(trimmed)) {
    return fallback
  }

  const normalized = trimmed.slice(2).replace(/^0+/, "") || "0"
  return `0x${normalized.padStart(8, "0")}`
}

function toStringInt(value: number | undefined, fallback: string) {
  if (!Number.isInteger(value)) {
    return fallback
  }

  return String(value)
}

function parseStrictDecimalToNumber(value: string) {
  const trimmed = value.trim()
  if (!/^\d+$/.test(trimmed)) {
    return null
  }

  return Number.parseInt(trimmed, 10)
}

function toBackendIntegerValue(parsed: number | null, raw: string): number {
  if (parsed !== null) {
    return parsed
  }

  return raw as unknown as number
}


function getCrontabGuruUrl(value: string) {
  if (getCronHash(value) === null) {
    return "https://crontab.guru/"
  }

  return `https://crontab.guru/#${getCronHash(value)}`
}

function resolveSettingsFieldPath(path: string) {
  switch (path) {
    case "daemon.strict_enforcement":
      return "strictEnforcement"
    case "lists_autoupdate.enabled":
      return "listsAutoupdateEnabled"
    case "lists_autoupdate.cron":
      return "cron"
    case "fwmark.start":
      return "fwmarkStart"
    case "fwmark.mask":
      return "fwmarkMask"
    case "iproute.table_start":
      return "tableStart"
    default:
      return undefined
  }
}

function getCronHash(value: string) {
  const trimmed = value.trim()
  if (!trimmed) {
    return null
  }

  const fields = trimmed.split(/\s+/)
  if (fields.length !== 5) {
    return null
  }

  return fields.join("_")
}
