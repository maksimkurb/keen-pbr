import { useEffect, useState } from "react"
import { useLocation } from "wouter"

import type { ApiError } from "@/api/client"
import type { getConfigResponse } from "@/api/generated/keen-api"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { DnsServer } from "@/api/generated/model/dnsServer"
import { usePostConfigMutation } from "@/api/mutations"
import { useGetConfig } from "@/api/queries"
import {
  Field,
  FieldContent,
  FieldGroup,
  FieldHint,
  FieldLabel,
} from "@/components/shared/field"
import { UpsertPage } from "@/components/shared/upsert-page"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
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
  const [, navigate] = useLocation()
  const configQuery = useGetConfig()
  const config = getConfigData(configQuery.data)
  const dnsServers = config?.dns?.servers ?? []

  const existingServer =
    mode === "edit"
      ? dnsServers.find((server) => server.tag === serverTag)
      : undefined

  if (mode === "edit" && !existingServer && !configQuery.isLoading) {
    return (
      <UpsertPage
        cardDescription="The requested DNS server could not be found."
        cardTitle="Missing DNS server"
        description="Return to the DNS servers table and choose a valid entry."
        title="Edit DNS server"
      >
        <div className="flex justify-end">
          <Button onClick={() => navigate("/dns-servers")} variant="outline">
            Back to DNS servers
          </Button>
        </div>
      </UpsertPage>
    )
  }

  return (
    <UpsertPage
      cardDescription="Configure server address and optional detour outbound."
      cardTitle={
        mode === "create"
          ? "Create DNS server"
          : `Edit ${existingServer?.tag ?? "DNS server"}`
      }
      description="DNS servers can be referenced by DNS rules and the fallback selector."
      title={mode === "create" ? "Create DNS server" : "Edit DNS server"}
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
  const [apiErrorMessage, setApiErrorMessage] = useState<string | null>(null)
  const postConfigMutation = usePostConfigMutation({
    mutation: {
      onSuccess: () => {
        setApiErrorMessage(null)
        onSaved()
      },
      onError: (error) => {
        setApiErrorMessage(getApiErrorMessage(error as ApiError))
      },
    },
  })

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
      postConfigMutation.mutate({ data: updatedConfig })
    },
  })

  useEffect(() => {
    form.reset(initialDraft)
  }, [initialDraft, form])

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
      {apiErrorMessage ? (
        <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription>{apiErrorMessage}</AlertDescription>
        </Alert>
      ) : null}

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
                <FieldLabel htmlFor="dns-server-tag">Tag</FieldLabel>
                <FieldContent>
                  <Input
                    aria-invalid={Boolean(error)}
                    id="dns-server-tag"
                    onBlur={field.handleBlur}
                    onChange={(event) => field.handleChange(event.target.value)}
                    value={field.state.value}
                  />
                  <FieldHint
                    description="Unique server tag referenced by DNS rules and fallback."
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
                <FieldLabel htmlFor="dns-server-address">Address</FieldLabel>
                <FieldContent>
                  <Input
                    aria-invalid={Boolean(error)}
                    id="dns-server-address"
                    onBlur={field.handleBlur}
                    onChange={(event) => field.handleChange(event.target.value)}
                    placeholder="8.8.8.8, 8.8.8.8:5353, ::1, [::1]:5353"
                    value={field.state.value}
                  />
                  <FieldHint
                    description="IPv4 or IPv6 address, optionally with a port."
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
              <FieldLabel htmlFor="dns-server-detour">Detour</FieldLabel>
              <FieldContent>
                <Input
                  id="dns-server-detour"
                  onBlur={field.handleBlur}
                  onChange={(event) => field.handleChange(event.target.value)}
                  placeholder="Optional outbound tag"
                  value={field.state.value}
                />
                <FieldHint description="Optional outbound tag used when querying this DNS server." />
              </FieldContent>
            </Field>
          )}
        </form.Field>
      </FieldGroup>

      <div className="flex justify-end gap-3">
        <Button onClick={onCancel} size="xl" type="button" variant="outline">
          Cancel
        </Button>
        <Button size="xl" type="submit">
          {mode === "create" ? "Create DNS server" : "Save DNS server"}
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
    address: server.address,
    detour: server.detour ?? "",
  }
}

function getFirstFieldError(errors: unknown[]) {
  const error = errors.find((item) => typeof item === "string")
  return typeof error === "string" ? error : null
}

function getTagError(value: string, servers: DnsServer[], editingTag?: string) {
  const normalizedTag = value.trim()
  if (!normalizedTag) {
    return "Tag is required."
  }

  const duplicate = servers.some(
    (server) => server.tag === normalizedTag && server.tag !== editingTag
  )
  if (duplicate) {
    return "Tag must be unique."
  }

  return undefined
}

function getAddressError(value: string) {
  if (!value.trim()) {
    return "Address is required."
  }

  if (!normalizeDnsAddress(value)) {
    return "Address must be a valid IPv4/IPv6 value with an optional port."
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

function getConfigData(response: getConfigResponse | undefined) {
  if (!response || response.status !== 200) {
    return undefined
  }

  return response.data
}

function getApiErrorMessage(error: ApiError | null) {
  if (!error) {
    return null
  }

  return error.message
}
