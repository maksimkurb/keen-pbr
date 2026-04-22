import { ExternalLink } from "lucide-react"
import { useEffect, useMemo, useState } from "react"
import { useTranslation } from "react-i18next"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { DnsServer } from "@/api/generated/model/dnsServer"
import { DnsServerType } from "@/api/generated/model/dnsServerType"
import { usePostConfigMutation } from "@/api/mutations"
import { useGetConfig, useGetHealthService } from "@/api/queries"
import { selectConfig } from "@/api/selectors"
import {
  Field,
  FieldContent,
  FieldGroup,
  FieldHint,
  FieldLabel,
} from "@/components/shared/field"
import { OutboundSelect } from "@/components/shared/outbound-select"
import { UpsertPage } from "@/components/shared/upsert-page"
import { ServerValidationAlert } from "@/components/shared/server-validation-alert"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import i18n from "@/i18n"
import {
  applyFormApiErrors,
  clearFormServerErrors,
} from "@/lib/form-api-errors"
import { getTagNameValidationError } from "@/lib/tag-name-validation"
import { useForm } from "@tanstack/react-form"
import { useStore } from "@tanstack/react-store"

type DnsServerDraft = {
  tag: string
  type: typeof DnsServerType.static | typeof DnsServerType.keenetic
  address: string
  detour: string
}

const emptyDnsServerDraft: DnsServerDraft = {
  tag: "",
  type: DnsServerType.static,
  address: "",
  detour: "",
}

const DNS_SERVER_FIELD_NAMES = {
  tag: "tag",
  type: "type",
  address: "address",
  detour: "detour",
} as const

type DnsServerFieldName =
  (typeof DNS_SERVER_FIELD_NAMES)[keyof typeof DNS_SERVER_FIELD_NAMES]

export function DnsServerUpsertPage({
  mode,
  serverTag,
}: {
  mode: "create" | "edit"
  serverTag?: string
}) {
  const { t } = useTranslation()
  const [, navigate] = useLocation()
  const configQuery = useGetConfig()
  const serviceHealthQuery = useGetHealthService({
    query: {
      staleTime: 60_000,
    },
  })
  const config = selectConfig(configQuery.data)
  const dnsServers = config?.dns?.servers ?? []
  const serviceHealth =
    serviceHealthQuery.data?.status === 200 ? serviceHealthQuery.data.data : undefined
  const supportsKeeneticDns = serviceHealth?.os_type === "keenetic"

  const existingServer =
    mode === "edit"
      ? dnsServers.find((server) => server.tag === serverTag)
      : undefined
  const initialDraft = useMemo(
    () => getDnsServerDraft(existingServer),
    [existingServer]
  )

  if (mode === "edit" && !existingServer && !configQuery.isLoading) {
    return (
      <UpsertPage
        cardDescription={t("pages.dnsServerUpsert.missingCardDescription")}
        cardTitle={t("pages.dnsServerUpsert.missingCardTitle")}
        description={t("pages.dnsServerUpsert.missingDescription")}
        title={t("pages.dnsServerUpsert.editTitle")}
      >
        <div className="flex justify-end">
          <Button onClick={() => navigate("/dns-servers")} variant="outline">
            {t("pages.dnsServerUpsert.back")}
          </Button>
        </div>
      </UpsertPage>
    )
  }

  return (
    <UpsertPage
      cardDescription={t("pages.dnsServerUpsert.cardDescription")}
      cardTitle={
        mode === "create"
          ? t("pages.dnsServerUpsert.createTitle")
          : t("pages.dnsServerUpsert.editCardTitle", {
              tag: existingServer?.tag ?? t("pages.dnsServerUpsert.editTitle"),
            })
      }
      description={t("pages.dnsServerUpsert.description")}
      title={
        mode === "create"
          ? t("pages.dnsServerUpsert.createTitle")
          : t("pages.dnsServerUpsert.editTitle")
      }
    >
      <DnsServerForm
        config={config}
        initialDraft={initialDraft}
        mode={mode}
        onCancel={() => navigate("/dns-servers")}
        onSaved={() => navigate("/dns-servers")}
        serverTag={serverTag}
        supportsKeeneticDns={supportsKeeneticDns}
      />
    </UpsertPage>
  )
}

