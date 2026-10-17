import { ExternalLink, Loader2, RefreshCw } from "lucide-react"
import { useTranslation } from "react-i18next"

import type { ApiError } from "@/api/client"
import type { KeeneticDnsProxy } from "@/api/generated/model/keeneticDnsProxy"
import { usePostKeeneticDnsProxiesRefreshMutation } from "@/api/mutations"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import {
  KEENETIC_DNS_SETTINGS_PATH,
  keeneticWebUrl,
} from "@/lib/keenetic-web-url"

/**
 * Re-reads the DNS proxy groups from the router. A failed read keeps the
 * groups shown so far; `onResult` gets the reason it failed, if it did.
 */
export function KeeneticDnsRefreshButton({
  onResult,
  size = "sm",
}: {
  onResult?: (error: string | undefined) => void
  size?: "sm" | "default"
}) {
  const { t } = useTranslation()
  const refreshMutation = usePostKeeneticDnsProxiesRefreshMutation({
    mutation: {
      onSuccess: (response) => {
        onResult?.(response.status === 200 ? response.data.error : undefined)
      },
      onError: (error) => {
        onResult?.((error as Partial<ApiError>).message ?? "")
      },
    },
  })

  return (
    <Button
      disabled={refreshMutation.isPending}
      onClick={() => refreshMutation.mutate()}
      size={size}
      type="button"
      variant="outline"
    >
      {refreshMutation.isPending ? (
        <Loader2 className="mr-1 h-4 w-4 animate-spin" />
      ) : (
        <RefreshCw className="mr-1 h-4 w-4" />
      )}
      {t("keeneticDns.refresh")}
    </Button>
  )
}

/** Opens the DNS settings of the Keenetic web interface in a new tab. */
export function KeeneticDnsSettingsLink() {
  const { t } = useTranslation()

  return (
    <Button
      onClick={() =>
        window.open(
          keeneticWebUrl(KEENETIC_DNS_SETTINGS_PATH),
          "_blank",
          "noopener,noreferrer"
        )
      }
      size="sm"
      type="button"
      variant="outline"
    >
      {t("keeneticDns.openSettings")}
      <ExternalLink className="h-3.5 w-3.5 text-muted-foreground" />
    </Button>
  )
}

/**
 * Read-only details of one DNS proxy group: its upstream servers, the
 * interfaces its policy routes through (when it has encrypted upstreams) and
 * the servers it uses for single domains.
 */
export function KeeneticProxyDetails({
  proxy,
  isSystem,
}: {
  proxy: KeeneticDnsProxy
  isSystem: boolean
}) {
  const { t } = useTranslation()
  const encrypted = proxy.upstreams.some(
    (upstream) => upstream.protocol !== "Plain"
  )
  const routeInterfaces = proxy.route_interfaces ?? []
  const domainServers = proxy.domain_servers ?? []

  return (
    <div className="space-y-4">
      <div className="space-y-2">
        <p className="text-sm font-medium">{t("keeneticDns.upstreams")}</p>
        {proxy.upstreams.length === 0 ? (
          <p className="text-sm text-muted-foreground">
            {t("keeneticDns.noUpstreams")}
          </p>
        ) : (
          <ul className="space-y-1.5">
            {proxy.upstreams.map((upstream, index) => (
              <li
                className="flex flex-wrap items-center gap-2 text-sm"
                key={`${upstream.protocol}-${upstream.target}-${index}`}
              >
                <Badge variant="secondary">{upstream.protocol}</Badge>
                <span className="min-w-0 break-all">{upstream.target}</span>
              </li>
            ))}
          </ul>
        )}
        {!isSystem && encrypted && routeInterfaces.length > 0 ? (
          <p className="text-sm text-muted-foreground">
            {t("keeneticDns.via", { interfaces: routeInterfaces.join(", ") })}
          </p>
        ) : null}
      </div>

      {domainServers.length > 0 ? (
        <div className="space-y-2">
          <p className="text-sm font-medium">
            {t("keeneticDns.domainServers.title")}
          </p>
          <ul className="space-y-1">
            {domainServers.map((entry) => (
              <li className="text-sm" key={entry.domain}>
                <span className="font-medium">{entry.domain}</span>
                <span className="text-muted-foreground">
                  {" → "}
                  {entry.addresses.join(", ")}
                </span>
              </li>
            ))}
          </ul>
          <p className="text-sm text-muted-foreground">
            {t("keeneticDns.domainServers.hint")}
          </p>
        </div>
      ) : null}
    </div>
  )
}
