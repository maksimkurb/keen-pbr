import { useState } from "react"
import { useTranslation } from "react-i18next"

import type { DnsServerKeeneticOnMissing } from "@/api/generated/model/dnsServerKeeneticOnMissing"
import { useGetConfigDefaults } from "@/api/queries"
import {
  Field,
  FieldContent,
  FieldHint,
  FieldLabel,
} from "@/components/shared/field"
import {
  KeeneticDnsRefreshButton,
  KeeneticDnsSettingsLink,
  KeeneticProxyDetails,
} from "@/components/shared/keenetic-dns"
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import {
  KEENETIC_SYSTEM_POLICY,
  keeneticProxyLabel,
  useKeeneticDnsProxies,
} from "@/lib/keenetic-dns"

type KeeneticPolicyFieldsProps = {
  /** "" is the System group. */
  policy: string
  /** "" is untouched: the daemon's default applies and nothing is written. */
  onMissing: DnsServerKeeneticOnMissing | ""
  onPolicyChange: (policy: string) => void
  onOnMissingChange: (value: DnsServerKeeneticOnMissing) => void
  policyError?: string | null
  onMissingError?: string | null
}

/** DNS proxy group and "group disappears" choice of a Keenetic DNS server. */
export function KeeneticPolicyFields({
  policy,
  onMissing,
  onPolicyChange,
  onOnMissingChange,
  policyError,
  onMissingError,
}: KeeneticPolicyFieldsProps) {
  const { t } = useTranslation()
  const proxiesQuery = useKeeneticDnsProxies(true)
  const defaultsQuery = useGetConfigDefaults({
    query: { staleTime: Infinity },
  })
  const [refreshError, setRefreshError] = useState<string | undefined>()

  const defaultOnMissing: DnsServerKeeneticOnMissing =
    (defaultsQuery.data?.status === 200
      ? defaultsQuery.data.data.dns?.servers?.[0]?.keenetic_on_missing
      : undefined) ?? "block"

  const proxies = proxiesQuery.proxies
  const selectedName = policy || KEENETIC_SYSTEM_POLICY
  const selectedProxy = proxies?.find((proxy) => proxy.name === selectedName)
  // Known only after the router was read; until then nothing is "missing".
  const policyMissing = proxies !== undefined && !selectedProxy
  const error = refreshError ?? proxiesQuery.error

  const policyItems = [
    {
      value: KEENETIC_SYSTEM_POLICY,
      label: t("keeneticDns.systemGroup"),
    },
    ...(proxies ?? [])
      .filter((proxy) => proxy.name !== KEENETIC_SYSTEM_POLICY)
      .map((proxy) => ({
        value: proxy.name,
        label: keeneticProxyLabel(proxy),
      })),
    ...(policyMissing && policy !== ""
      ? [
          {
            value: policy,
            label: t("keeneticDns.notFound", { name: policy }),
          },
        ]
      : []),
  ]
  const onMissingItems = [
    { value: "block", label: t("keeneticDns.onMissing.options.block") },
    { value: "system", label: t("keeneticDns.onMissing.options.system") },
  ]

  return (
    <>
      <Field invalid={Boolean(policyError)}>
        <FieldLabel>{t("keeneticDns.policy.label")}</FieldLabel>
        <FieldContent>
          <div className="flex flex-wrap items-center gap-2">
            <Select
              items={policyItems}
              onValueChange={(value) =>
                onPolicyChange(
                  !value || value === KEENETIC_SYSTEM_POLICY ? "" : value
                )
              }
              value={selectedName}
            >
              <SelectTrigger
                aria-invalid={Boolean(policyError)}
                className="max-w-sm"
              >
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectGroup>
                  {policyItems.map((item) => (
                    <SelectItem key={item.value} value={item.value}>
                      {item.label}
                    </SelectItem>
                  ))}
                </SelectGroup>
              </SelectContent>
            </Select>
            <KeeneticDnsRefreshButton onResult={setRefreshError} />
            <KeeneticDnsSettingsLink />
          </div>
          <FieldHint
            description={t("keeneticDns.policy.hint")}
            error={policyError}
          />
          {error ? (
            <p className="text-sm text-destructive">
              {t("keeneticDns.loadError", { message: error })}
            </p>
          ) : null}
        </FieldContent>
      </Field>

      {selectedProxy ? (
        <Field>
          <FieldContent>
            <KeeneticProxyDetails
              isSystem={selectedName === KEENETIC_SYSTEM_POLICY}
              proxy={selectedProxy}
            />
          </FieldContent>
        </Field>
      ) : null}

      {policy !== "" ? (
        <Field invalid={Boolean(onMissingError)}>
          <FieldLabel>{t("keeneticDns.onMissing.label")}</FieldLabel>
          <FieldContent>
            <Select
              items={onMissingItems}
              onValueChange={(value) =>
                onOnMissingChange((value ?? defaultOnMissing) as DnsServerKeeneticOnMissing)
              }
              value={onMissing || defaultOnMissing}
            >
              <SelectTrigger
                aria-invalid={Boolean(onMissingError)}
                className="max-w-sm"
              >
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                <SelectGroup>
                  {onMissingItems.map((item) => (
                    <SelectItem key={item.value} value={item.value}>
                      {item.label}
                    </SelectItem>
                  ))}
                </SelectGroup>
              </SelectContent>
            </Select>
            <FieldHint
              description={t("keeneticDns.onMissing.hint")}
              error={onMissingError}
            />
          </FieldContent>
        </Field>
      ) : null}
    </>
  )
}
