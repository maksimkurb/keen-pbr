import { useState } from "react"
import type { FormEvent } from "react"
import { useTranslation } from "react-i18next"

import { useAuth } from "@/auth/auth-context"
import { getDevicePageTitle } from "@/auth/device-name"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card"
import { Field, FieldGroup, FieldLabel } from "@/components/shared/field"
import { Input } from "@/components/ui/input"
import logoUrl from "@/assets/logo.svg"

export function LoginPage() {
  const { deviceName, login } = useAuth()
  const { t } = useTranslation()
  const [password, setPassword] = useState("")
  const [error, setError] = useState<string | null>(null)
  const [pending, setPending] = useState(false)

  const submit = async (event: FormEvent) => {
    event.preventDefault()
    setPending(true)
    setError(null)
    try { await login(password) } catch (reason) {
      setError(reason instanceof Error && reason.message === "invalid_credentials" ? t("auth.invalid") : t("auth.failed"))
    } finally { setPending(false) }
  }

  return (
    <main className="flex min-h-screen items-center justify-center bg-muted/30 p-4">
      <Card className="w-full max-w-sm shadow-lg">
        <CardHeader className="items-center text-center">
          <div className="mb-2 flex size-14 items-center justify-center overflow-hidden rounded-xl border bg-[#1A2D35] p-2"><img alt={t("brand.logoAlt")} className="size-full object-contain" src={logoUrl} /></div>
          <CardTitle>{deviceName ? getDevicePageTitle(deviceName) : t("auth.title")}</CardTitle>
          <CardDescription>{t("auth.description")}</CardDescription>
        </CardHeader>
        <CardContent>
          <form onSubmit={submit}>
            <FieldGroup>
              <Field>
                <FieldLabel htmlFor="auth-password">{t("auth.password")}</FieldLabel>
                <Input autoFocus autoComplete="current-password" id="auth-password" onChange={(e) => setPassword(e.target.value)} required type="password" value={password} />
              </Field>
              {error ? <Alert variant="destructive"><AlertDescription>{error}</AlertDescription></Alert> : null}
              <Button className="w-full" disabled={pending || !password} type="submit">{pending ? t("auth.signingIn") : t("auth.signIn")}</Button>
            </FieldGroup>
          </form>
        </CardContent>
      </Card>
    </main>
  )
}
