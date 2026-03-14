import { ListPlus, Plus, Trash2 } from "lucide-react"
import { useState } from "react"

import { Button } from "@/components/ui/button"
import {
  Command,
  CommandEmpty,
  CommandGroup,
  CommandItem,
  CommandList,
} from "@/components/ui/command"
import {
  Empty,
  EmptyContent,
  EmptyDescription,
  EmptyHeader,
  EmptyMedia,
  EmptyTitle,
} from "@/components/ui/empty"
import { InputGroup, InputGroupAddon, InputGroupButton } from "@/components/ui/input-group"
import { Popover, PopoverContent, PopoverTrigger } from "@/components/ui/popover"

export function MultiSelectList({
  options,
  unavailable = [],
  value,
  onChange,
  addLabel = "Add item",
  emptyMessage = "No items found.",
  groupLabel = "Available items",
  placeholderTitle = "No items selected",
  placeholderDescription = "Add your first item to start building this list.",
}: {
  options: string[]
  unavailable?: string[]
  value: string[]
  onChange: (nextValue: string[]) => void
  addLabel?: string
  emptyMessage?: string
  groupLabel?: string
  placeholderTitle?: string
  placeholderDescription?: string
}) {
  const [open, setOpen] = useState(false)
  const selectedSet = new Set(value)
  const unavailableSet = new Set(unavailable)

  const addButton = (
    <Popover onOpenChange={setOpen} open={open}>
      <PopoverTrigger render={<Button size="sm" type="button" variant="outline" />}>
        <Plus className="h-4 w-4" />
        {addLabel}
      </PopoverTrigger>
      <PopoverContent align="start" className="w-[280px] p-0" sideOffset={2}>
        <Command>
          <CommandList>
            <CommandEmpty>{emptyMessage}</CommandEmpty>
            <CommandGroup heading={groupLabel}>
              {options.map((option) => {
                const selected = selectedSet.has(option)
                const disabled = selected || unavailableSet.has(option)

                return (
                  <CommandItem
                    data-checked={selected}
                    disabled={disabled}
                    key={option}
                    onSelect={() => {
                      if (disabled) {
                        return
                      }

                      onChange([...value, option])
                      setOpen(false)
                    }}
                  >
                    <span className={disabled ? "text-muted-foreground" : undefined}>
                      {option}
                    </span>
                  </CommandItem>
                )
              })}
            </CommandGroup>
          </CommandList>
        </Command>
      </PopoverContent>
    </Popover>
  )

  return (
    <div className="space-y-2">
      {value.length ? (
        <div className="space-y-2 rounded-xl border border-border p-3">
          {value.map((item, index) => (
            <InputGroup key={`${item}-${index}`}>
              <InputGroupAddon className="w-full justify-start text-foreground">
                {item}
              </InputGroupAddon>
              <InputGroupAddon align="inline-end">
                <InputGroupButton
                  aria-label={`Remove ${item}`}
                  className="text-destructive hover:text-destructive"
                  onClick={() => onChange(value.filter((current) => current !== item))}
                  size="icon-xs"
                >
                  <Trash2 className="h-4 w-4" />
                </InputGroupButton>
              </InputGroupAddon>
            </InputGroup>
          ))}
          <div>{addButton}</div>
        </div>
      ) : (
        <Empty className="border border-border">
          <EmptyHeader>
            <EmptyMedia variant="icon">
              <ListPlus />
            </EmptyMedia>
            <EmptyTitle>{placeholderTitle}</EmptyTitle>
            <EmptyDescription>{placeholderDescription}</EmptyDescription>
          </EmptyHeader>
          <EmptyContent>{addButton}</EmptyContent>
        </Empty>
      )}
    </div>
  )
}
