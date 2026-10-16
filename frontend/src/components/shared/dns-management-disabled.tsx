import { useTranslation } from "react-i18next"
import { Link } from "wouter"

import { Button } from "@/components/ui/button"
import { ListPlaceholder } from "@/components/shared/list-placeholder"

export function DnsManagementDisabled() {
  const { t } = useTranslation()

  return (
    <ListPlaceholder
      title={t("pages.settings.dns.disabledTitle")}
      description={t("pages.settings.dns.disabledDescription")}
      action={
        <Button render={<Link href="/general#dnsmasq-management" />}>
          {t("pages.settings.dns.disabledAction")}
        </Button>
      }
    />
  )
}
