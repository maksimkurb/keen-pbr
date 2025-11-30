import { Check, File, Globe, List, ListPlus, Plus, Trash2 } from 'lucide-react';
import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { ListInfo } from '../../src/api/client';
import { cn } from '../../src/lib/utils';
import { Button } from './button';
import {
  Command,
  CommandEmpty,
  CommandGroup,
  CommandInput,
  CommandItem,
  CommandList,
} from './command';
import {
  Empty,
  EmptyDescription,
  EmptyHeader,
  EmptyMedia,
  EmptyTitle,
} from './empty';
import { InputGroup, InputGroupAddon, InputGroupButton } from './input-group';
import { Popover, PopoverContent, PopoverTrigger } from './popover';

interface ListSelectorProps {
  value: string[];
  onChange: (lists: string[]) => void;
  availableLists: ListInfo[];
  className?: string;
}

// Helper function to get icon for list type
const getListIcon = (type: string) => {
  switch (type) {
    case 'url':
      return Globe;
    case 'file':
      return File;
    case 'hosts':
      return List;
    default:
      return ListPlus;
  }
};

export function ListSelector({
  value,
  onChange,
  availableLists,
  className,
}: ListSelectorProps) {
  const { t } = useTranslation();
  const [open, setOpen] = useState(false);

  const addList = (listName: string) => {
    if (!value.includes(listName)) {
      onChange([...value, listName]);
    }
    setOpen(false);
  };

  const removeList = (listName: string) => {
    onChange(value.filter((l) => l !== listName));
  };

  return (
    <div className={cn('max-w-lg', className)}>
      {value.length > 0 ? (
        <div className="space-y-2 p-4 border rounded-md">
          {value.map((listName) => {
            const listInfo = availableLists.find(
              (l) => l.list_name === listName,
            );
            const ListIcon = listInfo ? getListIcon(listInfo.type) : ListPlus;

            return (
              <InputGroup key={listName}>
                <InputGroupAddon className="cursor-default">
                  <ListIcon className="h-4 w-4 text-foreground" />
                </InputGroupAddon>
                <InputGroupAddon className="w-full justify-start cursor-default text-foreground">
                  <span>{listName}</span>
                </InputGroupAddon>
                <InputGroupAddon align="inline-end">
                  <InputGroupButton
                    size="icon-xs"
                    onClick={() => removeList(listName)}
                    aria-label="Remove"
                    title="Remove"
                    className="text-destructive hover:text-destructive"
                  >
                    <Trash2 />
                  </InputGroupButton>
                </InputGroupAddon>
              </InputGroup>
            );
          })}
          <Popover open={open} onOpenChange={setOpen}>
            <PopoverTrigger asChild>
              <Button
                variant="outline"
                size="sm"
                role="combobox"
                aria-expanded={open}
              >
                <Plus className="mr-2 h-4 w-4" />
                {t('routingRules.dialog.addList')}
              </Button>
            </PopoverTrigger>
            <PopoverContent align="start" className="w-100 p-0">
              <Command>
                <CommandInput
                  placeholder={t('routingRules.dialog.searchLists')}
                />
                <CommandList className="max-h-[300px] overflow-y-auto">
                  <CommandEmpty>
                    {t('routingRules.dialog.noLists')}
                  </CommandEmpty>
                  <CommandGroup>
                    {availableLists.map((list) => {
                      const ListIcon = getListIcon(list.type);
                      return (
                        <CommandItem
                          key={list.list_name}
                          value={list.list_name}
                          onSelect={() => addList(list.list_name)}
                          disabled={value.includes(list.list_name)}
                        >
                          <Check
                            className={cn(
                              'mr-2 h-4 w-4',
                              value.includes(list.list_name)
                                ? 'opacity-100'
                                : 'opacity-0',
                            )}
                          />
                          <ListIcon className="mr-2 h-4 w-4 text-muted-foreground" />
                          <span>{list.list_name}</span>
                        </CommandItem>
                      );
                    })}
                  </CommandGroup>
                </CommandList>
              </Command>
            </PopoverContent>
          </Popover>
        </div>
      ) : (
        <Empty className="border">
          <EmptyHeader>
            <EmptyMedia variant="icon">
              <ListPlus className="h-5 w-5" />
            </EmptyMedia>
            <EmptyTitle className="text-base">
              {t('routingRules.dialog.emptyLists.title')}
            </EmptyTitle>
            <EmptyDescription>
              {t('routingRules.dialog.emptyLists.description')}
            </EmptyDescription>
          </EmptyHeader>
          <Popover open={open} onOpenChange={setOpen}>
            <PopoverTrigger asChild>
              <Button
                variant="outline"
                size="sm"
                role="combobox"
                aria-expanded={open}
              >
                <Plus className="mr-2 h-4 w-4" />
                {t('routingRules.dialog.addList')}
              </Button>
            </PopoverTrigger>
            <PopoverContent className="w-full p-0">
              <Command>
                <CommandInput
                  placeholder={t('routingRules.dialog.searchLists')}
                />
                <CommandList className="max-h-[300px] overflow-y-auto">
                  <CommandEmpty>
                    {t('routingRules.dialog.noLists')}
                  </CommandEmpty>
                  <CommandGroup>
                    {availableLists.map((list) => {
                      const ListIcon = getListIcon(list.type);
                      return (
                        <CommandItem
                          key={list.list_name}
                          value={list.list_name}
                          onSelect={() => addList(list.list_name)}
                          disabled={value.includes(list.list_name)}
                        >
                          <ListIcon className="mr-2 h-4 w-4 text-muted-foreground" />
                          <span>{list.list_name}</span>
                        </CommandItem>
                      );
                    })}
                  </CommandGroup>
                </CommandList>
              </Command>
            </PopoverContent>
          </Popover>
        </Empty>
      )}
    </div>
  );
}
