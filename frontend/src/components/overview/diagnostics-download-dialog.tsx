import { useMemo, useState } from "react"
import { useTranslation } from "react-i18next"

import type {
  ConfigObject,
  HealthResponse,
  RoutingHealthResponse,
  RuntimeOutboundsResponse,
} from "@/api/generated/model"
import type { DnsCheckStatus } from "@/hooks/use-dns-check"
import { Button } from "@/components/ui/button"
import { Checkbox } from "@/components/ui/checkbox"
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog"
import { Label } from "@/components/ui/label"

export function DiagnosticsDownloadDialog({
  open,
  onOpenChange,
  config,
  serviceHealth,
  routingHealth,
  runtimeOutbounds,
  dnsCheckStatus,
}: {
  open: boolean
  onOpenChange: (open: boolean) => void
  config: ConfigObject
  serviceHealth: HealthResponse
  routingHealth: RoutingHealthResponse
  runtimeOutbounds: RuntimeOutboundsResponse
  dnsCheckStatus: DnsCheckStatus
}) {
  const { t } = useTranslation()
  const [hideListsContent, setHideListsContent] = useState(false)

  const diagnosticsPayload = useMemo(() => {
    const generatedAt = new Date().toISOString()

    return {
      generated_at: generatedAt,
      config: hideListsContent ? redactConfigLists(config) : config,
      service_health: serviceHealth,
      routing_health: routingHealth,
      outbounds_status: runtimeOutbounds,
      dnscheck_status: {
        status: dnsCheckStatus,
      },
    }
  }, [config, dnsCheckStatus, hideListsContent, routingHealth, runtimeOutbounds, serviceHealth])

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent>
        <DialogHeader>
          <DialogTitle>{t("overview.diagnosticsDownload.modal.title")}</DialogTitle>
          <DialogDescription>
            {t("overview.diagnosticsDownload.modal.description")}
          </DialogDescription>
        </DialogHeader>

        <div className="space-y-3 text-sm">
          <ol className="list-decimal space-y-1 pl-5 text-muted-foreground">
            <li>{t("overview.diagnosticsDownload.modal.items.config")}</li>
            <li>{t("overview.diagnosticsDownload.modal.items.serviceHealth")}</li>
            <li>{t("overview.diagnosticsDownload.modal.items.routingHealth")}</li>
            <li>{t("overview.diagnosticsDownload.modal.items.outbounds")}</li>
            <li>{t("overview.diagnosticsDownload.modal.items.names")}</li>
          </ol>

          <p className="text-sm text-muted-foreground">
            {t("overview.diagnosticsDownload.modal.trustWarning")}
          </p>

          <Label className="flex cursor-pointer items-start gap-2 text-sm">
            <Checkbox
              checked={hideListsContent}
              onCheckedChange={(checked) => setHideListsContent(Boolean(checked))}
            />
            <span>{t("overview.diagnosticsDownload.modal.hideListsOption")}</span>
          </Label>

          <Button
            className="w-full"
            onClick={() => {
              downloadDiagnosticsFile(diagnosticsPayload)
              onOpenChange(false)
            }}
          >
            {t("overview.diagnosticsDownload.modal.downloadAction")}
          </Button>
        </div>
      </DialogContent>
    </Dialog>
  )
}

function redactConfigLists(config: ConfigObject): ConfigObject {
  if (!config.lists) {
    return config
  }

  const redactedLists = Object.fromEntries(
    Object.entries(config.lists).map(([name, listConfig]) => [
      name,
      {
        ...listConfig,
        url: listConfig.url ? "<redacted>" : undefined,
        domains: listConfig.domains ? ["<redacted>"] : undefined,
        ip_cidrs: listConfig.ip_cidrs ? ["<redacted>"] : undefined,
      },
    ])
  )

  return {
    ...config,
    lists: redactedLists,
  }
}

function downloadDiagnosticsFile(payload: Record<string, unknown>) {
  const timestamp = new Date().toISOString().replace(/[:.]/g, "-")
  const filename = `keen-pbr-diagnostics-${timestamp}.json`
  const content = `${JSON.stringify(payload, null, 2)}\n`
  const blob = new Blob([content], { type: "application/json;charset=utf-8" })
  const url = URL.createObjectURL(blob)

  const anchor = document.createElement("a")
  anchor.href = url
  anchor.download = filename
  document.body.appendChild(anchor)
  anchor.click()
  document.body.removeChild(anchor)

  URL.revokeObjectURL(url)
}
