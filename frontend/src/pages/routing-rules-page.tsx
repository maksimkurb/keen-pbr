import { ArrowDown, ArrowUp, Pencil, Plus, Trash2 } from "lucide-react"
import { useMemo, useState } from "react"

import type { ApiError } from "@/api/client"
import type { ConfigObject } from "@/api/generated/model/configObject"
import type { RouteRule } from "@/api/generated/model/routeRule"
import { usePostConfigMutation } from "@/api/mutations"
import { useGetConfig } from "@/api/queries"
import { ActionButtons } from "@/components/shared/action-buttons"
import {
  Field,
  FieldContent,
  FieldGroup,
  FieldHint,
  FieldLabel,
} from "@/components/shared/field"
import { MultiSelectList } from "@/components/shared/multi-select-list"
import { PageHeader } from "@/components/shared/page-header"
import { Alert, AlertDescription } from "@/components/ui/alert"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card"
import { Input } from "@/components/ui/input"
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectLabel,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import { useForm } from "@tanstack/react-form"

type RouteRuleDraft = {
  list: string[]
  outbound: string
  proto: string
  src_port: string
  dest_port: string
  src_addr: string
  dest_addr: string
}

const protoOptions = ["", "tcp", "udp", "tcp/udp"] as const

const emptyDraft: RouteRuleDraft = {
  list: [],
  outbound: "",
  proto: "",
  src_port: "",
  dest_port: "",
  src_addr: "",
  dest_addr: "",
}

