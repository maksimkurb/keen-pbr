/* eslint-disable react-refresh/only-export-components */
import { StrictMode } from "react"
import { createRoot } from "react-dom/client"
import { QueryClient, QueryClientProvider } from "@tanstack/react-query"
import { ReactQueryDevtools } from "@tanstack/react-query-devtools"

import "./index.css"
import "./i18n"
import { LanguageProvider } from "@/components/language-provider"
import App from "./App.tsx"
import { ThemeProvider } from "@/components/theme-provider.tsx"
import { Toaster } from "@/components/ui/sonner"
import { TooltipProvider } from "@/components/ui/tooltip"
import { StatusEventBridge } from "@/api/status-event-bridge"
import { AuthProvider, useAuth } from "@/auth/auth-context"
import { LoginPage } from "@/auth/login-page"
import { useTranslation } from "react-i18next"

function AuthenticatedRoot() {
  const auth = useAuth()
  const { t } = useTranslation()
  if (auth.loading) return <div className="flex min-h-screen items-center justify-center text-sm text-muted-foreground">{t("auth.loading")}</div>
  if (auth.enabled && !auth.authenticated) return <LoginPage />
  return <><StatusEventBridge /><App /></>
}

const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      staleTime: 30_000,
      gcTime: 5 * 60_000,
      retry: (failureCount, error) => {
        const status =
          typeof error === "object" &&
          error !== null &&
          "status" in error &&
          typeof (error as { status?: unknown }).status === "number"
            ? (error as { status: number }).status
            : null

        if (status !== null && status >= 400 && status < 500) {
          return false
        }

        return failureCount < 2
      },
    },
  },
})

const toasterBottomOffset =
  "calc(var(--warning-banner-height, 0px) + env(safe-area-inset-bottom, 0px) + 1rem)"

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <QueryClientProvider client={queryClient}>
      <TooltipProvider>
        <LanguageProvider>
          <ThemeProvider>
            <AuthProvider><AuthenticatedRoot /></AuthProvider>
            <Toaster
              offset={{ bottom: toasterBottomOffset }}
              mobileOffset={{ bottom: toasterBottomOffset }}
            />
          </ThemeProvider>
        </LanguageProvider>
      </TooltipProvider>
      {import.meta.env.DEV ? (
        <ReactQueryDevtools initialIsOpen={false} />
      ) : null}
    </QueryClientProvider>
  </StrictMode>
)
