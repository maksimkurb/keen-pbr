import { ExternalLink } from "lucide-react"
import { useTranslation } from "react-i18next"
import { Link } from "wouter"

import { Button } from "@/components/ui/button"
import { CommandCopyField } from "@/components/shared/command-copy-field"
import { ResponsiveDialog } from "@/components/shared/responsive-dialog"
import {
  dnsmasqInstallCommand,
  dnsmasqSetupDocsUrl,
  dnsmasqSetupOsLabel,
} from "@/lib/dnsmasq-setup"

/**
 * dnsmasq management is on but dnsmasq is not installed (for example opkg
 * removed it when keen-pbr 3.3.x was upgraded).  Offers the two ways out:
 * install it again, or turn management off.
 */
export function DnsmasqMissingDialog({
  open,
  onOpenChange,
  osType,
  osVersion,
}: {
  open: boolean
  onOpenChange: (open: boolean) => void
  osType?: string
  osVersion?: string
}) {
  const { t } = useTranslation()
  const command = dnsmasqInstallCommand(osType, osVersion)
  const osLabel = dnsmasqSetupOsLabel(osType)
  const docsUrl = dnsmasqSetupDocsUrl(t("common.documentationUrl"), osType)

  return (
    <ResponsiveDialog
      open={open}
      onOpenChange={onOpenChange}
      className="sm:max-w-xl"
      title={t("overview.dnsmasqMissing.title")}
      description={t("overview.dnsmasqMissing.description")}
    >
      <div className="space-y-5 text-sm">
        <section className="space-y-2">
          <h3 className="font-medium">
            {t("overview.dnsmasqMissing.install.title")}
          </h3>
          {command ? (
            <>
              <p className="text-muted-foreground">
                {t("overview.dnsmasqMissing.install.command")}
              </p>
              <CommandCopyField
                command={command}
                ariaLabel={t("overview.dnsmasqMissing.install.copyAria")}
              />
            </>
          ) : null}
          <p className="text-muted-foreground">
            {t("overview.dnsmasqMissing.install.pickup")}
          </p>
          <a
            className="inline-flex items-center gap-1 underline underline-offset-2"
            href={docsUrl}
            rel="noreferrer"
            target="_blank"
          >
            {osLabel
              ? t("overview.dnsmasqMissing.install.docsFor", { os: osLabel })
              : t("overview.dnsmasqMissing.install.docs")}
            <ExternalLink className="h-3.5 w-3.5" />
          </a>
        </section>

        <section className="space-y-2">
          <h3 className="font-medium">
            {t("overview.dnsmasqMissing.disable.title")}
          </h3>
          <p className="text-muted-foreground">
            {t("overview.dnsmasqMissing.disable.description")}
          </p>
          <Button
            className="w-full sm:w-auto"
            variant="outline"
            render={<Link href="/general#dnsmasq-management" />}
            onClick={() => onOpenChange(false)}
          >
            {t("overview.dnsmasqMissing.disable.action")}
          </Button>
        </section>
      </div>
    </ResponsiveDialog>
  )
}
