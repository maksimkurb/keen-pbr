import { ArrowLeft, LayoutDashboard } from "lucide-react"
import { useTranslation } from "react-i18next"
import { useLocation } from "wouter"

import {
  Empty,
  EmptyDescription,
  EmptyHeader,
  EmptyTitle,
} from "@/components/ui/empty"
import { Button } from "@/components/ui/button"

export function NotFoundPage() {
  const { t } = useTranslation()
  const [locationPath, navigate] = useLocation()

  return (
    <div className="space-y-6" data-testid="not-found-page">
      <Empty
        className="min-h-[min(60vh,32rem)] justify-center border border-dashed md:min-h-[40vh]"
      >
        <EmptyHeader>
          <EmptyTitle>{t("common.notFound.title")}</EmptyTitle>
          <EmptyDescription className="max-w-lg">
            {t("common.notFound.description")}
            <span className="mt-3 block rounded-md border bg-muted/40 px-3 py-2 font-mono text-xs tracking-tight wrap-break-word text-muted-foreground">
              {locationPath || "/"}
            </span>
          </EmptyDescription>
        </EmptyHeader>
        <div className="flex w-full max-w-md flex-wrap justify-center gap-2">
          <Button
            className="gap-1.5"
            size="sm"
            type="button"
            variant="outline"
            onClick={() => {
              window.history.back()
            }}
          >
            <ArrowLeft className="size-4" />
            {t("common.notFound.goBack")}
          </Button>
          <Button
            className="gap-1.5"
            size="sm"
            type="button"
            onClick={() => {
              navigate("/")
            }}
          >
            <LayoutDashboard className="size-4" />
            {t("common.notFound.goHome")}
          </Button>
        </div>
      </Empty>
    </div>
  )
}