function DnsServerForm({
  mode,
  serverTag,
  config,
  initialDraft,
  onCancel,
  onSaved,
  supportsKeeneticDns,
}: {
  mode: "create" | "edit"
  serverTag?: string
  config: ConfigObject | undefined
  initialDraft: DnsServerDraft
  onCancel: () => void
  onSaved: () => void
  supportsKeeneticDns: boolean
}) {
  const { t } = useTranslation()
  const [apiErrorMessage, setApiErrorMessage] = useState<string | null>(null)
  const showTypeSelector =
    supportsKeeneticDns || initialDraft.type === DnsServerType.keenetic
  const dnsTypeSelectItems = [
    {
      value: DnsServerType.static,
      label: t("pages.dnsServerUpsert.fields.typeOptions.static"),
    },
    {
      value: DnsServerType.keenetic,
      label: t("pages.dnsServerUpsert.fields.typeOptions.keenetic"),
    },
  ]
  const form = useForm({
    defaultValues: initialDraft,
    onSubmit: ({ value }) => {
      if (!config) {
        return
      }

      const normalizedTag = value.tag.trim()
      const isKeeneticDns = value.type === DnsServerType.keenetic
      const normalizedAddress = isKeeneticDns
        ? null
        : normalizeDnsAddress(value.address)
      if (!isKeeneticDns && !normalizedAddress) {
        return
      }

      const normalizedDetour = isKeeneticDns ? "" : value.detour.trim()
      const nextServer: DnsServer = {
        tag: normalizedTag,
        type: value.type,
        ...(normalizedAddress ? { address: normalizedAddress } : {}),
        ...(normalizedDetour ? { detour: normalizedDetour } : {}),
      }

      const currentServers = config.dns?.servers ?? []
      const nextServers =
        mode === "edit"
          ? currentServers.map((server) =>
              server.tag === serverTag ? nextServer : server
            )
          : [...currentServers, nextServer]

      const updatedConfig = {
        ...config,
        dns: {
          ...(config.dns ?? {}),
          servers: nextServers,
        },
      } satisfies ConfigObject

      setApiErrorMessage(null)
      clearFormServerErrors(form)
      postConfigMutation.mutate({ data: updatedConfig })
    },
  })
  const unmappedServerErrors = useStore(
    form.store,
    (state) =>
      ((state.errorMap.onServer as { unmapped?: { path: string; message: string }[] } | undefined)
        ?.unmapped ?? [])
  )

  const postConfigMutation = usePostConfigMutation({
    mutation: {
      onSuccess: () => {
        clearFormServerErrors(form)
        setApiErrorMessage(null)
        onSaved()
      },
      onError: (error) => {
        setApiErrorMessage(
          applyFormApiErrors({
            error: error as ApiError,
            fieldNames: Object.values(DNS_SERVER_FIELD_NAMES),
            form,
            resolvePath: (path) =>
              resolveDnsServerFieldPath(
                path,
                form.state.values.tag || serverTag || initialDraft.tag
              ),
          }) ?? null
        )
      },
    },
  })

  useEffect(() => {
    form.reset(initialDraft)
    clearFormServerErrors(form)
  }, [form, initialDraft])

  const configServers = config?.dns?.servers ?? []

  return (
    <form
      className="space-y-6"
      onSubmit={(event) => {
        event.preventDefault()
        event.stopPropagation()
        void form.handleSubmit()
      }}
    >
      <FieldGroup>
        <form.Field
          name={DNS_SERVER_FIELD_NAMES.tag}
          validators={{
            onChange: ({ value }) =>
              getTagError(
                value,
                configServers,
                mode === "edit" ? serverTag : undefined
              ),
          }}
        >
          {(field) => {
            const error = getFirstFieldError(field.state.meta.errors)

            return (
              <Field invalid={Boolean(error)}>
                <FieldLabel htmlFor="dns-server-tag">
                  {t("pages.dnsServerUpsert.fields.tag")}
                </FieldLabel>
                <FieldContent>
                  <Input
                    aria-invalid={Boolean(error)}
                    id="dns-server-tag"
                    onBlur={field.handleBlur}
                    onChange={(event) => field.handleChange(event.target.value)}
                    value={field.state.value}
                  />
                  <FieldHint
                    description={t("pages.dnsServerUpsert.fields.tagHint")}
                    error={error}
                  />
                </FieldContent>
              </Field>
            )
          }}
        </form.Field>

        <form.Field
          name={DNS_SERVER_FIELD_NAMES.type}
          validators={{
            onChange: ({ value }) => getDnsTypeError(value) ?? undefined,
          }}
        >
          {(field) => {
            const error = getFirstFieldError(field.state.meta.errors)

            if (!showTypeSelector) {
              return null
            }

            return (
              <Field invalid={Boolean(error)}>
                <FieldLabel>{t("pages.dnsServerUpsert.fields.type")}</FieldLabel>
                <FieldContent>
                  <Select
                    items={dnsTypeSelectItems}
                    onValueChange={(value) =>
                      field.handleChange((value ?? DnsServerType.static) as DnsServerDraft["type"])
                    }
                    value={field.state.value}
                  >
                    <SelectTrigger aria-invalid={Boolean(error)}>
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectGroup>
                        <SelectItem value={DnsServerType.static}>
                          {t("pages.dnsServerUpsert.fields.typeOptions.static")}
                        </SelectItem>
                        <SelectItem value={DnsServerType.keenetic}>
                          {t("pages.dnsServerUpsert.fields.typeOptions.keenetic")}
                        </SelectItem>
                      </SelectGroup>
                    </SelectContent>
                  </Select>
                  <FieldHint
                    description={t("pages.dnsServerUpsert.fields.typeHint")}
                    error={error}
                  />
                </FieldContent>
              </Field>
            )
          }}
        </form.Field>

        <form.Subscribe selector={(state) => state.values.type}>
          {(type) => {
            const isKeeneticDns = type === DnsServerType.keenetic

            return (
              <>
                {isKeeneticDns ? (
                  <Field>
                    <FieldContent>
                        <Alert>
                          <AlertDescription className="space-y-2">
                            <p className="flex flex-wrap items-center gap-2">
                              <span>
                                {t("pages.dnsServerUpsert.fields.keeneticNotice.description")}
                              </span>
                              <Button
                                onClick={() =>
                                  window.open(
                                    "http://my.keenetic.net/internet-filter/dns-configuration",
                                    "_blank",
                                    "noopener,noreferrer"
                                  )
                                }
                                size="sm"
                                type="button"
                                variant="outline"
                              >
                                {t("pages.dnsServerUpsert.fields.keeneticNotice.openLink")}
                                <ExternalLink className="h-3.5 w-3.5 text-muted-foreground" />
                              </Button>
                            </p>
                            <p>{t("pages.dnsServerUpsert.fields.keeneticNotice.navigation")}</p>
                            <p>{t("pages.dnsServerUpsert.fields.keeneticNotice.dotDohOnly")}</p>
                          </AlertDescription>
                        </Alert>
                      </FieldContent>
                  </Field>
                ) : null}

                <form.Field
                  name={DNS_SERVER_FIELD_NAMES.address}
                  validators={{
                    onChange: ({ value, fieldApi }) =>
                      fieldApi.form.getFieldValue("type") === DnsServerType.keenetic
                        ? undefined
                        : getAddressError(value) ?? undefined,
                  }}
                >
                  {(field) => {
                    const error = getFirstFieldError(field.state.meta.errors)

                    if (isKeeneticDns) {
                      return null
                    }

                    return (
                      <Field invalid={Boolean(error)}>
                        <FieldLabel htmlFor="dns-server-address">
                          {t("pages.dnsServerUpsert.fields.address")}
                        </FieldLabel>
                        <FieldContent>
                          <Input
                            aria-invalid={Boolean(error)}
                            id="dns-server-address"
                            onBlur={field.handleBlur}
                            onChange={(event) => field.handleChange(event.target.value)}
                            placeholder={t("pages.dnsServerUpsert.fields.addressPlaceholder")}
                            value={field.state.value}
                          />
                          <FieldHint
                            description={t("pages.dnsServerUpsert.fields.addressHint")}
                            error={error}
                          />
                        </FieldContent>
                      </Field>
                    )
                  }}
                </form.Field>

                <form.Field name={DNS_SERVER_FIELD_NAMES.detour}>
                  {(field) => {
                    if (isKeeneticDns) {
                      return null
                    }

                    return (
                      <Field>
                        <FieldLabel>{t("pages.dnsServerUpsert.fields.detour")}</FieldLabel>
                        <FieldContent>
                          <OutboundSelect
                            allowEmpty
                            emptyLabel={t("pages.dnsServerUpsert.fields.detourEmpty")}
                            onValueChange={field.handleChange}
                            outbounds={config?.outbounds ?? []}
                            placeholder={t("pages.routingRuleUpsert.fields.selectOutbound")}
                            value={field.state.value}
                          />
                          <FieldHint description={t("pages.dnsServerUpsert.fields.detourHint")} />
                        </FieldContent>
                      </Field>
                    )
                  }}
                </form.Field>
              </>
            )
          }}
        </form.Subscribe>
      </FieldGroup>

      {apiErrorMessage ? (
        <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription className="whitespace-pre-wrap">
            {apiErrorMessage}
          </AlertDescription>
        </Alert>
      ) : null}

      <ServerValidationAlert errors={unmappedServerErrors} />

      <div className="flex justify-end gap-3">
        <Button onClick={onCancel} size="xl" type="button" variant="outline">
          {t("common.cancel")}
        </Button>
        <form.Subscribe
          selector={(state) => ({
            canSubmit: state.canSubmit,
            isPristine: state.isPristine,
          })}
        >
          {({ canSubmit, isPristine }) => (
            <Button
              disabled={
                postConfigMutation.isPending ||
                !config ||
                isPristine ||
                !canSubmit
              }
              size="xl"
              type="submit"
            >
              {mode === "create"
                ? t("pages.dnsServerUpsert.actions.create")
                : t("pages.dnsServerUpsert.actions.save")}
            </Button>
          )}
        </form.Subscribe>
      </div>
    </form>
  )
}

