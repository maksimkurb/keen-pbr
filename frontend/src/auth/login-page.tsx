import { useState } from "react"
import type { FormEvent } from "react"
import { useTranslation } from "react-i18next"

import logoUrl from "@/assets/logo.svg"
import { useAuth } from "@/auth/auth-context"
import { DocumentationLink } from "@/components/documentation-link"
import { LanguageSelector } from "@/components/language-selector"
import { ThemeSelector } from "@/components/theme-selector"
import { Field, FieldGroup, FieldLabel } from "@/components/shared/field"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"

const brandPanelClassName = "bg-[#1A2D35] text-white"

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
    try {
      await login(password)
    } catch (reason) {
      setError(
        reason instanceof Error && reason.message === "invalid_credentials"
          ? t("auth.invalid")
          : t("auth.failed")
      )
    } finally {
      setPending(false)
    }
  }

  return (
    <main className="grid min-h-svh grid-cols-1 grid-rows-[auto_minmax(0,1fr)_auto] bg-background md:h-svh md:grid-cols-[minmax(300px,38%)_minmax(0,1fr)] md:grid-rows-[minmax(0,1fr)_auto] md:overflow-hidden">
      <aside
        aria-label="keen-pbr"
        className={`${brandPanelClassName} flex min-w-0 items-center px-5 py-4 md:col-start-1 md:row-start-1 md:justify-center md:p-12`}
      >
        <div className="flex min-w-0 items-center gap-3 md:gap-4">
          <div className="flex size-10 shrink-0 items-center justify-center overflow-hidden rounded-[10px] border border-white/10 md:size-14 md:rounded-[13px]">
            <img
              alt={t("brand.logoAlt")}
              className="size-full object-contain"
              src={logoUrl}
            />
          </div>
          <div className="min-w-0">
            <p className="text-base leading-tight font-semibold tracking-tight md:text-[22px]">
              keen-pbr
            </p>
            <p
              className="mt-1 max-w-[calc(100vw-6.25rem)] truncate text-xs text-white/60 md:mt-1.5 md:max-w-65 md:text-[13px]"
              title={deviceName || undefined}
            >
              {deviceName || t("brand.tagline")}
            </p>
          </div>
        </div>
      </aside>

      <section className="flex min-w-0 items-center justify-center bg-background px-6 py-8 md:col-start-2 md:row-span-2 md:row-start-1 md:px-10 md:py-12">
        <form className="w-full max-w-90" onSubmit={submit}>
          <div className="mb-7">
            <h1 className="text-2xl leading-tight font-semibold tracking-tight md:text-[25px]">
              {t("auth.title")}
            </h1>
            <p className="mt-2 text-sm leading-6 text-muted-foreground">
              {t("auth.description")}
            </p>
          </div>

          <FieldGroup className="gap-4">
            <Field className="gap-2">
              <FieldLabel htmlFor="auth-password">
                {t("auth.password")}
              </FieldLabel>
              <Input
                aria-invalid={Boolean(error)}
                autoFocus
                autoComplete="current-password"
                className="h-10"
                id="auth-password"
                onChange={(event) => setPassword(event.target.value)}
                required
                type="password"
                value={password}
              />
            </Field>

            {error ? (
              <Alert variant="destructive">
                <AlertDescription>{error}</AlertDescription>
              </Alert>
            ) : null}

            <Button
              className="mt-0.5 h-10 w-full"
              disabled={pending || !password}
              type="submit"
            >
              {pending ? t("auth.signingIn") : t("auth.signIn")}
            </Button>
          </FieldGroup>
        </form>
      </section>

      <footer className="border-t border-border bg-background px-5 pt-4 pb-[max(1.125rem,env(safe-area-inset-bottom))] text-foreground md:col-start-1 md:row-start-2 md:border-white/10 md:bg-[#1A2D35] md:px-6 md:pt-4.5 md:pb-5.5 md:text-white">
        <DocumentationLink className="text-muted-foreground md:border-white/15 md:bg-white/5 md:text-white/65 md:hover:bg-white/10 md:hover:text-white" />

        <div className="mt-3.5 grid grid-cols-1 gap-3.5 md:grid-cols-2 md:gap-2.5 [&_[data-slot=select-trigger]]:!bg-background md:[&_[data-slot=select-trigger]]:!border-white/15 md:[&_[data-slot=select-trigger]]:!bg-white/5 md:[&_[data-slot=select-trigger]]:!text-white md:[&_[data-slot=select-trigger]_svg]:!text-white/60 [&_p]:!text-muted-foreground md:[&_p]:!text-white/55">
          <LanguageSelector />
          <ThemeSelector />
        </div>
      </footer>
    </main>
  )
}
