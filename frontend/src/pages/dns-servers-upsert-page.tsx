import { ExternalLink } from "lucide-react"
import { useMemo } from "react"
import { Trans, useTranslation } from "react-i18next"
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
  KEENETIC_DNS_SETTINGS_PATH,
  keeneticWebUrl,
} from "@/lib/keenetic-web-url"
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import i18n from "@/i18n"
import { useDraftForm } from "@/lib/draft-form"
import { PLATFORM_DEVELOPMENT, PLATFORM_KEENETIC } from "@/lib/platform"
import { getTagNameValidationError } from "@/lib/tag-name-validation"

type DnsServerDraft = {
  tag: string
  type:
    | typeof DnsServerType.static
    | typeof DnsServerType.keenetic
    | typeof DnsServerType.doh
  address: string
  detour: string
}

const emptyDnsServerDraft: DnsServerDraft = {
  tag: "",
  type: DnsServerType.static,
  address: "",
  detour: "",
}

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
  const config = selectConfig(configQuery.data)
  const dnsServers = config?.dns?.servers ?? []
  const supportsKeeneticDns =
    import.meta.env.VITE_KEEN_PBR_PLATFORM === PLATFORM_KEENETIC ||
    import.meta.env.VITE_KEEN_PBR_PLATFORM === PLATFORM_DEVELOPMENT
  // Older daemons do not report it and have no DNS-over-HTTPS.
  const healthQuery = useGetHealthService()
  const supportsDoh =
    healthQuery.data?.status === 200 &&
    healthQuery.data.data.doh_supported === true

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
        mode === "create" ? (
          t("pages.dnsServerUpsert.createTitle")
        ) : existingServer ? (
          <Trans
            i18nKey="pages.dnsServerUpsert.editCardTitle"
            values={{ tag: existingServer.tag }}
            components={{ entity: <span className="text-primary" /> }}
          />
        ) : (
          t("pages.dnsServerUpsert.editTitle")
        )
      }
    >
      <DnsServerForm
        config={config}
        initialDraft={initialDraft}
        mode={mode}
        onCancel={() => navigate("/dns-servers")}
        onSaved={() => navigate("/dns-servers")}
        serverTag={serverTag}
        supportsDoh={supportsDoh}
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
  supportsDoh,
  supportsKeeneticDns,
}: {
  mode: "create" | "edit"
  serverTag?: string
  config: ConfigObject | undefined
  initialDraft: DnsServerDraft
  onCancel: () => void
  onSaved: () => void
  supportsDoh: boolean
  supportsKeeneticDns: boolean
}) {
  const { t } = useTranslation()
  // A type the daemon cannot use is offered only while the server already has it.
  const offerKeenetic =
    supportsKeeneticDns || initialDraft.type === DnsServerType.keenetic
  const offerDoh = supportsDoh || initialDraft.type === DnsServerType.doh
  const showTypeSelector = offerKeenetic || offerDoh
  const dnsTypeSelectItems = [
    {
      value: DnsServerType.static,
      label: t("pages.dnsServerUpsert.fields.typeOptions.static"),
    },
    ...(offerKeenetic
      ? [
          {
            value: DnsServerType.keenetic,
            label: t("pages.dnsServerUpsert.fields.typeOptions.keenetic"),
          },
        ]
      : []),
    ...(offerDoh
      ? [
          {
            value: DnsServerType.doh,
            label: t("pages.dnsServerUpsert.fields.typeOptions.doh"),
          },
        ]
      : []),
  ]

  const configServers = config?.dns?.servers ?? []

  // Index the server has in the array that is sent (new ones are appended).
  const serverIndex =
    mode === "edit"
      ? configServers.findIndex((server) => server.tag === serverTag)
      : configServers.length

  const form = useDraftForm<DnsServerDraft>(initialDraft, {
    apiPrefix: `dns.servers[${serverIndex}]`,
    validate: (value) => {
      const errors: Record<string, string> = {}

      const tagError = getTagError(
        value.tag,
        configServers,
        mode === "edit" ? serverTag : undefined
      )
      if (tagError) {
        errors.tag = tagError
      }

      const typeError = getDnsTypeError(value.type)
      if (typeError) {
        errors.type = typeError
      }

      if (value.type !== DnsServerType.keenetic) {
        const addressError =
          value.type === DnsServerType.doh
            ? getDohUrlError(value.address)
            : getAddressError(value.address)
        if (addressError) {
          errors.address = addressError
        }
      }

      return errors
    },
  })
  const { values } = form

  const postConfigMutation = usePostConfigMutation()

  const save = async (value: DnsServerDraft) => {
    if (!config) {
      return
    }

    const normalizedTag = value.tag.trim()
    const isKeeneticDns = value.type === DnsServerType.keenetic
    const normalizedAddress = isKeeneticDns
      ? null
      : value.type === DnsServerType.doh
        ? normalizeDohUrl(value.address)
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

    try {
      await postConfigMutation.mutateAsync({ data: updatedConfig })
      onSaved()
    } catch (error) {
      form.setApiError(error as ApiError)
    }
  }

  const isKeeneticDns = values.type === DnsServerType.keenetic
  const isDoh = values.type === DnsServerType.doh
  // The tag control also shows errors addressed to the whole server entry.
  const tagError = form.errorFor("tag", { alsoClaims: [""] })

  return (
    <form className="space-y-6" onSubmit={form.onSubmit(save)}>
      <FieldGroup>
        <Field invalid={Boolean(tagError)}>
          <FieldLabel htmlFor="dns-server-tag">
            {t("pages.dnsServerUpsert.fields.tag")}
          </FieldLabel>
          <FieldContent>
            <Input
              aria-invalid={Boolean(tagError)}
              id="dns-server-tag"
              className="max-w-sm"
              onChange={(event) => form.setValue("tag", event.target.value)}
              readOnly={mode === "edit"}
              value={values.tag}
            />
            <FieldHint
              description={t("pages.dnsServerUpsert.fields.tagHint")}
              error={tagError}
            />
          </FieldContent>
        </Field>

        {showTypeSelector ? (
          <Field invalid={Boolean(form.errorFor("type"))}>
            <FieldLabel>{t("pages.dnsServerUpsert.fields.type")}</FieldLabel>
            <FieldContent>
              <Select
                items={dnsTypeSelectItems}
                onValueChange={(value) =>
                  form.setValue(
                    "type",
                    (value ?? DnsServerType.static) as DnsServerDraft["type"]
                  )
                }
                value={values.type}
              >
                <SelectTrigger aria-invalid={Boolean(form.errorFor("type"))}>
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectGroup>
                    {dnsTypeSelectItems.map((item) => (
                      <SelectItem key={item.value} value={item.value}>
                        {item.label}
                      </SelectItem>
                    ))}
                  </SelectGroup>
                </SelectContent>
              </Select>
              <FieldHint
                description={t("pages.dnsServerUpsert.fields.typeHint")}
                error={form.errorFor("type")}
              />
            </FieldContent>
          </Field>
        ) : null}

        {isKeeneticDns ? (
          <Field>
            <FieldContent>
              <Alert>
                <AlertDescription className="space-y-2">
                  <p className="flex flex-wrap items-center gap-2">
                    <span>
                      {t(
                        "pages.dnsServerUpsert.fields.keeneticNotice.description"
                      )}
                    </span>
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
                      {t(
                        "pages.dnsServerUpsert.fields.keeneticNotice.openLink"
                      )}
                      <ExternalLink className="h-3.5 w-3.5 text-muted-foreground" />
                    </Button>
                  </p>
                  <p>
                    {t(
                      "pages.dnsServerUpsert.fields.keeneticNotice.navigation"
                    )}
                  </p>
                  <p>
                    {t(
                      "pages.dnsServerUpsert.fields.keeneticNotice.dotDohOnly"
                    )}
                  </p>
                </AlertDescription>
              </Alert>
            </FieldContent>
          </Field>
        ) : null}

        {!isKeeneticDns ? (
          <>
            <Field invalid={Boolean(form.errorFor("address"))}>
              <FieldLabel htmlFor="dns-server-address">
                {isDoh
                  ? t("pages.dnsServerUpsert.fields.dohUrl")
                  : t("pages.dnsServerUpsert.fields.address")}
              </FieldLabel>
              <FieldContent>
                <Input
                  aria-invalid={Boolean(form.errorFor("address"))}
                  id="dns-server-address"
                  onChange={(event) =>
                    form.setValue("address", event.target.value)
                  }
                  placeholder={
                    isDoh
                      ? t("pages.dnsServerUpsert.fields.dohUrlPlaceholder")
                      : t("pages.dnsServerUpsert.fields.addressPlaceholder")
                  }
                  value={values.address}
                />
                <FieldHint
                  description={
                    isDoh
                      ? t("pages.dnsServerUpsert.fields.dohUrlHint")
                      : t("pages.dnsServerUpsert.fields.addressHint")
                  }
                  error={form.errorFor("address")}
                />
              </FieldContent>
            </Field>

            <Field>
              <FieldLabel>
                {t("pages.dnsServerUpsert.fields.detour")}
              </FieldLabel>
              <FieldContent>
                <OutboundSelect
                  allowEmpty
                  emptyLabel={t("pages.dnsServerUpsert.fields.detourEmpty")}
                  onValueChange={(value) => form.setValue("detour", value)}
                  outbounds={config?.outbounds ?? []}
                  placeholder={t(
                    "pages.routingRuleUpsert.fields.selectOutbound"
                  )}
                  value={values.detour}
                />
                <FieldHint
                  description={t("pages.dnsServerUpsert.fields.detourHint")}
                />
              </FieldContent>
            </Field>
          </>
        ) : null}
      </FieldGroup>

      {form.formError ? (
        <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription className="whitespace-pre-wrap">
            {form.formError}
          </AlertDescription>
        </Alert>
      ) : null}

      <ServerValidationAlert errors={form.unmappedErrors()} />

      <div className="flex justify-end gap-3">
        <Button onClick={onCancel} size="xl" type="button" variant="outline">
          {t("common.cancel")}
        </Button>
        <Button
          disabled={
            postConfigMutation.isPending ||
            !config ||
            (mode === "edit" && !form.isDirty) ||
            form.isSubmitting
          }
          size="xl"
          type="submit"
        >
          {mode === "create"
            ? t("pages.dnsServerUpsert.actions.create")
            : t("pages.dnsServerUpsert.actions.save")}
        </Button>
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

function getTagError(value: string, servers: DnsServer[], editingTag?: string) {
  const t = i18n.t.bind(i18n)
  const normalizedTag = value.trim()
  const duplicate = servers.some(
    (server) => server.tag === normalizedTag && server.tag !== editingTag
  )

  return (
    getTagNameValidationError(value, {
      requiredError: t("common.validation.required"),
      invalidError: t("common.validation.tagNamePattern"),
      duplicateError: duplicate
        ? t("pages.dnsServerUpsert.validation.tagUnique")
        : null,
    }) ?? undefined
  )
}

function getDnsTypeError(value: string) {
  const t = i18n.t.bind(i18n)
  if (
    value === DnsServerType.static ||
    value === DnsServerType.keenetic ||
    value === DnsServerType.doh
  ) {
    return undefined
  }

  return t("pages.dnsServerUpsert.validation.typeRequired")
}

function getAddressError(value: string) {
  const t = i18n.t.bind(i18n)
  if (!value.trim()) {
    return t("common.validation.required")
  }

  if (!normalizeDnsAddress(value)) {
    return t("pages.dnsServerUpsert.validation.addressInvalid")
  }

  return undefined
}

function getDohUrlError(value: string) {
  const t = i18n.t.bind(i18n)
  if (!value.trim()) {
    return t("common.validation.required")
  }

  if (!normalizeDohUrl(value)) {
    return t("pages.dnsServerUpsert.validation.dohUrlInvalid")
  }

  return undefined
}

/** The URL of a DNS-over-HTTPS server: https, a host, no user name. */
function normalizeDohUrl(value: string) {
  const trimmed = value.trim()
  try {
    const url = new URL(trimmed)
    if (url.protocol !== "https:" || !url.hostname || url.username) {
      return null
    }
    if (url.hash || /\s/.test(trimmed)) {
      return null
    }
    return trimmed
  } catch {
    return null
  }
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
