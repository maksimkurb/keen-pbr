import { useState } from "react"
import { useTranslation } from "react-i18next"

import { useQueryClient } from "@tanstack/react-query"
import { toast } from "sonner"

import { postAuthPassword } from "@/api/generated/keen-api"
import type { ConfigObject } from "@/api/generated/model/configObject"
import { usePostConfigMutation } from "@/api/mutations"
import { queryKeys } from "@/api/query-keys"
import { useGetAuthPasswordStatus, useGetConfig } from "@/api/queries"
import { selectConfig } from "@/api/selectors"
import { Field, FieldDescription, FieldLabel } from "@/components/shared/field"
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

export function SecurityPage() {
  const { t } = useTranslation()
  const configQuery = useGetConfig()
  const loadedConfig = selectConfig(configQuery.data)

  return (
    <div className="space-y-6">
      <PageHeader
        description={t("pages.security.description")}
        title={t("pages.security.title")}
      />

      {configQuery.isLoading ? (
        <SecurityPageSkeleton />
      ) : configQuery.isError || !loadedConfig ? (
        <ListPlaceholder
          description={t("common.loadErrorDescription")}
          title={t("common.unableToLoadData")}
          variant="error"
        />
      ) : (
        <LoadedSecurityPage loadedConfig={loadedConfig} />
      )}
    </div>
  )
}

function LoadedSecurityPage({ loadedConfig }: { loadedConfig: ConfigObject }) {
  const { t } = useTranslation()
  const queryClient = useQueryClient()
  const passwordStatusQuery = useGetAuthPasswordStatus()
  const postConfigMutation = usePostConfigMutation()
  const [authEnabled, setAuthEnabled] = useState(
    loadedConfig.api?.authentication?.enabled ?? false
  )
  const [authPassword, setAuthPassword] = useState("")
  const [authConfirmation, setAuthConfirmation] = useState("")
  const [allowedOrigins, setAllowedOrigins] = useState(
    (loadedConfig.api?.cors?.allowed_origins ?? []).join("\n")
  )
  const [pending, setPending] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const passwordSet =
    passwordStatusQuery.data?.status === 200 &&
    passwordStatusQuery.data.data.password_set

  const updateAuthPassword = (value: string) => {
    setAuthPassword(value)
    if (!value) {
      setAuthConfirmation("")
    }
  }

  const save = async () => {
    setError(null)

    if (authPassword !== authConfirmation) {
      setError(t("auth.settings.passwordMismatch"))
      return
    }

    if (authEnabled && !authPassword && !passwordSet) {
      setError(t("auth.settings.passwordRequired"))
      return
    }

    const origins = allowedOrigins
      .split("\n")
      .map((value) => value.trim())
      .filter(Boolean)

    if (origins.some((origin) => !isExactHttpOrigin(origin))) {
      setError(t("auth.settings.invalidOrigin"))
      return
    }

    setPending(true)
    try {
      if (authPassword) {
        const response = await postAuthPassword({ password: authPassword })
        if (response.status !== 200) {
          throw new Error("password update failed")
        }
      }

      await postConfigMutation.mutateAsync({
        data: {
          ...loadedConfig,
          api: {
            ...loadedConfig.api,
            enabled: true,
            authentication: { enabled: authEnabled },
            cors: { allowed_origins: origins },
          },
        },
      })
      setAuthPassword("")
      setAuthConfirmation("")
      toast.success(t("auth.settings.staged"))
      await Promise.all([
        queryClient.invalidateQueries({ queryKey: queryKeys.config() }),
        passwordStatusQuery.refetch(),
      ])
    } catch {
      setError(t("auth.settings.updateFailed"))
    } finally {
      setPending(false)
    }
  }

  const cancel = () => {
    setAuthEnabled(loadedConfig.api?.authentication?.enabled ?? false)
    setAuthPassword("")
    setAuthConfirmation("")
    setAllowedOrigins(
      (loadedConfig.api?.cors?.allowed_origins ?? []).join("\n")
    )
    setError(null)
  }

  return (
    <>
      <Card>
        <CardHeader>
          <CardTitle>{t("auth.settings.title")}</CardTitle>
          <CardDescription>{t("auth.settings.description")}</CardDescription>
        </CardHeader>
        <CardContent className="space-y-5">
          <div className="flex items-center gap-3">
            <Checkbox
              checked={authEnabled}
              id="authentication-enabled"
              onCheckedChange={(value) => setAuthEnabled(value === true)}
            />
            <FieldLabel htmlFor="authentication-enabled">
              {t("auth.settings.enable")}
            </FieldLabel>
          </div>

          <div className="space-y-4">
            <Field>
              <FieldLabel htmlFor="new-auth-password">
                {t("auth.settings.newPassword")}
              </FieldLabel>
              <Input
                autoComplete="new-password"
                id="new-auth-password"
                onChange={(event) => updateAuthPassword(event.target.value)}
                placeholder={t(
                  passwordSet
                    ? "auth.settings.passwordSetPlaceholder"
                    : "auth.settings.newPasswordPlaceholder"
                )}
                type="password"
                value={authPassword}
              />
            </Field>
            {authPassword ? (
              <Field className="animate-in duration-200 fade-in-0 slide-in-from-top-2 motion-reduce:animate-none">
                <FieldLabel htmlFor="confirm-auth-password">
                  {t("auth.settings.confirmPassword")}
                </FieldLabel>
                <Input
                  autoComplete="new-password"
                  id="confirm-auth-password"
                  onChange={(event) => setAuthConfirmation(event.target.value)}
                  type="password"
                  value={authConfirmation}
                />
              </Field>
            ) : null}
          </div>

          <Field>
            <FieldLabel htmlFor="cors-origins">
              {t("auth.settings.allowedOrigins")}
            </FieldLabel>
            <Textarea
              id="cors-origins"
              onChange={(event) => setAllowedOrigins(event.target.value)}
              placeholder={t("auth.settings.originsPlaceholder")}
              value={allowedOrigins}
            />
            <FieldDescription>
              {t("auth.settings.originsDescription")}
            </FieldDescription>
          </Field>

          {error ? (
            <Alert variant="destructive">
              <AlertDescription>{error}</AlertDescription>
            </Alert>
          ) : null}
        </CardContent>
      </Card>

      <div className="flex justify-end gap-2">
        <Button disabled={pending} onClick={cancel} size="xl" variant="outline">
          {t("common.cancel")}
        </Button>
        <Button
          disabled={pending || passwordStatusQuery.isLoading}
          onClick={() => void save()}
          size="xl"
        >
          {pending
            ? t("pages.settings.actions.saving")
            : t("pages.settings.actions.save")}
        </Button>
      </div>
    </>
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