export function RoutingRulesPage() {
  const [editingIndex, setEditingIndex] = useState<number | null>(null)
  const [saveSuccessMessage, setSaveSuccessMessage] = useState<string | null>(null)
  const [mutationErrorMessage, setMutationErrorMessage] = useState<string | null>(null)

  const configQuery = useGetConfig()
  const loadedConfig = configQuery.data?.data

  const listOptions = useMemo(
    () => Object.keys(loadedConfig?.lists ?? {}).sort((left, right) => left.localeCompare(right)),
    [loadedConfig]
  )
  const outboundOptions = useMemo(
    () =>
      (loadedConfig?.outbounds ?? [])
        .map((outbound) => outbound.tag)
        .filter((tag): tag is string => Boolean(tag))
        .sort((left, right) => left.localeCompare(right)),
    [loadedConfig]
  )

  const routeRules = loadedConfig?.route?.rules ?? []

  const postConfigMutation = usePostConfigMutation({
    mutation: {
      onSuccess: () => {
        setSaveSuccessMessage("Routing rules saved.")
        setMutationErrorMessage(null)
      },
      onError: (error) => {
        const apiError = error as ApiError
        setSaveSuccessMessage(null)
        setMutationErrorMessage(getApiErrorMessage(apiError))
      },
    },
  })

  const form = useForm({
    defaultValues: emptyDraft,
    onSubmit: ({ value }) => {
      if (!loadedConfig) {
        return
      }

      const nextRule = normalizeDraft(value)
      const nextRules =
        editingIndex === null
          ? [...routeRules, nextRule]
          : routeRules.map((rule, index) => (index === editingIndex ? nextRule : rule))

      persistRules(loadedConfig, nextRules, {
        onSuccess: () => {
          resetToCreateMode()
        },
      })
    },
  })

  const isPending = postConfigMutation.isPending

  const persistRules = (
    config: ConfigObject,
    nextRules: RouteRule[],
    options?: { onSuccess?: () => void }
  ) => {
    setSaveSuccessMessage(null)
    setMutationErrorMessage(null)

    postConfigMutation.mutate({
      data: {
        ...config,
        route: {
          ...config.route,
          rules: nextRules,
        },
      },
    }, {
      onSuccess: () => {
        options?.onSuccess?.()
      },
    })
  }

  const resetToCreateMode = () => {
    setEditingIndex(null)
    form.reset(emptyDraft)
  }

  const handleEdit = (index: number) => {
    const rule = routeRules[index]
    if (!rule) {
      return
    }

    setEditingIndex(index)
    form.reset(toDraft(rule))
    setMutationErrorMessage(null)
    setSaveSuccessMessage(null)
  }

  const handleDelete = (index: number) => {
    if (!loadedConfig) {
      return
    }

    const nextRules = routeRules.filter((_, ruleIndex) => ruleIndex !== index)
    persistRules(loadedConfig, nextRules)

    if (editingIndex === index) {
      resetToCreateMode()
      return
    }

    if (editingIndex !== null && editingIndex > index) {
      setEditingIndex(editingIndex - 1)
    }
  }

  const handleMove = (index: number, direction: -1 | 1) => {
    if (!loadedConfig) {
      return
    }

    const targetIndex = index + direction
    if (targetIndex < 0 || targetIndex >= routeRules.length) {
      return
    }

    const nextRules = reorderRules(routeRules, index, targetIndex)
    persistRules(loadedConfig, nextRules)

    if (editingIndex === index) {
      setEditingIndex(targetIndex)
    } else if (editingIndex === targetIndex) {
      setEditingIndex(index)
    }
  }

  return (
    <div className="space-y-6">
      <PageHeader
        description="Manage route rule order and field-level match criteria."
        title="Routing rules"
      />

      {saveSuccessMessage ? (
        <Alert className="border-success/30 bg-success/5 text-success">
          <AlertDescription>{saveSuccessMessage}</AlertDescription>
        </Alert>
      ) : null}

      {mutationErrorMessage ? (
        <Alert className="border-destructive/30 bg-destructive/5 text-destructive">
          <AlertDescription>{mutationErrorMessage}</AlertDescription>
        </Alert>
      ) : null}

      <Card>
        <CardHeader>
          <CardTitle>Rule order</CardTitle>
          <CardDescription>
            Current `route.rules` sequence. Use actions to reorder, edit, or delete rules.
          </CardDescription>
        </CardHeader>
        <CardContent className="space-y-3">
          {routeRules.length === 0 ? (
            <p className="text-sm text-muted-foreground">No routing rules configured.</p>
          ) : (
            routeRules.map((rule, index) => (
              <div className="rounded-md border p-3" key={`rule-row-${index}`}>
                <div className="flex items-start justify-between gap-2">
                  <div className="space-y-2">
                    <div className="text-sm font-medium">Rule #{index + 1}</div>
                    <div className="flex flex-wrap gap-2">
                      {rule.list.map((listName) => (
                        <Badge key={`${index}-${listName}`} variant="outline">
                          {listName}
                        </Badge>
                      ))}
                      <Badge>{rule.outbound}</Badge>
                      <Badge variant="secondary">proto: {rule.proto || "any"}</Badge>
                    </div>
                    <p className="text-sm text-muted-foreground">{getRuleDetails(rule)}</p>
                  </div>
                  <ActionButtons
                    actions={[
                      {
                        icon: <ArrowUp className="h-4 w-4" />,
                        label: "Move up",
                        onClick: () => handleMove(index, -1),
                      },
                      {
                        icon: <ArrowDown className="h-4 w-4" />,
                        label: "Move down",
                        onClick: () => handleMove(index, 1),
                      },
                      {
                        icon: <Pencil className="h-4 w-4" />,
                        label: "Edit",
                        onClick: () => handleEdit(index),
                      },
                      {
                        icon: <Trash2 className="h-4 w-4" />,
                        label: "Delete",
                        onClick: () => handleDelete(index),
                      },
                    ]}
                  />
                </div>
              </div>
            ))
          )}
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>{editingIndex === null ? "Create route rule" : `Edit rule #${editingIndex + 1}`}</CardTitle>
          <CardDescription>
            Choose lists and outbound, then optionally narrow by proto, ports, and address matches.
          </CardDescription>
        </CardHeader>
        <CardContent className="space-y-6">
          <FieldGroup>
            <form.Field
              name="list"
              validators={{
                onChange: ({ value }) =>
                  value.length > 0 ? undefined : "Select at least one list.",
              }}
            >
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)

                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel>Lists</FieldLabel>
                    <FieldContent>
                      <MultiSelectList
                        onChange={field.handleChange}
                        options={listOptions}
                        placeholderDescription="Add one or more configured list names to match for this rule."
                        placeholderTitle="No lists selected"
                        value={field.state.value}
                      />
                      <FieldHint description="List names are sourced from config.lists keys." error={error} />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>

            <form.Field name="proto">
              {(field) => (
                <Field>
                  <FieldLabel>Proto</FieldLabel>
                  <FieldContent>
                    <Select onValueChange={(value) => field.handleChange(value ?? "")} value={field.state.value}>
                      <SelectTrigger>
                        <SelectValue placeholder="Any" />
                      </SelectTrigger>
                      <SelectContent>
                        <SelectGroup>
                          <SelectLabel>Protocol</SelectLabel>
                          {protoOptions.map((option) => (
                            <SelectItem key={option || "any"} value={option}>
                              {option || "any"}
                            </SelectItem>
                          ))}
                        </SelectGroup>
                      </SelectContent>
                    </Select>
                    <FieldHint description='Use "any" by leaving proto empty.' />
                  </FieldContent>
                </Field>
              )}
            </form.Field>

            <form.Field
              name="src_port"
              validators={{
                onChange: ({ value }) => getPortSpecError(value) ?? undefined,
              }}
            >
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)

                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor="routing-src-port">Source port</FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id="routing-src-port"
                        onBlur={field.handleBlur}
                        onChange={(event) => field.handleChange(event.target.value)}
                        placeholder="80,443 or 10000-20000"
                        value={field.state.value}
                      />
                      <FieldHint description="Comma-separated ports or ranges. Optional leading ! to negate." error={error} />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>

            <form.Field
              name="dest_port"
              validators={{
                onChange: ({ value }) => getPortSpecError(value) ?? undefined,
              }}
            >
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)

                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor="routing-dest-port">Destination port</FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id="routing-dest-port"
                        onBlur={field.handleBlur}
                        onChange={(event) => field.handleChange(event.target.value)}
                        placeholder="443 or !53,123"
                        value={field.state.value}
                      />
                      <FieldHint description="Comma-separated ports or ranges. Optional leading ! to negate." error={error} />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>

            <form.Field
              name="src_addr"
              validators={{
                onChange: ({ value }) => getAddressSpecError(value) ?? undefined,
              }}
            >
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)

                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor="routing-src-addr">Source addresses</FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id="routing-src-addr"
                        onBlur={field.handleBlur}
                        onChange={(event) => field.handleChange(event.target.value)}
                        placeholder="192.168.1.10,10.0.0.0/8"
                        value={field.state.value}
                      />
                      <FieldHint
                        description="Comma-separated IP addresses or CIDRs. Optional leading ! negates the entire spec."
                        error={error}
                      />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>

            <form.Field
              name="dest_addr"
              validators={{
                onChange: ({ value }) => getAddressSpecError(value) ?? undefined,
              }}
            >
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)

                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel htmlFor="routing-dest-addr">Destination addresses</FieldLabel>
                    <FieldContent>
                      <Input
                        aria-invalid={Boolean(error)}
                        id="routing-dest-addr"
                        onBlur={field.handleBlur}
                        onChange={(event) => field.handleChange(event.target.value)}
                        placeholder="2001:db8::1 or !203.0.113.0/24"
                        value={field.state.value}
                      />
                      <FieldHint
                        description="Comma-separated IP addresses or CIDRs. Optional leading ! negates the entire spec."
                        error={error}
                      />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>

            <form.Field
              name="outbound"
              validators={{
                onChange: ({ value }) =>
                  value.trim().length > 0 ? undefined : "Outbound tag is required.",
              }}
            >
              {(field) => {
                const error = getFirstFieldError(field.state.meta.errors)

                return (
                  <Field invalid={Boolean(error)}>
                    <FieldLabel>Outbound</FieldLabel>
                    <FieldContent>
                      <Select onValueChange={(value) => field.handleChange(value ?? "")} value={field.state.value}>
                        <SelectTrigger aria-invalid={Boolean(error)}>
                          <SelectValue placeholder="Select outbound" />
                        </SelectTrigger>
                        <SelectContent>
                          <SelectGroup>
                            <SelectLabel>Configured outbounds</SelectLabel>
                            {outboundOptions.map((option) => (
                              <SelectItem key={option} value={option}>
                                {option}
                              </SelectItem>
                            ))}
                          </SelectGroup>
                        </SelectContent>
                      </Select>
                      <FieldHint description="Outbound tags are sourced from config.outbounds." error={error} />
                    </FieldContent>
                  </Field>
                )
              }}
            </form.Field>
          </FieldGroup>

          <div className="flex justify-end gap-3">
            <Button disabled={isPending} onClick={resetToCreateMode} type="button" variant="outline">
              {editingIndex === null ? "Clear" : "Cancel edit"}
            </Button>
            <form.Subscribe
              selector={(state) => ({
                canSubmit: state.canSubmit,
              })}
            >
              {({ canSubmit }) => (
                <Button
                  disabled={isPending || !loadedConfig || !canSubmit}
                  onClick={() => form.handleSubmit()}
                  type="button"
                >
                  {editingIndex === null ? (
                    <>
                      <Plus className="h-4 w-4" />
                      Add rule
                    </>
                  ) : (
                    "Save rule"
                  )}
                </Button>
              )}
            </form.Subscribe>
          </div>
        </CardContent>
      </Card>

    </div>
  )
}

