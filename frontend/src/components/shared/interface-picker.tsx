import { Autocomplete } from "@base-ui/react/autocomplete"
import { ChevronsUpDown, ListPlus, Plus, Trash2 } from "lucide-react"
import type { ReactNode } from "react"
import { useMemo, useState } from "react"
import { useTranslation } from "react-i18next"

import type { RuntimeInterfaceInventoryEntry } from "@/api/generated/model"
import { FieldError } from "@/components/shared/field"
import { Badge } from "@/components/ui/badge"
import { Button } from "@/components/ui/button"
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip"
import { cn } from "@/lib/utils"

type InterfacePickerProps = {
  id?: string
  name?: string
  value: string
  interfaces: RuntimeInterfaceInventoryEntry[]
  onChange: (value: string) => void
  onSelect?: (name: string) => void
  placeholder?: string
  emptyMessage?: string
  disabled?: boolean
  invalid?: boolean
  showDetails?: boolean
  allowCustomOption?: boolean
  renderSelectedInline?: boolean
  className?: string
}

type VirtualInterfaceEntry = {
  name: string
  virtual: true
}

type InterfacePickerItem =
  | RuntimeInterfaceInventoryEntry
  | VirtualInterfaceEntry

export function InterfacePicker({
  id,
  name,
  value,
  interfaces,
  onChange,
  onSelect,
  placeholder,
  emptyMessage,
  disabled = false,
  invalid = false,
  showDetails = true,
  allowCustomOption = false,
  renderSelectedInline = false,
  className,
}: InterfacePickerProps) {
  const { t } = useTranslation()
  const [isFocused, setIsFocused] = useState(false)
  const trimmedValue = value.trim()
  const selectedInterface = findInterface(interfaces, value)
  const hasExactInterface = Boolean(selectedInterface)
  const filteredInterfaces = useMemo(
    () => filterInterfaces(interfaces, value),
    [interfaces, value]
  )
  const pickerItems = useMemo<InterfacePickerItem[]>(() => {
    if (!allowCustomOption || !trimmedValue || hasExactInterface) {
      return filteredInterfaces
    }

    return [...filteredInterfaces, { name: trimmedValue, virtual: true }]
  }, [allowCustomOption, filteredInterfaces, hasExactInterface, trimmedValue])
  const shouldRenderPopup = pickerItems.length > 0 || Boolean(trimmedValue)
  const inlineItem =
    renderSelectedInline && !isFocused && trimmedValue
      ? selectedInterface
        ? ({ item: selectedInterface, isVirtual: false } as const)
        : allowCustomOption
          ? ({
              item: { name: trimmedValue, virtual: true },
              isVirtual: true,
            } as const)
          : null
      : null

  return (
    <div className={cn(className)}>
      <Autocomplete.Root
        items={pickerItems}
        itemToStringValue={(item) => item.name}
        mode="list"
        onValueChange={(nextValue, details) => {
          onChange(nextValue)
          if (details.reason === "item-press") {
            onSelect?.(nextValue)
          }
        }}
        openOnInputClick
        value={value}
      >
        <div className="relative">
          <Autocomplete.Input
            aria-invalid={invalid}
            className={cn(
              "h-8 w-full min-w-0 rounded-lg border border-input bg-transparent px-2.5 py-1 pr-9 text-base transition-colors outline-none placeholder:text-muted-foreground focus-visible:border-ring focus-visible:ring-3 focus-visible:ring-ring/50 disabled:pointer-events-none disabled:cursor-not-allowed disabled:bg-input/50 disabled:opacity-50 aria-invalid:border-destructive aria-invalid:ring-3 aria-invalid:ring-destructive/20 md:text-sm dark:bg-input/30 dark:aria-invalid:border-destructive/50 dark:aria-invalid:ring-destructive/40",
              inlineItem ? "text-transparent caret-transparent" : null
            )}
            disabled={disabled}
            id={id}
            name={name}
            onBlur={() => setIsFocused(false)}
            onFocus={() => setIsFocused(true)}
            placeholder={placeholder}
          />
          {inlineItem ? (
            <div className="pointer-events-none absolute inset-y-0 right-9 left-2.5 flex items-center overflow-hidden">
              <InterfaceRowContent
                interfaceEntry={
                  inlineItem.isVirtual ? undefined : inlineItem.item
                }
                isVirtual={inlineItem.isVirtual}
                name={inlineItem.item.name}
                showAddressesInline
              />
            </div>
          ) : null}
          <Autocomplete.Trigger
            aria-label={t("common.interfacePicker.open")}
            className="absolute top-0 right-0 flex h-8 w-8 items-center justify-center rounded-r-lg text-muted-foreground transition-colors hover:text-foreground disabled:pointer-events-none disabled:opacity-50"
            disabled={disabled}
            type="button"
          >
            <ChevronsUpDown className="h-4 w-4" />
          </Autocomplete.Trigger>
        </div>
        {shouldRenderPopup ? (
          <Autocomplete.Portal>
            <Autocomplete.Positioner className="z-50" sideOffset={4}>
              <Autocomplete.Popup className="max-h-60 w-[var(--anchor-width)] min-w-72 overflow-hidden rounded-lg border border-border bg-popover p-1 text-popover-foreground shadow-md outline-hidden">
                {pickerItems.length > 0 ? (
                  <Autocomplete.List className="max-h-56 overflow-y-auto">
                    {(item: InterfacePickerItem, index) => (
                      <Autocomplete.Item
                        className="flex cursor-default items-start gap-2 rounded-md px-2 py-1.5 text-sm outline-hidden select-none hover:bg-accent hover:text-accent-foreground data-[highlighted]:bg-accent data-[highlighted]:text-accent-foreground"
                        index={index}
                        key={
                          isVirtualInterface(item)
                            ? `custom:${item.name}`
                            : item.name
                        }
                        value={item}
                      >
                        <InterfaceOption item={item} />
                      </Autocomplete.Item>
                    )}
                  </Autocomplete.List>
                ) : (
                  <div className="px-2 py-2 text-sm text-muted-foreground">
                    {emptyMessage ?? t("common.interfacePicker.notFound")}
                  </div>
                )}
              </Autocomplete.Popup>
            </Autocomplete.Positioner>
          </Autocomplete.Portal>
        ) : null}
      </Autocomplete.Root>

      {showDetails && selectedInterface ? (
        <div className="rounded-lg border border-border bg-background px-2.5 py-2">
          <InterfaceRowContent
            interfaceEntry={selectedInterface}
            name={selectedInterface.name}
          />
        </div>
      ) : showDetails &&
        allowCustomOption &&
        trimmedValue &&
        !selectedInterface ? (
        <div className="rounded-lg border border-border bg-background px-2.5 py-2">
          <InterfaceRowContent isVirtual name={trimmedValue} />
        </div>
      ) : null}
    </div>
  )
}

