import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Plus, X, ChevronUp, ChevronDown, Unplug, Network, Check, ChevronsUpDown } from 'lucide-react';
import { useInterfaces } from '../../src/hooks/useInterfaces';
import { Popover, PopoverContent, PopoverTrigger } from './popover';
import { Command, CommandEmpty, CommandGroup, CommandInput, CommandItem, CommandList } from './command';
import { Button } from './button';
import { Empty, EmptyHeader, EmptyMedia, EmptyTitle, EmptyDescription } from './empty';
import { cn } from '../../src/lib/utils';

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
    <div className={className}>
      <Popover open={open} onOpenChange={setOpen}>
        <PopoverTrigger asChild>
          <Button
            variant="outline"
            role="combobox"
            aria-expanded={open}
            className="w-full justify-between"
          >
            <Plus className="mr-2 h-4 w-4" />
            {t('routingRules.dialog.addInterface')}
            <ChevronsUpDown className="ml-2 h-4 w-4 shrink-0 opacity-50" />
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
                  Add custom: <span className="font-mono ml-1">{customInterface}</span>
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
                    {iface.is_up ? (
                      <Unplug className="mr-2 h-4 w-4 text-green-600" />
                    ) : (
                      <Unplug className="mr-2 h-4 w-4 text-red-600" />
                    )}
                    <Check
                      className={cn(
                        "mr-2 h-4 w-4",
                        value.includes(iface.name) ? "opacity-100" : "opacity-0"
                      )}
                    />
                    {iface.name}
                  </CommandItem>
                ))}
              </CommandGroup>
            </CommandList>
          </Command>
        </PopoverContent>
      </Popover>

      {value.length > 0 ? (
        <div className="mt-2 space-y-1 border rounded-md p-2">
          {value.map((iface, index) => {
            const interfaceInfo = interfaceOptions.find(i => i.name === iface);
            return (
              <div key={`${iface}-${index}`} className="flex items-center justify-between text-sm py-1 px-2 hover:bg-accent rounded">
                <div className="flex items-center gap-2">
                  <span className="text-xs text-muted-foreground">{index + 1}.</span>
                  {interfaceInfo?.is_up ? (
                    <Unplug className="h-4 w-4 text-green-600" />
                  ) : interfaceInfo ? (
                    <Unplug className="h-4 w-4 text-red-600" />
                  ) : null}
                  <span className="font-mono">{iface}</span>
                </div>
                <div className="flex items-center gap-1">
                  {allowReorder && (
                    <>
                      <Button
                        type="button"
                        variant="ghost"
                        size="sm"
                        className="h-6 w-6 p-0"
                        onClick={() => moveInterfaceUp(index)}
                        disabled={index === 0}
                      >
                        <ChevronUp className="h-3 w-3" />
                      </Button>
                      <Button
                        type="button"
                        variant="ghost"
                        size="sm"
                        className="h-6 w-6 p-0"
                        onClick={() => moveInterfaceDown(index)}
                        disabled={index === value.length - 1}
                      >
                        <ChevronDown className="h-3 w-3" />
                      </Button>
                    </>
                  )}
                  <Button
                    type="button"
                    variant="ghost"
                    size="sm"
                    className="h-6 w-6 p-0"
                    onClick={() => removeInterface(iface)}
                  >
                    <X className="h-3 w-3" />
                  </Button>
                </div>
              </div>
            );
          })}
        </div>
      ) : (
        <Empty className="mt-2 border">
          <EmptyHeader>
            <EmptyMedia variant="icon">
              <Network className="h-5 w-5" />
            </EmptyMedia>
            <EmptyTitle className="text-base">{t('routingRules.dialog.emptyInterfaces.title')}</EmptyTitle>
            <EmptyDescription>
              {t('routingRules.dialog.emptyInterfaces.description')}
            </EmptyDescription>
          </EmptyHeader>
        </Empty>
      )}
    </div>
  );
}