function getRuleDetails(rule: RouteRule) {
  const pieces = [
    `src_addr: ${rule.src_addr || "-"}`,
    `dest_addr: ${rule.dest_addr || "-"}`,
    `src_port: ${rule.src_port || "-"}`,
    `dest_port: ${rule.dest_port || "-"}`,
  ]

  return pieces.join(" · ")
}

function toDraft(rule: RouteRule): RouteRuleDraft {
  return {
    list: rule.list,
    outbound: rule.outbound,
    proto: rule.proto ?? "",
    src_port: rule.src_port ?? "",
    dest_port: rule.dest_port ?? "",
    src_addr: rule.src_addr ?? "",
    dest_addr: rule.dest_addr ?? "",
  }
}

function normalizeDraft(draft: RouteRuleDraft): RouteRule {
  return {
    list: draft.list,
    outbound: draft.outbound,
    proto: trimToUndefined(draft.proto),
    src_port: trimToUndefined(draft.src_port),
    dest_port: trimToUndefined(draft.dest_port),
    src_addr: trimToUndefined(draft.src_addr),
    dest_addr: trimToUndefined(draft.dest_addr),
  }
}

function trimToUndefined(value: string) {
  const trimmed = value.trim()
  return trimmed.length > 0 ? trimmed : undefined
}

