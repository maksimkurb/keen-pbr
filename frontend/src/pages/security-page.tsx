import { useTranslation } from "react-i18next"

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
import { useDraftForm } from "@/lib/draft-form"

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
  const passwordSet = settings.password_set
  const draft = getDraftFromSettings(settings)

  const form = useDraftForm<SecurityDraft>(draft, {
    validate: (value) => {
      const errors: Record<string, string> = {}

      if (value.authEnabled && !value.authPassword && !passwordSet) {
        errors.authPassword = t("auth.settings.passwordRequired")
      }

      if (value.authPassword && value.authPassword !== value.authConfirmation) {
        errors.authConfirmation = t("auth.settings.passwordMismatch")
      }

      const origins = value.allowedOrigins
        .split("\n")
        .map((origin) => origin.trim())
        .filter(Boolean)

      if (origins.some((origin) => !isExactHttpOrigin(origin))) {
        errors.allowedOrigins = t("auth.settings.invalidOrigin")
      }

      return errors
    },
  })
  const { values } = form

  const save = async (value: SecurityDraft) => {
    const origins = value.allowedOrigins
      .split("\n")
      .map((origin) => origin.trim())
      .filter(Boolean)

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
      form.setServerErrors({ form: t("auth.settings.updateFailed") })
    }
  }

  return (
    <form className="space-y-6" onSubmit={form.onSubmit(save)}>
      <Card>
        <CardHeader>
          <CardTitle>{t("auth.settings.title")}</CardTitle>
          <CardDescription>{t("auth.settings.description")}</CardDescription>
        </CardHeader>
        <CardContent className="space-y-5">
          <div className="flex items-center gap-3">
            <Checkbox
              checked={values.authEnabled}
              id="authentication-enabled"
              onCheckedChange={(value) =>
                form.setValue("authEnabled", value === true)
              }
            />
            <FieldLabel htmlFor="authentication-enabled">
              {t("auth.settings.enable")}
            </FieldLabel>
          </div>

          <div className="space-y-4">
            <Field invalid={Boolean(form.errorFor("authPassword"))}>
              <FieldLabel htmlFor="new-auth-password">
                {t("auth.settings.newPassword")}
              </FieldLabel>
              <Input
                aria-invalid={Boolean(form.errorFor("authPassword"))}
                autoComplete="new-password"
                id="new-auth-password"
                onChange={(event) => {
                  form.setValue("authPassword", event.target.value)
                  if (!event.target.value) {
                    form.setValue("authConfirmation", "")
                  }
                }}
                placeholder={t(
                  passwordSet
                    ? "auth.settings.passwordSetPlaceholder"
                    : "auth.settings.newPasswordPlaceholder"
                )}
                type="password"
                value={values.authPassword}
              />
              <FieldHint error={form.errorFor("authPassword")} />
            </Field>
            {values.authPassword ? (
              <Field
                className="animate-in duration-200 fade-in-0 slide-in-from-top-2 motion-reduce:animate-none"
                invalid={Boolean(form.errorFor("authConfirmation"))}
              >
                <FieldLabel htmlFor="confirm-auth-password">
                  {t("auth.settings.confirmPassword")}
                </FieldLabel>
                <FieldContent>
                  <Input
                    aria-invalid={Boolean(form.errorFor("authConfirmation"))}
                    autoComplete="new-password"
                    id="confirm-auth-password"
                    onChange={(event) =>
                      form.setValue("authConfirmation", event.target.value)
                    }
                    type="password"
                    value={values.authConfirmation}
                  />
                  <FieldHint error={form.errorFor("authConfirmation")} />
                </FieldContent>
              </Field>
            ) : null}
          </div>

          <Field invalid={Boolean(form.errorFor("allowedOrigins"))}>
            <FieldLabel htmlFor="cors-origins">
              {t("auth.settings.allowedOrigins")}
            </FieldLabel>
            <Textarea
              aria-invalid={Boolean(form.errorFor("allowedOrigins"))}
              id="cors-origins"
              onChange={(event) =>
                form.setValue("allowedOrigins", event.target.value)
              }
              placeholder={t("auth.settings.originsPlaceholder")}
              value={values.allowedOrigins}
            />
            <FieldDescription>
              {t("auth.settings.originsDescription")}
            </FieldDescription>
            <FieldHint error={form.errorFor("allowedOrigins")} />
          </Field>

          {form.errors.form ? (
            <Alert variant="destructive">
              <AlertDescription>{form.errors.form}</AlertDescription>
            </Alert>
          ) : null}
        </CardContent>
      </Card>

      <div className="flex justify-end gap-2">
        <Button
          onClick={() => form.reset(getDraftFromSettings(settings))}
          size="xl"
          type="button"
          variant="outline"
        >
          {t("common.cancel")}
        </Button>
        <Button
          disabled={
            form.isSubmitting || postAuthSettingsMutation.isPending || !form.isDirty
          }
          size="xl"
          type="submit"
        >
          {form.isSubmitting
            ? t("pages.settings.actions.saving")
            : t("pages.settings.actions.save")}
        </Button>
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
