import { useEffect, useState } from "react"

import { useForm } from "@tanstack/react-form"
import { useQueryClient } from "@tanstack/react-query"

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
import { getApiErrorMessage } from "@/lib/api-errors"
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
  const queryClient = useQueryClient()
  const configQuery = useGetConfig()

  const [savedDraft, setSavedDraft] = useState<SettingsDraft>(fallbackDraft)
  const [saveSuccessMessage, setSaveSuccessMessage] = useState<string | null>(
    null
  )
  const [mutationErrorMessage, setMutationErrorMessage] = useState<
    string | null
  >(null)

  const loadedConfig = selectConfig(configQuery.data)

  const postConfigMutation = usePostConfigMutation({
    mutation: {
      onSuccess: async (_response, variables) => {
        setSaveSuccessMessage("Settings staged. Apply config to persist them.")
        setMutationErrorMessage(null)
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
        setMutationErrorMessage(
          applyFormApiErrors({
            error: apiError,
            form,
            resolvePath: resolveSettingsFieldPath,
          }) ?? null
        )
      },
    },
  })

  const form = useForm({
    defaultValues: fallbackDraft,
    onSubmit: ({ value }) => {
      if (!loadedConfig) {
        return
      }

      const updatedConfig = buildUpdatedConfig(loadedConfig, value)
      setSaveSuccessMessage(null)
      setMutationErrorMessage(null)
      clearFormServerErrors(form)
      postConfigMutation.mutate({ data: updatedConfig })
    },
  })

  useEffect(() => {
    if (!loadedConfig) {
      return
    }

    const nextDraft = getDraftFromConfig(loadedConfig)
    setSavedDraft(nextDraft)
    form.reset(nextDraft)
    clearFormServerErrors(form)
  }, [loadedConfig, form])

  const isPending = postConfigMutation.isPending

  const handleCancel = () => {
    form.reset(savedDraft)
    clearFormServerErrors(form)
    setMutationErrorMessage(null)
    setSaveSuccessMessage(null)
  }

  return (
    <div className="space-y-6">
      <PageHeader
        description="Daemon defaults, global list refresh, and advanced routing values."
        title="Settings"
      />

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
          <CardTitle>General</CardTitle>
          <CardDescription>
            Daemon defaults for routing behavior.
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
                        Global strict enforcement
                      </FieldLabel>
                    </div>
                    <FieldHint description="Kill-switch for interface outbounds. When enabled and an outbound interface goes down, traffic matching its rules is blocked instead of falling back to the default routing table. This value can be overridden per outbound." />
                  </FieldContent>
                </Field>
              )}
            </form.Field>
          </FieldGroup>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Lists autoupdate</CardTitle>
          <CardDescription>
            Automatic refresh schedule for remote lists.
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
                        Enable lists autoupdate
                      </FieldLabel>
                    </div>
                    <FieldHint description="Refreshes remote lists on the schedule below and reapplies routing when relevant data changes." />
                  </FieldContent>
                </Field>
              )}
            </form.Field>

            <FieldSeparator />

            <form.Field
              name="cron"
              validators={{
                onChange: ({ value }) => getCronError(value) ?? undefined,
              }}
            >
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)

                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor="general-cron">Cron</FieldLabel>
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
                            Cron schedule used for refreshing remote lists. You
                            can build and verify an expression with{" "}
                            <a
                              className="underline underline-offset-3 hover:text-foreground"
                              href={getCrontabGuruUrl(field.state.value)}
                              rel="noreferrer"
                              target="_blank"
                            >
                              Crontab Guru
                            </a>
                            .
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
                                Open in Crontab Guru
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
          <CardTitle>Advanced routing settings</CardTitle>
          <CardDescription>
            Do not change these values unless you know exactly what they do.
          </CardDescription>
        </CardHeader>
        <CardContent>
          <FieldGroup>
            <form.Field
              name="fwmarkStart"
              validators={{
                onChange: ({ value }) =>
                  getFwmarkStartError(value) ?? undefined,
              }}
            >
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)

                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor="fwmark-start">
                      Firewall mark starting value
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
                        description="Used as the firewall mark for the first outbound, and each next outbound increases it by one step within the mask range. Example: 0x00010000."
                        error={error ?? null}
                      />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>

            <FieldSeparator />

            <form.Field
              name="fwmarkMask"
              validators={{
                onChange: ({ value }) => getFwmarkMaskError(value) ?? undefined,
              }}
            >
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)

                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor="fwmark-mask">
                      Firewall mark mask
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
                            Hex only. Must contain one consecutive run of{" "}
                            <code>f</code> digits, e.g. <code>0x00ff0000</code>.
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

            <form.Field
              name="tableStart"
              validators={{
                onChange: ({ value }) => getTableStartError(value) ?? undefined,
              }}
            >
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)

                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor="table-start">
                      IP routing table starting value
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
                        description="Used as the routing table number for the first outbound, and each next outbound increases it by one."
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
              disabled={isPending || !loadedConfig || !canSubmit || isPristine}
              onClick={() => form.handleSubmit()}
              size="xl"
            >
              {isPending ? "Saving..." : "Save"}
            </Button>
          )}
        </form.Subscribe>
      </div>
    </div>
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
  return {
    ...config,
    daemon: {
      ...config.daemon,
      strict_enforcement: draft.strictEnforcement,
    },
    fwmark: {
      ...config.fwmark,
      start: Number.parseInt(draft.fwmarkStart.slice(2), 16),
      mask: Number.parseInt(draft.fwmarkMask.slice(2), 16),
    },
    iproute: {
      ...config.iproute,
      table_start: Number.parseInt(draft.tableStart, 10),
    },
    lists_autoupdate: {
      ...config.lists_autoupdate,
      enabled: draft.listsAutoupdateEnabled,
      cron: draft.cron.trim(),
    },
  }
}