function reorderRules(rules: RouteRule[], fromIndex: number, toIndex: number) {
  const nextRules = [...rules]
  const [movedRule] = nextRules.splice(fromIndex, 1)
  nextRules.splice(toIndex, 0, movedRule)
  return nextRules
}

function getFirstFieldError(errors: unknown[]) {
  const firstError = errors[0]
  return typeof firstError === "string" ? firstError : undefined
}

function getApiErrorMessage(error: ApiError) {
  const details = error.details ? ` Details: ${JSON.stringify(error.details)}` : ""
  return `${error.message}.${details}`
}

function getPortSpecError(value: string) {
  const normalized = value.trim()
  if (!normalized) {
    return null
  }

  const content = normalized.startsWith("!") ? normalized.slice(1) : normalized
  if (!content || content.startsWith(",") || content.endsWith(",")) {
    return "Use comma-separated ports or ranges."
  }

  const tokens = content.split(",")
  for (const token of tokens) {
    const part = token.trim()
    if (!part) {
      return "Use comma-separated ports or ranges."
    }

    if (part.includes("-")) {
      const [start, end, extra] = part.split("-")
      if (extra !== undefined || !isValidPort(start) || !isValidPort(end)) {
        return "Port ranges must use valid ports such as 8000-9000."
      }

      if (Number(start) > Number(end)) {
        return "Port range start must be less than or equal to end."
      }

      continue
    }

    if (!isValidPort(part)) {
      return "Ports must be integers between 1 and 65535."
    }
  }

  return null
}

function isValidPort(value: string) {
  if (!/^\d+$/.test(value)) {
    return false
  }

  const port = Number(value)
  return port >= 1 && port <= 65535
}

function getAddressSpecError(value: string) {
  const normalized = value.trim()
  if (!normalized) {
    return null
  }

  const content = normalized.startsWith("!") ? normalized.slice(1) : normalized
  if (!content || content.startsWith(",") || content.endsWith(",")) {
    return "Use comma-separated IP addresses or CIDRs."
  }

  const tokens = content.split(",")
  for (const token of tokens) {
    const address = token.trim()
    if (!address || !isValidAddressSpec(address)) {
      return "Addresses must be valid IPv4 or IPv6 hosts or CIDR ranges, for example 10.0.0.1, 10.0.0.0/8, or 2001:db8::/32."
    }
  }

  return null
}

function isValidAddressSpec(value: string) {
  if (!value.includes("/")) {
    return isValidIpv4(value) || isValidIpv6(value)
  }

  const parts = value.split("/")
  if (parts.length !== 2) {
    return false
  }

  const [ip, prefix] = parts
  if (!/^\d+$/.test(prefix)) {
    return false
  }

  if (isValidIpv4(ip)) {
    const prefixNumber = Number(prefix)
    return prefixNumber >= 0 && prefixNumber <= 32
  }

  if (isValidIpv6(ip)) {
    const prefixNumber = Number(prefix)
    return prefixNumber >= 0 && prefixNumber <= 128
  }

  return false
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

    const number = Number(octet)
    return number >= 0 && number <= 255
  })
}

function isValidIpv6(value: string) {
  if (!/^[0-9A-Fa-f:]+$/.test(value)) {
    return false
  }

  const blocks = value.split("::")
  if (blocks.length > 2) {
    return false
  }

  if (blocks.length === 2) {
    const left = blocks[0] ? blocks[0].split(":") : []
    const right = blocks[1] ? blocks[1].split(":") : []
    if (
      !left.every(isValidIpv6Block) ||
      !right.every(isValidIpv6Block) ||
      left.length + right.length >= 8
    ) {
      return false
    }

    return true
  }

  const parts = value.split(":")
  return parts.length === 8 && parts.every(isValidIpv6Block)
}

function isValidIpv6Block(part: string) {
  return /^[0-9A-Fa-f]{1,4}$/.test(part)
}