type InterfaceMultiSelectListProps = {
  name?: string
  interfaces: RuntimeInterfaceInventoryEntry[]
  value: string[]
  onChange: (nextValue: string[]) => void
  addLabel?: string
  emptyMessage?: string
  placeholderTitle?: string
  placeholderDescription?: string
  error?: string | null
}

export function InterfaceMultiSelectList({
  name,
  interfaces,
  value,
  onChange,
  addLabel,
  emptyMessage,
  placeholderTitle,
  placeholderDescription,
  error,
}: InterfaceMultiSelectListProps) {
  const { t } = useTranslation()
  const [pickerValue, setPickerValue] = useState("")
  const selectedSet = useMemo(() => new Set(value), [value])
  const availableInterfaces = interfaces.filter(
    (item) => !selectedSet.has(item.name)
  )
  const canAddTyped = availableInterfaces.some(
    (item) => item.name === pickerValue
  )

  const addInterface = (interfaceName: string) => {
    if (!interfaceName || selectedSet.has(interfaceName)) {
      return
    }

    if (!interfaces.some((item) => item.name === interfaceName)) {
      return
    }

    onChange([...value, interfaceName])
    setPickerValue("")
  }

  return (
    <div className="space-y-2" data-field-name={name}>
      <div
        className={cn(
          "space-y-3 rounded-xl border p-3",
          error ? "border-destructive" : "border-border"
        )}
      >
        {value.length ? (
          <div className="space-y-2">
            {value.map((item, index) => (
              <SelectedInterfaceRow
                interfaceEntry={findInterface(interfaces, item)}
                key={`${item}-${index}`}
                name={item}
                onRemove={() =>
                  onChange(
                    value.filter((_, currentIndex) => currentIndex !== index)
                  )
                }
              />
            ))}
          </div>
        ) : (
          <div className="flex items-start gap-3">
            <div className="flex h-10 w-10 shrink-0 items-center justify-center rounded-lg bg-muted/50">
              <ListPlus className="h-5 w-5 text-muted-foreground" />
            </div>
            <div className="mt-0.5 flex flex-col gap-0.5">
              <span className="text-sm font-medium text-foreground">
                {placeholderTitle ??
                  t("common.multiSelectList.noItemsSelected")}
              </span>
              {placeholderDescription ? (
                <span className="text-sm text-muted-foreground">
                  {placeholderDescription}
                </span>
              ) : null}
            </div>
          </div>
        )}

        <div className="flex flex-col gap-2 sm:flex-row">
          <InterfacePicker
            className="min-w-0 flex-1"
            disabled={availableInterfaces.length === 0}
            emptyMessage={t("common.interfacePicker.notFound")}
            interfaces={availableInterfaces}
            invalid={Boolean(error)}
            onChange={setPickerValue}
            onSelect={addInterface}
            placeholder={
              availableInterfaces.length > 0
                ? (addLabel ?? t("common.multiSelectList.addItem"))
                : (emptyMessage ?? t("common.multiSelectList.emptyMessage"))
            }
            showDetails={false}
            value={pickerValue}
          />
          <Button
            className="whitespace-nowrap"
            disabled={!canAddTyped}
            onClick={() => addInterface(pickerValue)}
            type="button"
            variant="outline"
          >
            <Plus className="h-4 w-4" />
            {addLabel ?? t("common.multiSelectList.addItem")}
          </Button>
        </div>
      </div>
      {error ? <FieldError>{error}</FieldError> : null}
    </div>
  )
}

