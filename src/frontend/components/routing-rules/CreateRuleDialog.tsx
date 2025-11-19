import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
import { Loader2, X, Plus, Check, ChevronsUpDown } from 'lucide-react';
import { useCreateIPSet } from '../../src/hooks/useIPSets';
import { useInterfaces } from '../../src/hooks/useInterfaces';
import {
  ResponsiveDialog,
  ResponsiveDialogContent,
  ResponsiveDialogDescription,
  ResponsiveDialogFooter,
  ResponsiveDialogHeader,
  ResponsiveDialogTitle,
} from '../ui/responsive-dialog';
import { Field, FieldLabel, FieldDescription, FieldGroup } from '../ui/field';
import { Input } from '../ui/input';
import { Checkbox } from '../ui/checkbox';
import { Button } from '../ui/button';
import { RadioGroup, RadioGroupItem } from '../ui/radio-group';
import { Label } from '../ui/label';
import { Popover, PopoverContent, PopoverTrigger } from '../ui/popover';
import { Command, CommandEmpty, CommandGroup, CommandInput, CommandItem, CommandList } from '../ui/command';
import { cn } from '../../src/lib/utils';
import type { CreateIPSetRequest } from '../../src/api/client';

interface CreateRuleDialogProps {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  availableLists: string[];
}

