import { ChevronDown, ChevronUp, ListPlus, Plus, Trash2 } from "lucide-react"
import { useState } from "react"
import { useTranslation } from "react-i18next"

import { FieldError } from "@/components/shared/field"
import { InputGroup, InputGroupAddon, InputGroupButton } from "@/components/ui/input-group"
import {
  Select,
  SelectContent,
  SelectGroup,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"

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
  error,
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
  error?: string | null
}) {
  const { t } = useTranslation()
  const [selectValue, setSelectValue] = useState("")
  const selectedSet = new Set(value)
  const unavailableSet = new Set(unavailable)
  const availableOptions = options.filter(
    (option) => !selectedSet.has(option) && !unavailableSet.has(option)
  )

  const resolvedAddLabel = addLabel ?? t("common.multiSelectList.addItem")
  const resolvedEmptyMessage =
    emptyMessage ?? t("common.multiSelectList.emptyMessage")
  const resolvedPlaceholderTitle =
    placeholderTitle ?? t("common.multiSelectList.noItemsSelected")
  const resolvedPlaceholderDescription =
    placeholderDescription ?? t("common.multiSelectList.addFirstItem")

  const addSelect = (
    <Select
      onValueChange={(nextValue) => {
        if (!nextValue) {
          return
        }

        onChange([...value, nextValue])
        setSelectValue("")
      }}
      value={selectValue}
    >
      <SelectTrigger
        aria-invalid={Boolean(error)}
        className="w-full sm:w-auto sm:min-w-52 data-placeholder:text-foreground disabled:data-placeholder:text-muted-foreground"
        disabled={availableOptions.length === 0}
        size="sm"
      >
        <Plus className="h-4 w-4" />
        <SelectValue
          placeholder={
            availableOptions.length > 0
              ? resolvedAddLabel
              : resolvedEmptyMessage
          }
        />
      </SelectTrigger>
      <SelectContent alignItemWithTrigger={false} align="start">
        <SelectGroup>
          {availableOptions.map((option) => (
            <SelectItem key={option} value={option}>
              {option}
            </SelectItem>
          ))}
        </SelectGroup>
      </SelectContent>
    </Select>
  )

  return (
    <div className="space-y-2" data-field-name={name}>
      {value.length ? (
        <div className={`space-y-2 rounded-xl border p-3 ${error ? "border-destructive" : "border-border"}`}>
          {value.map((item, index) => (
            <InputGroup key={`${item}-${index}`} className="cursor-default">
              <InputGroupAddon className="w-full justify-start text-foreground">
                {item}
              </InputGroupAddon>
              <InputGroupAddon align="inline-end">
                <InputGroupButton
                  aria-label={t("common.multiSelectList.removeItem", { item })}
                  className="text-destructive hover:text-destructive"
                  onClick={() =>
                    onChange(value.filter((_, currentIndex) => currentIndex !== index))
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
        <div className={`space-y-3 rounded-xl border p-3 ${error ? "border-destructive" : "border-border"}`}>
          <div className="flex items-start gap-3">
            <div className="flex h-10 w-10 shrink-0 items-center justify-center rounded-lg bg-muted/50">
              <ListPlus className="h-5 w-5 text-muted-foreground" />
            </div>
            <div className="mt-0.5 flex flex-col gap-0.5">
              <span className="text-sm font-medium text-foreground">{resolvedPlaceholderTitle}</span>
              {resolvedPlaceholderDescription ? (
                <span className="text-sm text-muted-foreground">{resolvedPlaceholderDescription}</span>
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