function InterfaceOption({ item }: { item: InterfacePickerItem }) {
  return (
    <InterfaceRowContent
      interfaceEntry={isVirtualInterface(item) ? undefined : item}
      isVirtual={isVirtualInterface(item)}
      name={item.name}
      showAddressesInline
    />
  )
}

function SelectedInterfaceRow({
  name,
  interfaceEntry,
  onRemove,
}: {
  name: string
  interfaceEntry?: RuntimeInterfaceInventoryEntry
  onRemove: () => void
}) {
  const { t } = useTranslation()

  return (
    <div className="flex min-w-0 items-center gap-2 rounded-lg border border-border bg-background px-2.5 py-2">
      <InterfaceRowContent
        interfaceEntry={interfaceEntry}
        name={name}
        showAddressesInline
      />
      <Button
        aria-label={t("common.multiSelectList.removeItem", { item: name })}
        className="size-5 text-destructive hover:text-destructive [&_svg:not([class*='size-'])]:size-3.5"
        onClick={onRemove}
        size="icon-xs"
        type="button"
        variant="ghost"
      >
        <Trash2 className="h-4 w-4" />
      </Button>
    </div>
  )
}

export function InterfaceRowContent({
  name,
  interfaceEntry,
  isVirtual = false,
  grow = true,
  afterStatus,
  showAddressesInline = false,
}: {
  name: string
  interfaceEntry?: RuntimeInterfaceInventoryEntry
  isVirtual?: boolean
  grow?: boolean
  afterStatus?: ReactNode
  showAddressesInline?: boolean
}) {
  const { t } = useTranslation()
  const addresses = interfaceEntry ? getInterfaceAddresses(interfaceEntry) : []
  const className = cn(
    "flex min-h-5 min-w-0 flex-wrap items-center gap-2",
    grow ? "flex-1" : null
  )

  const content = (
    <>
      <span className="truncate text-sm font-medium text-foreground">
        {name}
      </span>
      {interfaceEntry ? (
        <>
          <InterfaceStatusBadge status={interfaceEntry.status} />
          {afterStatus}
          {showAddressesInline ? (
            <AddressPreview interfaceEntry={interfaceEntry} />
          ) : null}
        </>
      ) : isVirtual ? (
        <span className="text-xs text-muted-foreground">
          {t("common.interfacePicker.notExists")}
        </span>
      ) : (
        <>
          <Badge size="xs" variant="warning">
            {t("pages.settings.general.inboundInterfacesStatusMissing")}
          </Badge>
          <span className="text-xs text-muted-foreground">
            {t("pages.settings.general.inboundInterfacesMissingDetail")}
          </span>
        </>
      )}
    </>
  )

  if (!addresses.length || showAddressesInline) {
    return <div className={className}>{content}</div>
  }

  return (
    <Tooltip>
      <TooltipTrigger render={<div className={className} />}>
        {content}
      </TooltipTrigger>
      <TooltipContent
        align="center"
        className="max-w-md"
        side="right"
        sideOffset={16}
      >
        <AddressTooltipContent addresses={addresses} />
      </TooltipContent>
    </Tooltip>
  )
}

