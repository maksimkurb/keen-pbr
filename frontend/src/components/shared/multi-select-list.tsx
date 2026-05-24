import { Autocomplete } from "@base-ui/react/autocomplete"
import {
  ChevronDown,
  ChevronUp,
  ChevronsUpDown,
  ListPlus,
  Plus,
  Trash2,
} from "lucide-react"
import type { ReactNode } from "react"
import { useMemo, useState } from "react"
import { useTranslation } from "react-i18next"

import { FieldError } from "@/components/shared/field"
import {
  InputGroup,
  InputGroupAddon,
  InputGroupButton,
} from "@/components/ui/input-group"
import { cn } from "@/lib/utils"

function OptionLabel({
  option,
  renderItem,
  usageSubtitle,
}: {
  option: string
  renderItem?: (item: string) => ReactNode
  usageSubtitle?: (optionName: string) => string | undefined
}) {
  const usage = usageSubtitle?.(option)
  const label = renderItem ? renderItem(option) : option

  if (!usage) {
    return <>{label}</>
  }

  return (
    <span className="flex max-w-[min(100vw-4rem,24rem)] flex-col items-start gap-0.5 py-0.5">
      <span>{label}</span>
      <span className="text-xs break-words whitespace-normal text-muted-foreground">
        {usage}
      </span>
    </span>
  )
}

