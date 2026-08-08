import { ExternalLinkIcon } from "lucide-react"
import { useTranslation } from "react-i18next"

import { Button } from "@/components/ui/button"
import { cn } from "@/lib/utils"

const DOCUMENTATION_URL = "https://keen-pbr.fyi/docs/"

export function DocumentationLink({ className }: { className?: string }) {
  const { t } = useTranslation()

  return (
    <Button
      className={cn("w-full justify-start", className)}
      render={
        <a href={DOCUMENTATION_URL} rel="noreferrer" target="_blank" />
      }
      variant="ghost"
    >
      {t("common.documentation")}
      <ExternalLinkIcon data-icon="inline-end" />
    </Button>
  )
}
