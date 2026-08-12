import { useState } from "react"
import { useTranslation } from "react-i18next"

import { useForm } from "@tanstack/react-form"
import { useQueryClient } from "@tanstack/react-query"
import { toast } from "sonner"

import {
  getGetAuthSettingsQueryKey,
  useGetAuthSettings,
  usePostAuthSettings,
} from "@/api/generated/keen-api"
import type { AuthSettingsResponse } from "@/api/generated/model/authSettingsResponse"
import {
  Field,
  FieldContent,
  FieldDescription,
  FieldHint,
  FieldLabel,
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
import { Textarea } from "@/components/ui/textarea"

type SecurityDraft = {
  authEnabled: boolean
  authPassword: string
  authConfirmation: string
  allowedOrigins: string
}

function getDraftFromSettings(settings: AuthSettingsResponse): SecurityDraft {
  return {
    authEnabled: settings.authentication.enabled ?? false,
    authPassword: "",
    authConfirmation: "",
    allowedOrigins: (settings.cors.allowed_origins ?? []).join("\n"),
  }
}

export function SecurityPage() {
  const { t } = useTranslation()
  const settingsQuery = useGetAuthSettings()

  return (
    <div className="space-y-6">
      <PageHeader
        description={t("pages.security.description")}
        title={t("pages.security.title")}
      />

      {settingsQuery.isLoading ? (
        <SecurityPageSkeleton />
      ) : settingsQuery.isError || settingsQuery.data?.status !== 200 ? (
        <ListPlaceholder
          description={t("common.loadErrorDescription")}
          title={t("common.unableToLoadData")}
          variant="error"
        />
      ) : (
        <LoadedSecurityPage settings={settingsQuery.data.data} />
      )}
    </div>
  )
}

function LoadedSecurityPage({
  settings,
}: {
  settings: AuthSettingsResponse
}) {
  const { t } = useTranslation()
  const queryClient = useQueryClient()
  const postAuthSettingsMutation = usePostAuthSettings()
  const [error, setError] = useState<string | null>(null)
  const passwordSet = settings.password_set
  const form = useForm({
    defaultValues: getDraftFromSettings(settings),
    validators: {
      onSubmitAsync: async ({ value }) => {
        setError(null)

        if (value.authEnabled && !value.authPassword && !passwordSet) {
          setError(t("auth.settings.passwordRequired"))
          return
        }

        const origins = value.allowedOrigins
          .split("\n")
          .map((origin) => origin.trim())
          .filter(Boolean)

        if (origins.some((origin) => !isExactHttpOrigin(origin))) {
          setError(t("auth.settings.invalidOrigin"))
          return
        }

        try {
          await postAuthSettingsMutation.mutateAsync({
            data: {
              authentication: { enabled: value.authEnabled },
              cors: { allowed_origins: origins },
              ...(value.authPassword ? { password: value.authPassword } : {}),
            },
          })
          toast.success(t("auth.settings.saved"))
          await Promise.all([
            queryClient.invalidateQueries({ queryKey: getGetAuthSettingsQueryKey() }),
          ])
          form.reset(getDraftFromSettings({
            authentication: { enabled: value.authEnabled },
            cors: { allowed_origins: origins },
            password_set: Boolean(value.authPassword) || passwordSet,
          }))
        } catch {
          setError(t("auth.settings.updateFailed"))
        }
      },
    },
  })

  const cancel = () => {
    form.reset(getDraftFromSettings(settings))
    setError(null)
  }

  return (
    <form
      className="space-y-6"
      onSubmit={(event) => {
        event.preventDefault()
        void form.handleSubmit()
      }}
    >
      <Card>
        <CardHeader>
          <CardTitle>{t("auth.settings.title")}</CardTitle>
          <CardDescription>{t("auth.settings.description")}</CardDescription>
        </CardHeader>
        <CardContent className="space-y-5">
          <div className="flex items-center gap-3">
            <form.Field name="authEnabled">
              {(field) => (
                <Checkbox
                  checked={field.state.value}
                  id="authentication-enabled"
                  onCheckedChange={(value) =>
                    field.handleChange(value === true)
                  }
                />
              )}
            </form.Field>
            <FieldLabel htmlFor="authentication-enabled">
              {t("auth.settings.enable")}
            </FieldLabel>
          </div>

          <div className="space-y-4">
            <form.Field name="authPassword">
              {(field) => (
                <>
                  <Field>
                    <FieldLabel htmlFor="new-auth-password">
                      {t("auth.settings.newPassword")}
                    </FieldLabel>
                    <Input
                      autoComplete="new-password"
                      id="new-auth-password"
                      onChange={(event) => {
                        field.handleChange(event.target.value)
                        if (!event.target.value) {
                          form.setFieldValue("authConfirmation", "")
                        }
                      }}
                      placeholder={t(
                        passwordSet
                          ? "auth.settings.passwordSetPlaceholder"
                          : "auth.settings.newPasswordPlaceholder"
                      )}
                      type="password"
                      value={field.state.value}
                    />
                  </Field>
                  {field.state.value ? (
                    <form.Field
                      name="authConfirmation"
                      validators={{
                        onSubmit: ({ value }) =>
                          value === form.getFieldValue("authPassword")
                            ? undefined
                            : t("auth.settings.passwordMismatch"),
                      }}
                    >
                      {(confirmationField) => {
                        const confirmationError = getFirstFieldError(
                          confirmationField.state.meta.errors
                        )

                        return (
                          <Field
                            className="animate-in duration-200 fade-in-0 slide-in-from-top-2 motion-reduce:animate-none"
                            invalid={Boolean(confirmationError)}
                          >
                            <FieldLabel htmlFor="confirm-auth-password">
                              {t("auth.settings.confirmPassword")}
                            </FieldLabel>
                            <FieldContent>
                              <Input
                                aria-invalid={Boolean(confirmationError)}
                                autoComplete="new-password"
                                id="confirm-auth-password"
                                onBlur={confirmationField.handleBlur}
                                onChange={(event) =>
                                  confirmationField.handleChange(
                                    event.target.value
                                  )
                                }
                                type="password"
                                value={confirmationField.state.value}
                              />
                              <FieldHint error={confirmationError} />
                            </FieldContent>
                          </Field>
                        )
                      }}
                    </form.Field>
                  ) : null}
                </>
              )}
            </form.Field>
          </div>

          <form.Field name="allowedOrigins">
            {(field) => (
              <Field>
                <FieldLabel htmlFor="cors-origins">
                  {t("auth.settings.allowedOrigins")}
                </FieldLabel>
                <Textarea
                  id="cors-origins"
                  onChange={(event) => field.handleChange(event.target.value)}
                  placeholder={t("auth.settings.originsPlaceholder")}
                  value={field.state.value}
                />
                <FieldDescription>
                  {t("auth.settings.originsDescription")}
                </FieldDescription>
              </Field>
            )}
          </form.Field>

          {error ? (
            <Alert variant="destructive">
              <AlertDescription>{error}</AlertDescription>
            </Alert>
          ) : null}
        </CardContent>
      </Card>

      <div className="flex justify-end gap-2">
        <Button onClick={cancel} size="xl" type="button" variant="outline">
          {t("common.cancel")}
        </Button>
        <form.Subscribe
          selector={(state) => ({
            isPristine: state.isPristine,
            isSubmitting: state.isSubmitting,
          })}
        >
          {({ isPristine, isSubmitting }) => (
            <Button
              disabled={
                isSubmitting || postAuthSettingsMutation.isPending || isPristine
              }
              size="xl"
              type="submit"
            >
              {isSubmitting
                ? t("pages.settings.actions.saving")
                : t("pages.settings.actions.save")}
            </Button>
          )}
        </form.Subscribe>
      </div>
    </form>
  )
}

function SecurityPageSkeleton() {
  return (
    <>
      <Card>
        <CardHeader>
          <Skeleton className="h-6 w-48" />
          <Skeleton className="h-4 w-full max-w-xl" />
        </CardHeader>
        <CardContent className="space-y-5">
          <Skeleton className="h-5 w-48" />
          <Skeleton className="h-10 w-full" />
          <Skeleton className="h-24 w-full" />
        </CardContent>
      </Card>
      <div className="flex justify-end gap-2">
        <Skeleton className="h-11 w-24 rounded-xl" />
        <Skeleton className="h-11 w-24 rounded-xl" />
      </div>
    </>
  )
}

function getFirstFieldError(errors: unknown[]) {
  const error = errors.find((item) => typeof item === "string")
  return typeof error === "string" ? error : null
}

function isExactHttpOrigin(value: string) {
  try {
    const url = new URL(value)
    return (
      (url.protocol === "http:" || url.protocol === "https:") &&
      url.origin === value &&
      url.username === "" &&
      url.password === "" &&
      url.pathname === "/" &&
      url.search === "" &&
      url.hash === ""
    )
  } catch {
    return false
  }
}