export function MultiSelectList({
  name,
  options,
  unavailable = [],
  value,
  onChange,
  addLabel,
  emptyMessage,
  placeholderTitle,
  placeholderDescription,
  allowReorder = false,
  usageSubtitle,
  error,
  renderItem,
  getSearchText,
}: {
  name?: string
  options: string[]
  unavailable?: string[]
  value: string[]
  onChange: (nextValue: string[]) => void
  addLabel?: string
  emptyMessage?: string
  groupLabel?: string
  placeholderTitle?: string
  placeholderDescription?: string
  allowReorder?: boolean
  usageSubtitle?: (optionName: string) => string | undefined
  error?: string | null
  renderItem?: (item: string) => ReactNode
  getSearchText?: (item: string) => string
}) {
  const { t } = useTranslation()
  const [selectValue, setSelectValue] = useState("")
  const selectedSet = new Set(value)
  const unavailableSet = new Set(unavailable)
  const availableOptions = options.filter(
    (option) => !selectedSet.has(option) && !unavailableSet.has(option)
  )
  const filteredOptions = useMemo(() => {
    const normalizedValue = selectValue.trim().toLowerCase()

    if (!normalizedValue) {
      return availableOptions
    }

    return availableOptions.filter((option) =>
      (getSearchText?.(option) ?? option)
        .toLowerCase()
        .includes(normalizedValue)
    )
  }, [availableOptions, getSearchText, selectValue])

  const resolvedAddLabel = addLabel ?? t("common.multiSelectList.addItem")
  const resolvedEmptyMessage =
    emptyMessage ?? t("common.multiSelectList.emptyMessage")
  const resolvedPlaceholderTitle =
    placeholderTitle ?? t("common.multiSelectList.noItemsSelected")
  const resolvedPlaceholderDescription =
    placeholderDescription ?? t("common.multiSelectList.addFirstItem")

  const addOption = (nextValue: string) => {
    if (!nextValue || !availableOptions.includes(nextValue)) {
      return
    }

    onChange([...value, nextValue])
    setSelectValue("")
  }
  const shouldRenderPopup =
    filteredOptions.length > 0 || Boolean(selectValue.trim())
  const addSelect = (
    <Autocomplete.Root
      items={filteredOptions}
      itemToStringValue={(item) => item}
      mode="list"
      onValueChange={(nextValue, details) => {
        setSelectValue(nextValue)
        if (details.reason === "item-press") {
          addOption(nextValue)
        }
      }}
      openOnInputClick
      value={selectValue}
    >
      <div className="relative w-full sm:w-80">
        <Plus className="pointer-events-none absolute top-1/2 left-2.5 h-4 w-4 -translate-y-1/2 text-muted-foreground" />
        <Autocomplete.Input
          aria-invalid={Boolean(error)}
          className="h-8 w-full min-w-0 rounded-lg border border-input bg-transparent py-1 pr-9 pl-8 text-base transition-colors outline-none placeholder:text-foreground focus-visible:border-ring focus-visible:ring-3 focus-visible:ring-ring/50 disabled:pointer-events-none disabled:cursor-not-allowed disabled:bg-input/50 disabled:text-muted-foreground disabled:opacity-50 aria-invalid:border-destructive aria-invalid:ring-3 aria-invalid:ring-destructive/20 md:text-sm dark:bg-input/30 dark:aria-invalid:border-destructive/50 dark:aria-invalid:ring-destructive/40"
          disabled={availableOptions.length === 0}
          placeholder={
            availableOptions.length > 0
              ? resolvedAddLabel
              : resolvedEmptyMessage
          }
        />
        <Autocomplete.Trigger
          aria-label={resolvedAddLabel}
          className="absolute top-0 right-0 flex h-8 w-8 items-center justify-center rounded-r-lg text-muted-foreground transition-colors hover:text-foreground disabled:pointer-events-none disabled:opacity-50"
          disabled={availableOptions.length === 0}
          type="button"
        >
          <ChevronsUpDown className="h-4 w-4" />
        </Autocomplete.Trigger>
      </div>
      {shouldRenderPopup ? (
        <Autocomplete.Portal>
          <Autocomplete.Positioner className="z-50" sideOffset={4}>
            <Autocomplete.Popup className="max-h-64 w-[var(--anchor-width)] min-w-80 overflow-hidden rounded-lg border border-border bg-popover p-1 text-popover-foreground shadow-md outline-hidden">
              {filteredOptions.length > 0 ? (
                <Autocomplete.List className="max-h-60 overflow-y-auto">
                  {(option: string, index) => (
                    <Autocomplete.Item
                      className="cursor-default rounded-md px-2 py-1.5 text-sm outline-hidden select-none hover:bg-accent hover:text-accent-foreground data-[highlighted]:bg-accent data-[highlighted]:text-accent-foreground"
                      index={index}
                      key={option}
                      value={option}
                    >
                      <OptionLabel
                        option={option}
                        renderItem={renderItem}
                        usageSubtitle={usageSubtitle}
                      />
                    </Autocomplete.Item>
                  )}
                </Autocomplete.List>
              ) : (
                <div className="px-2 py-2 text-sm text-muted-foreground">
                  {resolvedEmptyMessage}
                </div>
              )}
            </Autocomplete.Popup>
          </Autocomplete.Positioner>
        </Autocomplete.Portal>
      ) : null}
    </Autocomplete.Root>
  )

  return (
    <div className="space-y-2" data-field-name={name}>
      {value.length ? (
        <div
          className={cn(
            "space-y-2 rounded-xl border p-3",
            error ? "border-destructive" : "border-border"
          )}
        >
          {value.map((item, index) => (
            <InputGroup
              key={`${item}-${index}`}
              className="h-auto min-h-8 cursor-default"
            >
              <InputGroupAddon className="w-full flex-col items-stretch gap-0.5 text-left text-foreground">
                <OptionLabel
                  option={item}
                  renderItem={renderItem}
                  usageSubtitle={usageSubtitle}
                />
              </InputGroupAddon>
              <InputGroupAddon align="inline-end">
                <InputGroupButton
                  aria-label={t("common.multiSelectList.removeItem", { item })}
                  className="text-destructive hover:text-destructive"
                  onClick={() =>
                    onChange(
                      value.filter((_, currentIndex) => currentIndex !== index)
                    )
                  }
                  size="icon-xs"
                >
                  <Trash2 className="h-4 w-4" />
                </InputGroupButton>
                {allowReorder ? (
                  <>
                    <InputGroupButton
                      aria-label={t("common.moveUp")}
                      disabled={index === 0}
                      onClick={() => {
                        if (index === 0) {
                          return
                        }
                        const nextValue = [...value]
                        ;[nextValue[index - 1], nextValue[index]] = [
                          nextValue[index],
                          nextValue[index - 1],
                        ]
                        onChange(nextValue)
                      }}
                      size="icon-xs"
                    >
                      <ChevronUp className="h-4 w-4" />
                    </InputGroupButton>
                    <InputGroupButton
                      aria-label={t("common.moveDown")}
                      disabled={index === value.length - 1}
                      onClick={() => {
                        if (index === value.length - 1) {
                          return
                        }
                        const nextValue = [...value]
                        ;[nextValue[index], nextValue[index + 1]] = [
                          nextValue[index + 1],
                          nextValue[index],
                        ]
                        onChange(nextValue)
                      }}
                      size="icon-xs"
                    >
                      <ChevronDown className="h-4 w-4" />
                    </InputGroupButton>
                  </>
                ) : null}
              </InputGroupAddon>
            </InputGroup>
          ))}
          <div>{addSelect}</div>
        </div>
      ) : (
        <div
          className={cn(
            "space-y-3 rounded-xl border p-3",
            error ? "border-destructive" : "border-border"
          )}
        >
          <div className="flex items-start gap-3">
            <div className="flex h-10 w-10 shrink-0 items-center justify-center rounded-lg bg-muted/50">
              <ListPlus className="h-5 w-5 text-muted-foreground" />
            </div>
            <div className="mt-0.5 flex flex-col gap-0.5">
              <span className="text-sm font-medium text-foreground">
                {resolvedPlaceholderTitle}
              </span>
              {resolvedPlaceholderDescription ? (
                <span className="text-sm text-muted-foreground">
                  {resolvedPlaceholderDescription}
                </span>
              ) : null}
            </div>
          </div>
          <div>{addSelect}</div>
        </div>
      )}
      {error ? <FieldError>{error}</FieldError> : null}
    </div>
  )
}
