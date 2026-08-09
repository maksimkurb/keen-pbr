import { ExternalLinkIcon } from "lucide-react"
import { useTranslation } from "react-i18next"

import { Button } from "@/components/ui/button"
import { cn } from "@/lib/utils"

export function DocumentationLink({ className }: { className?: string }) {
  const { t } = useTranslation()

  return (
    <Button
      className={cn("w-full justify-start", className)}
      render={
        <a
          href={t("common.documentationUrl")}
          rel="noreferrer"
          target="_blank"
        />
      }
      nativeButton={false}
      variant="outline"
    >
      {t("common.documentation")}
      <ExternalLinkIcon data-icon="inline-end" />
    </Button>
  )
}
