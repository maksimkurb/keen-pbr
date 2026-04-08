import { useEffect, useState } from "react"
import { useTranslation } from "react-i18next"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { DnsServer } from "@/api/generated/model/dnsServer"
import { usePostConfigMutation } from "@/api/mutations"
import { useGetConfig } from "@/api/queries"
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
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import i18n from "@/i18n"
import {
  applyFormApiErrors,
  clearFormServerErrors,
} from "@/lib/form-api-errors"
import { useForm } from "@tanstack/react-form"

type DnsServerDraft = {
  tag: string
  address: string
  detour: string
}

const emptyDnsServerDraft: DnsServerDraft = {
  tag: "",
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

  const existingServer =
    mode === "edit"
      ? dnsServers.find((server) => server.tag === serverTag)
      : undefined

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
        initialDraft={getDnsServerDraft(existingServer)}
        mode={mode}
        onCancel={() => navigate("/dns-servers")}
        onSaved={() => navigate("/dns-servers")}
        serverTag={serverTag}
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
}: {
  mode: "create" | "edit"
  serverTag?: string
  config: ConfigObject | undefined
  initialDraft: DnsServerDraft
  onCancel: () => void
  onSaved: () => void
}) {
  const { t } = useTranslation()
  const [apiErrorMessage, setApiErrorMessage] = useState<string | null>(null)
  const form = useForm({
    defaultValues: initialDraft,
    onSubmit: ({ value }) => {
      if (!config) {
        return
      }

      const normalizedTag = value.tag.trim()
      const normalizedAddress = normalizeDnsAddress(value.address)
      if (!normalizedAddress) {
        return
      }

      const normalizedDetour = value.detour.trim()
      const nextServer: DnsServer = {
        tag: normalizedTag,
        address: normalizedAddress,
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
  }, [form, initialDraft.address, initialDraft.detour, initialDraft.tag])

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
          name="tag"
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
          name="address"
          validators={{
            onChange: ({ value }) => getAddressError(value) ?? undefined,
          }}
        >
          {(field) => {
            const error = getFirstFieldError(field.state.meta.errors)

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

        <form.Field name="detour">
          {(field) => (
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
          )}
        </form.Field>
      </FieldGroup>

      {apiErrorMessage ? (
        <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription className="whitespace-pre-wrap">
            {apiErrorMessage}
          </AlertDescription>
        </Alert>
      ) : null}

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
  if (!normalizedTag) {
    return t("pages.dnsServerUpsert.validation.tagRequired")
  }

  const duplicate = servers.some(
    (server) => server.tag === normalizedTag && server.tag !== editingTag
  )
  if (duplicate) {
    return t("pages.dnsServerUpsert.validation.tagUnique")
  }

  return undefined
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

function resolveDnsServerFieldPath(path: string, tag: string) {
  const normalizedTag = tag.trim()

  if (path === "dns.servers") {
    return "tag"
  }

  if (path === `dns.servers.${normalizedTag}`) {
    return "tag"
  }

  if (path === `dns.servers.${normalizedTag}.tag`) {
    return "tag"
  }

  if (path === `dns.servers.${normalizedTag}.address`) {
    return "address"
  }

  if (path === `dns.servers.${normalizedTag}.detour`) {
    return "detour"
  }

  return undefined
}
