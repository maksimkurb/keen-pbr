import { FlaskConical } from "lucide-react"
import { useTranslation } from "react-i18next"

import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert"

const isDemoBuild = import.meta.env.MODE === "demo"

export function DemoModeBanner() {
  const { t } = useTranslation()

  if (!isDemoBuild) {
    return null
  }

  return (
    <Alert
      className="mb-4 border-primary/30 bg-primary/5"
      data-testid="demo-mode-banner"
    >
      <FlaskConical className="size-4" />
      <AlertTitle>{t("common.demoMode.title")}</AlertTitle>
      <AlertDescription>{t("common.demoMode.description")}</AlertDescription>
    </Alert>
  )
}
