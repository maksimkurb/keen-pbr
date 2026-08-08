import { useState } from "react"
import type { FormEvent } from "react"
import { LockKeyhole } from "lucide-react"
import { useTranslation } from "react-i18next"

import { useAuth } from "@/auth/auth-context"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card"
import { Field, FieldGroup, FieldLabel } from "@/components/shared/field"
import { Input } from "@/components/ui/input"

export function LoginPage() {
  const { login } = useAuth()
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
          <div className="mb-2 rounded-full bg-primary/10 p-3 text-primary"><LockKeyhole className="size-6" /></div>
          <CardTitle>{t("auth.title")}</CardTitle>
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