export function OutboundInterfaceLabel({
  tag,
  interfaceName,
  runtimeInterface,
  t,
}: {
  tag: string
  interfaceName?: string
  runtimeInterface?: RuntimeInterfaceInventoryEntry
  t: (key: string, options?: Record<string, unknown>) => string
}) {
  const ipv4 = runtimeInterface?.ipv4_addresses?.[0]
  const ipv6 = runtimeInterface?.ipv6_addresses?.[0]

  return (
    <div className="flex min-w-0 items-center gap-2 overflow-hidden whitespace-nowrap">
      <span className="shrink-0 text-sm font-medium text-foreground">
        {tag}
      </span>
      {interfaceName ? (
        <>
          <span className="shrink-0 text-sm font-medium text-foreground">
            ({interfaceName})
          </span>
          {runtimeInterface ? (
            <>
              <InterfaceStatusBadge status={runtimeInterface.status} />
              {ipv4 ? <AddressPreviewChip address={ipv4} /> : null}
              {ipv6 ? <AddressPreviewChip address={ipv6} /> : null}
            </>
          ) : (
            <span className="text-xs text-muted-foreground">
              {t("common.interfacePicker.notExists")}
            </span>
          )}
        </>
      ) : null}
    </div>
  )
}

function InterfaceStatusBadge({
  status,
}: {
  status: RuntimeInterfaceInventoryEntry["status"]
}) {
  const { t } = useTranslation()

  return (
    <Badge size="xs" variant={status === "up" ? "success" : "outline"}>
      {status === "up"
        ? t("pages.settings.general.inboundInterfacesStatusUp")
        : t("pages.settings.general.inboundInterfacesStatusDown")}
    </Badge>
  )
}

export function InterfaceAddressDetails({
  interfaceEntry,
  compact = false,
}: {
  interfaceEntry: RuntimeInterfaceInventoryEntry
  compact?: boolean
}) {
  const addresses = getInterfaceAddresses(interfaceEntry)

  if (!addresses.length) {
    return null
  }

  return (
    <div
      className={cn(
        "flex flex-wrap gap-1",
        compact
          ? "text-xs"
          : "rounded-lg border border-border bg-muted/30 p-2 text-xs"
      )}
    >
      {addresses.map((address) => (
        <code
          className={cn(
            "rounded-md px-1.5 py-0.5 text-muted-foreground",
            compact ? "bg-muted" : "bg-background"
          )}
          key={address}
        >
          {address}
        </code>
      ))}
    </div>
  )
}

function AddressTooltipContent({ addresses }: { addresses: string[] }) {
  return (
    <div className="flex flex-col items-start gap-1">
      {addresses.map((address) => (
        <code
          className="rounded-md bg-background/15 px-1.5 py-0.5 text-background"
          key={address}
        >
          {address}
        </code>
      ))}
    </div>
  )
}

function AddressPreview({
  interfaceEntry,
}: {
  interfaceEntry: RuntimeInterfaceInventoryEntry
}) {
  const ipv4 = interfaceEntry.ipv4_addresses?.[0]
  const ipv6 = interfaceEntry.ipv6_addresses?.[0]

  if (!ipv4 && !ipv6) {
    return null
  }

  return (
    <div className="flex min-w-0 flex-wrap items-center gap-1 text-xs text-muted-foreground">
      {ipv4 ? <AddressPreviewChip address={ipv4} /> : null}
      {ipv6 ? <AddressPreviewChip address={ipv6} /> : null}
    </div>
  )
}

function AddressPreviewChip({ address }: { address: string }) {
  return (
    <code className="truncate rounded bg-muted px-1.5 py-0.5">{address}</code>
  )
}

function getInterfaceAddresses(interfaceEntry: RuntimeInterfaceInventoryEntry) {
  return [
    ...(interfaceEntry.ipv4_addresses ?? []),
    ...(interfaceEntry.ipv6_addresses ?? []),
  ]
}

function findInterface(
  interfaces: RuntimeInterfaceInventoryEntry[],
  name: string
) {
  return interfaces.find((item) => item.name === name)
}

function isVirtualInterface(
  item: InterfacePickerItem
): item is VirtualInterfaceEntry {
  return "virtual" in item && item.virtual
}

function filterInterfaces(
  interfaces: RuntimeInterfaceInventoryEntry[],
  query: string
) {
  const normalizedQuery = query.trim().toLowerCase()
  if (!normalizedQuery) {
    return interfaces
  }

  return interfaces.filter((item) =>
    [item.name, ...(item.ipv4_addresses ?? []), ...(item.ipv6_addresses ?? [])]
      .join(" ")
      .toLowerCase()
      .includes(normalizedQuery)
  )
}