function toHex32(value: number | undefined, fallback: string) {
  if (!Number.isInteger(value)) {
    return fallback
  }

  const normalized = ((value ?? 0) >>> 0).toString(16)
  return `0x${normalized.padStart(8, "0")}`
}

function toStringInt(value: number | undefined, fallback: string) {
  if (!Number.isInteger(value)) {
    return fallback
  }

  return String(value)
}


function getCronError(value: string) {
  const fields = value.trim().split(/\s+/)
  if (fields.length !== 5) {
    return "Cron must have exactly 5 fields."
  }

  const ranges: Array<[number, number]> = [
    [0, 59],
    [0, 23],
    [1, 31],
    [1, 12],
    [0, 7],
  ]

  for (const [index, field] of fields.entries()) {
    if (!isValidCronField(field, ranges[index][0], ranges[index][1])) {
      return "Enter a valid 5-field cron expression."
    }
  }

  return null
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

function isValidCronField(field: string, min: number, max: number) {
  return field.split(",").every((part) => isValidCronPart(part, min, max))
}

function isValidCronPart(part: string, min: number, max: number) {
  const stepParts = part.split("/")
  if (stepParts.length > 2) {
    return false
  }

  const [base, step] = stepParts
  if (step && !isValidNumber(step, 1, max)) {
    return false
  }

  if (base === "*") {
    return true
  }

  const rangeParts = base.split("-")
  if (rangeParts.length === 2) {
    const [start, end] = rangeParts
    return (
      isValidNumber(start, min, max) &&
      isValidNumber(end, min, max) &&
      Number(start) <= Number(end)
    )
  }

  if (rangeParts.length === 1) {
    return isValidNumber(base, min, max)
  }

  return false
}

function isValidNumber(value: string, min: number, max: number) {
  if (!/^\d+$/.test(value)) {
    return false
  }

  const numericValue = Number(value)
  return numericValue >= min && numericValue <= max
}

function getFwmarkStartError(value: string) {
  return isValidHex32(value)
    ? null
    : "fwmark.start must be a 32-bit hexadecimal value like 0x00010000."
}

function getFwmarkMaskError(value: string) {
  if (!isValidHex32(value)) {
    return "fwmark.mask must be a 32-bit hexadecimal value."
  }

  const normalizedValue = value.slice(2).toLowerCase()
  if (!/0*f+0*/.test(normalizedValue) || /[^0f]/.test(normalizedValue)) {
    return "fwmark.mask must contain one consecutive run of f digits."
  }

  return null
}

function isValidHex32(value: string) {
  return /^0x[0-9a-fA-F]{8}$/.test(value)
}

function getTableStartError(value: string) {
  if (!/^\d+$/.test(value)) {
    return "iproute.table_start must be a positive integer."
  }

  const numericValue = Number(value)
  if (
    !Number.isInteger(numericValue) ||
    numericValue < 1 ||
    numericValue > 252
  ) {
    return "iproute.table_start must be between 1 and 252."
  }

  return null
}