function getDnsServerDraft(server?: DnsServer): DnsServerDraft {
  if (!server) {
    return emptyDnsServerDraft
  }

  return {
    tag: server.tag,
    type: server.type ?? DnsServerType.static,
    address: server.address ?? "",
    detour: server.detour ?? "",
  }
}

function getFirstFieldError(errors: unknown[]) {
  const error = errors.find((item) => typeof item === "string")
  return typeof error === "string" ? error : null
}

function getTagError(value: string, servers: DnsServer[], editingTag?: string) {
  const t = i18n.t.bind(i18n)
  const normalizedTag = value.trim()
  const duplicate = servers.some(
    (server) => server.tag === normalizedTag && server.tag !== editingTag
  )

  return getTagNameValidationError(value, {
    requiredError: t("pages.dnsServerUpsert.validation.tagRequired"),
    invalidError: t("common.validation.tagNamePattern"),
    duplicateError: duplicate
      ? t("pages.dnsServerUpsert.validation.tagUnique")
      : null,
  }) ?? undefined
}

function getDnsTypeError(value: string) {
  const t = i18n.t.bind(i18n)
  if (value === DnsServerType.static || value === DnsServerType.keenetic) {
    return undefined
  }

  return t("pages.dnsServerUpsert.validation.typeRequired")
}