export function CreateRuleDialog({ open, onOpenChange, availableLists }: CreateRuleDialogProps) {
  const { t } = useTranslation();
  const createIPSet = useCreateIPSet();
  const [interfacesOpen, setInterfacesOpen] = useState(false);
  const [listsOpen, setListsOpen] = useState(false);

  // Fetch interfaces when combobox is opened
  const { data: interfacesData } = useInterfaces(interfacesOpen);

  const [formData, setFormData] = useState<CreateIPSetRequest>({
    ipset_name: '',
    lists: [],
    ip_version: 4,
    flush_before_applying: false,
    routing: {
      interfaces: [],
      kill_switch: true,
      fwmark: 100,
      table: 100,
      priority: 100,
    },
  });

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();

    if (formData.lists.length === 0) {
      toast.error('At least one list must be selected');
      return;
    }

    if (formData.routing && formData.routing.interfaces.length === 0) {
      toast.error('At least one interface must be specified');
      return;
    }

    try {
      await createIPSet.mutateAsync(formData);
      toast.success(`Routing rule "${formData.ipset_name}" created successfully`);
      onOpenChange(false);
      resetForm();
    } catch (error) {
      toast.error(`Failed to create routing rule: ${String(error)}`);
    }
  };

  const resetForm = () => {
    setFormData({
      ipset_name: '',
      lists: [],
      ip_version: 4,
      flush_before_applying: false,
      routing: {
        interfaces: [],
        kill_switch: true,
        fwmark: 100,
        table: 100,
        priority: 100,
      },
    });
    setInterfaceSearch('');
  };

  const addList = (listName: string) => {
    if (!formData.lists.includes(listName)) {
      setFormData({
        ...formData,
        lists: [...formData.lists, listName],
      });
    }
    setListsOpen(false);
  };

  const removeList = (listName: string) => {
    setFormData({
      ...formData,
      lists: formData.lists.filter((l) => l !== listName),
    });
  };

  const addInterface = (iface: string) => {
    if (formData.routing && !formData.routing.interfaces.includes(iface)) {
      setFormData({
        ...formData,
        routing: {
          ...formData.routing,
          interfaces: [...formData.routing.interfaces, iface],
        },
      });
    }
    setInterfacesOpen(false);
  };

  const removeInterface = (iface: string) => {
    if (formData.routing) {
      setFormData({
        ...formData,
        routing: {
          ...formData.routing,
          interfaces: formData.routing.interfaces.filter((i) => i !== iface),
        },
      });
    }
  };

  // Get interface options
  const interfaceOptions = (interfacesData || []).map((i) => i.name);

  return (
    <ResponsiveDialog open={open} onOpenChange={onOpenChange}>
      <ResponsiveDialogContent className="max-w-2xl max-h-[90vh] overflow-y-auto">
        <ResponsiveDialogHeader>
          <ResponsiveDialogTitle>{t('routingRules.newRule')}</ResponsiveDialogTitle>
          <ResponsiveDialogDescription>
            Create a new routing rule for policy-based routing
          </ResponsiveDialogDescription>
        </ResponsiveDialogHeader>

        <form onSubmit={handleSubmit}>
          <FieldGroup>
            {/* Basic Info Section */}
            <div className="space-y-4">
              <h3 className="text-sm font-medium">Basic Information</h3>

              <Field>
                <FieldLabel htmlFor="ipset_name">IPSet Name</FieldLabel>
                <Input
                  id="ipset_name"
                  value={formData.ipset_name}
                  onChange={(e) => setFormData({ ...formData, ipset_name: e.target.value })}
                  placeholder="my_vpn_ipset"
                  pattern="^[a-z][a-z0-9_]*$"
                  required
                />
                <FieldDescription>
                  Cannot be changed after creation
                </FieldDescription>
              </Field>

              <Field>
                <FieldLabel>IP Version</FieldLabel>
                <RadioGroup
                  value={formData.ip_version.toString()}
                  onValueChange={(value) => setFormData({ ...formData, ip_version: parseInt(value) as 4 | 6 })}
                >
                  <div className="flex items-center space-x-4">
                    <div className="flex items-center space-x-2">
                      <RadioGroupItem value="4" id="ipv4" />
                      <Label htmlFor="ipv4">IPv4</Label>
                    </div>
                    <div className="flex items-center space-x-2">
                      <RadioGroupItem value="6" id="ipv6" />
                      <Label htmlFor="ipv6">IPv6</Label>
                    </div>
                  </div>
                </RadioGroup>
              </Field>
            </div>

            {/* Lists Section */}
            <div className="space-y-4">
              <h3 className="text-sm font-medium">Lists</h3>
              <Field>
                <FieldLabel>Select Lists</FieldLabel>
                <FieldDescription>
                  Choose which lists to include in this routing rule
                </FieldDescription>

                <Popover open={listsOpen} onOpenChange={setListsOpen}>
                  <PopoverTrigger asChild>
                    <Button
                      variant="outline"
                      role="combobox"
                      aria-expanded={listsOpen}
                      className="w-full justify-between"
                    >
                      <Plus className="mr-2 h-4 w-4" />
                      Add list
                      <ChevronsUpDown className="ml-2 h-4 w-4 shrink-0 opacity-50" />
                    </Button>
                  </PopoverTrigger>
                  <PopoverContent className="w-full p-0">
                    <Command>
                      <CommandInput placeholder="Search lists..." />
                      <CommandList>
                        <CommandEmpty>No lists found.</CommandEmpty>
                        <CommandGroup>
                          {availableLists.map((list) => (
                            <CommandItem
                              key={list}
                              value={list}
                              onSelect={() => addList(list)}
                              disabled={formData.lists.includes(list)}
                            >
                              <Check
                                className={cn(
                                  "mr-2 h-4 w-4",
                                  formData.lists.includes(list) ? "opacity-100" : "opacity-0"
                                )}
                              />
                              {list}
                            </CommandItem>
                          ))}
                        </CommandGroup>
                      </CommandList>
                    </Command>
                  </PopoverContent>
                </Popover>

                {formData.lists.length > 0 && (
                  <ul className="mt-2 space-y-1 border rounded-md p-2">
                    {formData.lists.map((list) => (
                      <li key={list} className="flex items-center justify-between text-sm py-1 px-2 hover:bg-accent rounded">
                        <span>{list}</span>
                        <X
                          className="h-4 w-4 cursor-pointer text-muted-foreground hover:text-foreground"
                          onClick={() => removeList(list)}
                        />
                      </li>
                    ))}
                  </ul>
                )}
              </Field>
            </div>

            {/* Routing Section */}
            {formData.routing && (
              <div className="space-y-4">
                <h3 className="text-sm font-medium">Routing Configuration</h3>

                <Field>
                  <FieldLabel>Interfaces</FieldLabel>
                  <FieldDescription>
                    Add interfaces in priority order (first = highest priority)
                  </FieldDescription>

                  <Popover open={interfacesOpen} onOpenChange={setInterfacesOpen}>
                    <PopoverTrigger asChild>
                      <Button
                        variant="outline"
                        role="combobox"
                        aria-expanded={interfacesOpen}
                        className="w-full justify-between"
                      >
                        <Plus className="mr-2 h-4 w-4" />
                        Add interface
                        <ChevronsUpDown className="ml-2 h-4 w-4 shrink-0 opacity-50" />
                      </Button>
                    </PopoverTrigger>
                    <PopoverContent className="w-full p-0">
                      <Command>
                        <CommandInput placeholder="Search interfaces..." />
                        <CommandList className="max-h-[200px]">
                          <CommandEmpty>No interfaces found.</CommandEmpty>
                          <CommandGroup>
                            {interfaceOptions.map((iface) => (
                              <CommandItem
                                key={iface}
                                value={iface}
                                onSelect={() => addInterface(iface)}
                                disabled={formData.routing?.interfaces.includes(iface)}
                              >
                                <Check
                                  className={cn(
                                    "mr-2 h-4 w-4",
                                    formData.routing?.interfaces.includes(iface) ? "opacity-100" : "opacity-0"
                                  )}
                                />
                                {iface}
                              </CommandItem>
                            ))}
                          </CommandGroup>
                        </CommandList>
                      </Command>
                    </PopoverContent>
                  </Popover>

                  {formData.routing.interfaces.length > 0 && (
                    <ol className="mt-2 space-y-1 border rounded-md p-2 list-decimal list-inside">
                      {formData.routing.interfaces.map((iface, index) => (
                        <li key={`${iface}-${index}`} className="flex items-center justify-between text-sm py-1 px-2 hover:bg-accent rounded">
                          <span>{iface}</span>
                          <X
                            className="h-4 w-4 cursor-pointer text-muted-foreground hover:text-foreground"
                            onClick={() => removeInterface(iface)}
                          />
                        </li>
                      ))}
                    </ol>
                  )}
                </Field>

                <div className="grid grid-cols-3 gap-4">
                  <Field>
                    <FieldLabel htmlFor="priority">Priority</FieldLabel>
                    <Input
                      id="priority"
                      type="number"
                      value={formData.routing.priority}
                      onChange={(e) => setFormData({
                        ...formData,
                        routing: { ...formData.routing!, priority: parseInt(e.target.value) },
                      })}
                      required
                    />
                  </Field>

                  <Field>
                    <FieldLabel htmlFor="table">Table</FieldLabel>
                    <Input
                      id="table"
                      type="number"
                      value={formData.routing.table}
                      onChange={(e) => setFormData({
                        ...formData,
                        routing: { ...formData.routing!, table: parseInt(e.target.value) },
                      })}
                      required
                    />
                  </Field>

                  <Field>
                    <FieldLabel htmlFor="fwmark">FW Mark</FieldLabel>
                    <Input
                      id="fwmark"
                      type="number"
                      value={formData.routing.fwmark}
                      onChange={(e) => setFormData({
                        ...formData,
                        routing: { ...formData.routing!, fwmark: parseInt(e.target.value) },
                      })}
                      required
                    />
                  </Field>
                </div>

                <Field>
                  <FieldLabel htmlFor="override_dns">DNS Override (Optional)</FieldLabel>
                  <FieldDescription>
                    Format: server#port (e.g., 1.1.1.1#53)
                  </FieldDescription>
                  <Input
                    id="override_dns"
                    value={formData.routing.override_dns || ''}
                    onChange={(e) => setFormData({
                      ...formData,
                      routing: { ...formData.routing!, override_dns: e.target.value || undefined },
                    })}
                    placeholder="1.1.1.1#53"
                  />
                </Field>
              </div>
            )}

            {/* Options Section */}
            <div className="space-y-4">
              <h3 className="text-sm font-medium">Options</h3>

              <div className="flex items-center space-x-2">
                <Checkbox
                  id="flush_before_applying"
                  checked={formData.flush_before_applying}
                  onCheckedChange={(checked) =>
                    setFormData({ ...formData, flush_before_applying: checked as boolean })
                  }
                />
                <label
                  htmlFor="flush_before_applying"
                  className="text-sm font-medium leading-none peer-disabled:cursor-not-allowed peer-disabled:opacity-70"
                >
                  Flush before applying
                </label>
              </div>

              {formData.routing && (
                <div className="flex items-center space-x-2">
                  <Checkbox
                    id="kill_switch"
                    checked={formData.routing.kill_switch}
                    onCheckedChange={(checked) =>
                      setFormData({
                        ...formData,
                        routing: { ...formData.routing!, kill_switch: checked as boolean },
                      })
                    }
                  />
                  <label
                    htmlFor="kill_switch"
                    className="text-sm font-medium leading-none peer-disabled:cursor-not-allowed peer-disabled:opacity-70"
                  >
                    Kill switch (block traffic when all interfaces are down)
                  </label>
                </div>
              )}
            </div>
          </FieldGroup>

          <ResponsiveDialogFooter className="mt-6">
            <Button
              type="button"
              variant="outline"
              onClick={() => onOpenChange(false)}
              disabled={createIPSet.isPending}
            >
              {t('common.cancel')}
            </Button>
            <Button type="submit" disabled={createIPSet.isPending}>
              {createIPSet.isPending && <Loader2 className="mr-2 h-4 w-4 animate-spin" />}
              {t('common.create')}
            </Button>
          </ResponsiveDialogFooter>
        </form>
      </ResponsiveDialogContent>
    </ResponsiveDialog>
  );
}
