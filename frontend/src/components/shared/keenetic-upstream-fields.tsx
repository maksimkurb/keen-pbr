import { useState } from "react"
import { useTranslation } from "react-i18next"

import type { KeeneticDnsProxyUpstream } from "@/api/generated/model/keeneticDnsProxyUpstream"
import {
  Field,
  FieldContent,
  FieldHint,
  FieldLabel,
} from "@/components/shared/field"
import {
  KeeneticDnsDomainServers,
  KeeneticDnsRefreshButton,
  KeeneticDnsSettingsLink,
} from "@/components/shared/keenetic-dns"
import { Badge } from "@/components/ui/badge"
import { Checkbox } from "@/components/ui/checkbox"
import {
  isSameKeeneticUpstream,
  useKeeneticSystemProxy,
} from "@/lib/keenetic-dns"

type KeeneticUpstreamFieldsProps = {
  /** Empty means every upstream of the Keenetic built-in DNS. */
  selected: KeeneticDnsProxyUpstream[]
  onChange: (selected: KeeneticDnsProxyUpstream[]) => void
  error?: string | null
}

/** Choice of the Keenetic built-in DNS upstreams a Keenetic DNS server uses. */
export function KeeneticUpstreamFields({
  selected,
  onChange,
  error,
}: KeeneticUpstreamFieldsProps) {
  const { t } = useTranslation()
  const { proxy, error: loadError, isLoading } = useKeeneticSystemProxy(true)
  const [refreshError, setRefreshError] = useState<string | undefined>()
  const shownError = refreshError ?? loadError

  const available = proxy?.upstreams ?? []
  // Selected upstreams the router no longer has stay listed, so they can be unchecked.
  const gone =
    proxy === undefined
      ? []
      : selected.filter(
          (item) =>
            !available.some((upstream) =>
              isSameKeeneticUpstream(upstream, item)
            )
        )
  const rows = [
    ...available.map((upstream) => ({ upstream, missing: false })),
    ...gone.map((upstream) => ({ upstream, missing: true })),
  ]
  const isSelected = (upstream: KeeneticDnsProxyUpstream) =>
    selected.some((item) => isSameKeeneticUpstream(item, upstream))

  const toggle = (upstream: KeeneticDnsProxyUpstream, checked: boolean) => {
    onChange(
      checked
        ? [...selected, upstream]
        : selected.filter((item) => !isSameKeeneticUpstream(item, upstream))
    )
  }

  return (
    <>
      <Field invalid={Boolean(error)}>
        <FieldLabel>{t("keeneticDns.upstreams.label")}</FieldLabel>
        <FieldContent>
          <div className="flex flex-wrap items-center gap-2">
            <KeeneticDnsRefreshButton onResult={setRefreshError} />
            <KeeneticDnsSettingsLink />
          </div>

          {rows.length === 0 && !isLoading ? (
            <p className="text-sm text-muted-foreground">
              {t("keeneticDns.upstreams.none")}
            </p>
          ) : (
            <ul className="space-y-1.5">
              {rows.map(({ upstream, missing }) => (
                <li key={`${upstream.protocol}-${upstream.target}`}>
                  <label className="flex flex-wrap items-center gap-2 text-sm">
                    <Checkbox
                      checked={isSelected(upstream)}
                      onCheckedChange={(checked) =>
                        toggle(upstream, checked === true)
                      }
                    />
                    <Badge variant="secondary">{upstream.protocol}</Badge>
                    <span className="min-w-0 break-all">
                      {missing
                        ? t("keeneticDns.notFound", { name: upstream.target })
                        : upstream.target}
                    </span>
                  </label>
                </li>
              ))}
            </ul>
          )}

          <FieldHint
            description={t("keeneticDns.upstreams.hint")}
            error={error}
          />
          {selected.length >= 2 ? (
            <p className="text-sm text-muted-foreground">
              {t("keeneticDns.upstreams.balanced")}
            </p>
          ) : null}
          {shownError ? (
            <p className="text-sm text-destructive">
              {t("keeneticDns.loadError", { message: shownError })}
            </p>
          ) : null}
        </FieldContent>
      </Field>

      {proxy ? (
        <Field>
          <FieldContent>
            <KeeneticDnsDomainServers proxy={proxy} />
          </FieldContent>
        </Field>
      ) : null}
    </>
  )
}
