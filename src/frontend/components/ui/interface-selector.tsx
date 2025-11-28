import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Plus, Trash2, ChevronUp, ChevronDown, Unplug, Network, Check } from 'lucide-react';
import { useInterfaces } from '../../src/hooks/useInterfaces';
import { Popover, PopoverContent, PopoverTrigger } from './popover';
import { Command, CommandEmpty, CommandGroup, CommandInput, CommandItem, CommandList } from './command';
import { Button } from './button';
import {
  InputGroup,
  InputGroupAddon,
  InputGroupButton,
  InputGroupInput,
} from './input-group';
import { Empty, EmptyHeader, EmptyMedia, EmptyTitle, EmptyDescription } from './empty';
import { cn } from '../../src/lib/utils';
import { Card } from './card';
import type { InterfaceInfo } from '../../src/api/client';

interface InterfaceSelectorProps {
  value: string[];
  onChange: (interfaces: string[]) => void;
  allowReorder?: boolean;
  className?: string;
}

export function InterfaceSelector({ value, onChange, allowReorder = false, className }: InterfaceSelectorProps) {
  const { t } = useTranslation();
  const [open, setOpen] = useState(false);
  const [customInterface, setCustomInterface] = useState('');

  // Fetch interfaces
  const { data: interfacesData } = useInterfaces(true);
  const interfaceOptions = interfacesData || [];

  const addInterface = (iface: string) => {
    if (!value.includes(iface)) {
      onChange([...value, iface]);
    }
    setOpen(false);
  };

  const addCustomInterface = () => {
    const trimmedInterface = customInterface.trim();
    if (trimmedInterface && !value.includes(trimmedInterface)) {
      onChange([...value, trimmedInterface]);
      setCustomInterface('');
      setOpen(false);
    }
  };

  const removeInterface = (iface: string) => {
    onChange(value.filter((i) => i !== iface));
  };

  const moveInterfaceUp = (index: number) => {
    if (index > 0) {
      const newInterfaces = [...value];
      [newInterfaces[index - 1], newInterfaces[index]] = [newInterfaces[index], newInterfaces[index - 1]];
      onChange(newInterfaces);
    }
  };

  const moveInterfaceDown = (index: number) => {
    if (index < value.length - 1) {
      const newInterfaces = [...value];
      [newInterfaces[index], newInterfaces[index + 1]] = [newInterfaces[index + 1], newInterfaces[index]];
      onChange(newInterfaces);
    }
  };

  return (
    <div className={cn('max-w-lg', className)}>
      {value.length > 0 ? (
        <div className='space-y-2 p-4 border rounded-md'>
          {value.map((iface, index) => {
            const interfaceInfo = interfaceOptions.find(i => i.name === iface);
            return (
              <InputGroup key={`${iface}-${index}`}>
                <InputGroupAddon className='cursor-default'>
                  <InterfaceStatus iface={interfaceInfo} />
                </InputGroupAddon>
                <InputGroupAddon className='w-full justify-start text-foreground cursor-default'>
                  <InterfaceName iface={interfaceInfo} defaultName={iface} />
                </InputGroupAddon>
                <InputGroupAddon align="inline-end">
                  {allowReorder && (
                    <>
                      <InputGroupButton
                        size="icon-xs"
                        onClick={() => moveInterfaceUp(index)}
                        disabled={index === 0}
                        aria-label="Move up"
                        title="Move up"
                      >
                        <ChevronUp />
                      </InputGroupButton>
                      <InputGroupButton
                        size="icon-xs"
                        onClick={() => moveInterfaceDown(index)}
                        disabled={index === value.length - 1}
                        aria-label="Move down"
                        title="Move down"
                      >
                        <ChevronDown />
                      </InputGroupButton>
                    </>
                  )}
                  <InputGroupButton
                    size="icon-xs"
                    onClick={() => removeInterface(iface)}
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
                {t('routingRules.dialog.addInterface')}
              </Button>
            </PopoverTrigger>
            <PopoverContent align='start' className="w-100 p-0">
              <Command>
                <CommandInput
                  placeholder={t('routingRules.dialog.searchInterfaces')}
                  value={customInterface}
                  onValueChange={setCustomInterface}
                />
                <CommandList className="max-h-[300px] overflow-y-auto">
                  {customInterface && !interfaceOptions.some(i => i.name === customInterface) && (
                    <CommandItem
                      onSelect={() => addCustomInterface()}
                      className="text-sm"
                    >
                      <Plus className="mr-2 h-4 w-4" />
                      {t('routingRules.dialog.addCustomInterface')}: {customInterface}
                    </CommandItem>
                  )}
                  {interfaceOptions.length === 0 && !customInterface && (
                    <CommandEmpty>{t('routingRules.dialog.noInterfaces')}</CommandEmpty>
                  )}
                  <CommandGroup>
                    {interfaceOptions.map((iface) => (
                      <CommandItem
                        key={iface.name}
                        value={iface.name}
                        onSelect={() => addInterface(iface.name)}
                        disabled={value.includes(iface.name)}
                      >
                        <Check
                          className={cn(
                            "mr-2 h-4 w-4",
                            value.includes(iface.name) ? "opacity-100" : "opacity-0"
                          )}
                        />
                        <InterfaceStatus iface={iface} />
                        <InterfaceName iface={iface} />
                      </CommandItem>
                    ))}
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
              <Network className="h-5 w-5" />
            </EmptyMedia>
            <EmptyTitle className="text-base">{t('routingRules.dialog.emptyInterfaces.title')}</EmptyTitle>
            <EmptyDescription>
              {t('routingRules.dialog.emptyInterfaces.description')}
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
                {t('routingRules.dialog.addInterface')}
              </Button>
            </PopoverTrigger>
            <PopoverContent className="w-full p-0">
              <Command>
                <CommandInput
                  placeholder={t('routingRules.dialog.searchInterfaces')}
                  value={customInterface}
                  onValueChange={setCustomInterface}
                />
                <CommandList className="max-h-[300px] overflow-y-auto">
                  {customInterface && !interfaceOptions.some(i => i.name === customInterface) && (
                    <CommandItem
                      onSelect={() => addCustomInterface()}
                      className="text-sm"
                    >
                      <Plus className="mr-2 h-4 w-4" />
                      {t('routingRules.dialog.addCustomInterface')}: <span className="font-mono ml-1">{customInterface}</span>
                    </CommandItem>
                  )}
                  {interfaceOptions.length === 0 && !customInterface && (
                    <CommandEmpty>{t('routingRules.dialog.noInterfaces')}</CommandEmpty>
                  )}
                  <CommandGroup>
                    {interfaceOptions.map((iface) => (
                      <CommandItem
                        key={iface.name}
                        value={iface.name}
                        onSelect={() => addInterface(iface.name)}
                        disabled={value.includes(iface.name)}
                      >
                        <InterfaceStatus iface={iface} />
                        <InterfaceName iface={iface} />
                      </CommandItem>
                    ))}
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

export function InterfaceName({
  iface,
  defaultName,
}: {
  iface: InterfaceInfo | undefined;
  defaultName?: string;
}) {
  if (iface != null) {
    return (
      <>
        {iface.name}
        {iface.keenetic_description ? (
          <span className="text-primary">({iface.keenetic_description})</span>
        ) : (
          iface.keenetic_id ? (
            <span className="text-muted-foreground">({iface.keenetic_id})</span>
          ) : null
        )}
      </>
    );
  }

  return <span className="text-muted-foreground">{defaultName || "(unknown)"}</span>;
}


export function InterfaceStatus({
  iface,
}: {
  iface: InterfaceInfo | undefined;
}) {
  if (iface == null) {
    return (
      <Unplug className="mr-2 h-4 w-4 text-gray-600" />
    );
  }

  if (!iface.is_up) {
    return (
      <Unplug className="mr-2 h-4 w-4 text-red-600" />
    );
  }

  return (
    <Unplug className="mr-2 h-4 w-4 text-green-600" />
  );
}