function getAddressError(value: string) {
  const t = i18n.t.bind(i18n)
  if (!value.trim()) {
    return t("pages.dnsServerUpsert.validation.addressRequired")
  }

  if (!normalizeDnsAddress(value)) {
    return t("pages.dnsServerUpsert.validation.addressInvalid")
  }

  return undefined
}

function normalizeDnsAddress(value: string) {
  const trimmed = value.trim()
  if (!trimmed) {
    return null
  }

  const bracketedV6Match = /^\[([^\]]+)\](?::(\d+))?$/.exec(trimmed)
  if (bracketedV6Match) {
    const host = bracketedV6Match[1].trim().toLowerCase()
    const port = bracketedV6Match[2]
    if (!isLikelyIpv6(host) || !isValidPort(port)) {
      return null
    }

    return port ? `[${host}]:${port}` : host
  }

  const maybeIpv4WithPort = /^(\d+\.\d+\.\d+\.\d+)(?::(\d+))?$/.exec(trimmed)
  if (maybeIpv4WithPort) {
    const host = maybeIpv4WithPort[1]
    const port = maybeIpv4WithPort[2]
    if (!isValidIpv4(host) || !isValidPort(port)) {
      return null
    }

    return port ? `${host}:${port}` : host
  }

  if (trimmed.includes(":")) {
    const host = trimmed.toLowerCase()
    if (!isLikelyIpv6(host)) {
      return null
    }

    return host
  }

  return null
}

function isValidIpv4(value: string) {
  const octets = value.split(".")
  if (octets.length !== 4) {
    return false
  }

  return octets.every((octet) => {
    if (!/^\d+$/.test(octet)) {
      return false
    }

    const num = Number(octet)
    return num >= 0 && num <= 255
  })
}

function isLikelyIpv6(value: string) {
  if (!/^[0-9a-f:]+$/i.test(value)) {
    return false
  }

  return value.includes(":")
}

function isValidPort(value?: string) {
  if (!value) {
    return true
  }

  if (!/^\d+$/.test(value)) {
    return false
  }

  const port = Number(value)
  return port >= 1 && port <= 65535
}

function resolveDnsServerFieldPath(
  path: string,
  tag: string
): DnsServerFieldName | undefined {
  const normalizedTag = tag.trim()

  if (path === "dns.servers") {
    return DNS_SERVER_FIELD_NAMES.tag
  }

  if (path === `dns.servers.${normalizedTag}`) {
    return DNS_SERVER_FIELD_NAMES.tag
  }

  if (path === `dns.servers.${normalizedTag}.tag`) {
    return DNS_SERVER_FIELD_NAMES.tag
  }

  if (path === `dns.servers.${normalizedTag}.type`) {
    return DNS_SERVER_FIELD_NAMES.type
  }

  if (path === `dns.servers.${normalizedTag}.address`) {
    return DNS_SERVER_FIELD_NAMES.address
  }

  if (path === `dns.servers.${normalizedTag}.detour`) {
    return DNS_SERVER_FIELD_NAMES.detour
  }

  return undefined
}
