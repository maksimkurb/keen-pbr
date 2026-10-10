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

/** Names mirror the auth settings API; `password_confirmation` is UI only. */
type SecurityDraft = {
  authentication: { enabled: boolean }
  password: string
  password_confirmation: string
  cors: { allowed_origins: string }
}

function getDraftFromSettings(settings: AuthSettingsResponse): SecurityDraft {
  return {
    authentication: { enabled: settings.authentication.enabled ?? false },
    password: "",
    password_confirmation: "",
    cors: { allowed_origins: (settings.cors.allowed_origins ?? []).join("\n") },
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

      if (value.authentication.enabled && !value.password && !passwordSet) {
        errors.password = t("auth.settings.passwordRequired")
      }

      if (value.password && value.password !== value.password_confirmation) {
        errors.password_confirmation = t("auth.settings.passwordMismatch")
      }

      const origins = value.cors.allowed_origins
        .split("\n")
        .map((origin) => origin.trim())
        .filter(Boolean)

      if (origins.some((origin) => !isExactHttpOrigin(origin))) {
        errors["cors.allowed_origins"] = t("auth.settings.invalidOrigin")
      }

      return errors
    },
  })
  const { values } = form

  const save = async (value: SecurityDraft) => {
    const origins = value.cors.allowed_origins
      .split("\n")
      .map((origin) => origin.trim())
      .filter(Boolean)

    try {
      await postAuthSettingsMutation.mutateAsync({
        data: {
          authentication: { enabled: value.authentication.enabled },
          cors: { allowed_origins: origins },
          ...(value.password ? { password: value.password } : {}),
        },
      })
      toast.success(t("auth.settings.saved"))
      await Promise.all([
        queryClient.invalidateQueries({ queryKey: getGetAuthSettingsQueryKey() }),
      ])
      form.reset(getDraftFromSettings({
        authentication: { enabled: value.authentication.enabled },
        cors: { allowed_origins: origins },
        password_set: Boolean(value.password) || passwordSet,
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
              checked={values.authentication.enabled}
              id="authentication-enabled"
              onCheckedChange={(value) =>
                form.setValue("authentication.enabled", value === true)
              }
            />
            <FieldLabel htmlFor="authentication-enabled">
              {t("auth.settings.enable")}
            </FieldLabel>
          </div>

          <div className="space-y-4">
            <Field invalid={Boolean(form.errorFor("password"))}>
              <FieldLabel htmlFor="new-auth-password">
                {t("auth.settings.newPassword")}
              </FieldLabel>
              <Input
                aria-invalid={Boolean(form.errorFor("password"))}
                autoComplete="new-password"
                id="new-auth-password"
                onChange={(event) => {
                  form.setValue("password", event.target.value)
                  if (!event.target.value) {
                    form.setValue("password_confirmation", "")
                  }
                }}
                placeholder={t(
                  passwordSet
                    ? "auth.settings.passwordSetPlaceholder"
                    : "auth.settings.newPasswordPlaceholder"
                )}
                type="password"
                value={values.password}
              />
              <FieldHint error={form.errorFor("password")} />
            </Field>
            {values.password ? (
              <Field
                className="animate-in duration-200 fade-in-0 slide-in-from-top-2 motion-reduce:animate-none"
                invalid={Boolean(form.errorFor("password_confirmation"))}
              >
                <FieldLabel htmlFor="confirm-auth-password">
                  {t("auth.settings.confirmPassword")}
                </FieldLabel>
                <FieldContent>
                  <Input
                    aria-invalid={Boolean(form.errorFor("password_confirmation"))}
                    autoComplete="new-password"
                    id="confirm-auth-password"
                    onChange={(event) =>
                      form.setValue("password_confirmation", event.target.value)
                    }
                    type="password"
                    value={values.password_confirmation}
                  />
                  <FieldHint error={form.errorFor("password_confirmation")} />
                </FieldContent>
              </Field>
            ) : null}
          </div>

          <Field invalid={Boolean(form.errorFor("cors.allowed_origins"))}>
            <FieldLabel htmlFor="cors-origins">
              {t("auth.settings.allowedOrigins")}
            </FieldLabel>
            <Textarea
              aria-invalid={Boolean(form.errorFor("cors.allowed_origins"))}
              id="cors-origins"
              onChange={(event) =>
                form.setValue("cors.allowed_origins", event.target.value)
              }
              placeholder={t("auth.settings.originsPlaceholder")}
              value={values.cors.allowed_origins}
            />
            <FieldDescription>
              {t("auth.settings.originsDescription")}
            </FieldDescription>
            <FieldHint error={form.errorFor("cors.allowed_origins")} />
          </Field>

          {form.formError ? (
            <Alert variant="destructive">
              <AlertDescription>{form.formError}</AlertDescription>
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
