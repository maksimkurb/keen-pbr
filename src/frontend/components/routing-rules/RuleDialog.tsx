import { useState, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
import { Loader2, X, Plus, Check, ChevronsUpDown, ChevronUp, ChevronDown, Unplug, ListPlus, Network } from 'lucide-react';
import { useCreateIPSet, useUpdateIPSet } from '../../src/hooks/useIPSets';
import { useLists } from '../../src/hooks/useLists';
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
import { Accordion, AccordionContent, AccordionItem, AccordionTrigger } from '../ui/accordion';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '../ui/select';
import { Textarea } from '../ui/textarea';
import { Empty, EmptyHeader, EmptyMedia, EmptyTitle, EmptyDescription } from '../ui/empty';
import { cn } from '../../src/lib/utils';
import type { IPSetConfig, CreateIPSetRequest, IPTablesRule } from '../../src/api/client';

interface RuleDialogProps {
  ipset?: IPSetConfig | null;
  open: boolean;
  onOpenChange: (open: boolean) => void;
  availableLists: string[];
}

// Default iptables rule
const DEFAULT_IPTABLES_RULE: IPTablesRule = {
  chain: 'PREROUTING',
  table: 'mangle',
  rule: ['-m', 'mark', '--mark', '0x0/0xffffffff', '-m', 'set', '--match-set', '{{ipset_name}}', 'dst,src', '-j', 'MARK', '--set-mark', '{{fwmark}}'],
};

// Available template variables
const TEMPLATE_VARS = ['{{ipset_name}}', '{{fwmark}}', '{{table}}', '{{priority}}'];

export function RuleDialog({ ipset, open, onOpenChange, availableLists }: RuleDialogProps) {
  const { t } = useTranslation();
  const isEditMode = !!ipset;
  const createIPSet = useCreateIPSet();
  const updateIPSet = useUpdateIPSet();
  const [interfacesOpen, setInterfacesOpen] = useState(false);
  const [listsOpen, setListsOpen] = useState(false);
  const [customInterface, setCustomInterface] = useState('');

  // Fetch and refresh interfaces while dialog is open (cached for 5s, auto-refresh every 5s)
  const { data: interfacesData } = useInterfaces(open);

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
    iptables_rule: [{ ...DEFAULT_IPTABLES_RULE }],
  });

  // Initialize form when ipset changes (edit mode) or reset (create mode)
  useEffect(() => {
    if (ipset) {
      setFormData({
        ipset_name: ipset.ipset_name,
        lists: ipset.lists,
        ip_version: ipset.ip_version,
        flush_before_applying: ipset.flush_before_applying,
        routing: ipset.routing || {
          interfaces: [],
          kill_switch: true,
          fwmark: 100,
          table: 100,
          priority: 100,
        },
        iptables_rule: ipset.iptables_rule && ipset.iptables_rule.length > 0
          ? ipset.iptables_rule
          : [{ ...DEFAULT_IPTABLES_RULE }],
      });
    } else if (!open) {
      // Reset form when dialog closes in create mode
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
        iptables_rule: [{ ...DEFAULT_IPTABLES_RULE }],
      });
      setCustomInterface('');
    }
  }, [ipset, open]);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();

    // At least one list is required
    if (formData.lists.length === 0) {
      toast.error(t('routingRules.dialog.selectListsError'));
      return;
    }

    // Interfaces are optional - no validation needed

    try {
      if (isEditMode) {
        await updateIPSet.mutateAsync({ name: ipset.ipset_name, data: formData });
        toast.success(t('routingRules.dialog.updateSuccess', { name: formData.ipset_name }));
      } else {
        await createIPSet.mutateAsync(formData);
        toast.success(t('routingRules.dialog.createSuccess', { name: formData.ipset_name }));
      }
      onOpenChange(false);
    } catch (error) {
      toast.error(t('routingRules.dialog.saveError', { action: isEditMode ? t('common.update') : t('common.create').toLowerCase(), error: String(error) }));
    }
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

  const addCustomInterface = () => {
    const trimmedInterface = customInterface.trim();
    if (trimmedInterface && formData.routing && !formData.routing.interfaces.includes(trimmedInterface)) {
      setFormData({
        ...formData,
        routing: {
          ...formData.routing,
          interfaces: [...formData.routing.interfaces, trimmedInterface],
        },
      });
      setCustomInterface('');
      setInterfacesOpen(false);
    }
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

  const moveInterfaceUp = (index: number) => {
    if (index > 0 && formData.routing) {
      const newInterfaces = [...formData.routing.interfaces];
      [newInterfaces[index - 1], newInterfaces[index]] = [newInterfaces[index], newInterfaces[index - 1]];
      setFormData({
        ...formData,
        routing: {
          ...formData.routing,
          interfaces: newInterfaces,
        },
      });
    }
  };

  const moveInterfaceDown = (index: number) => {
    if (formData.routing && index < formData.routing.interfaces.length - 1) {
      const newInterfaces = [...formData.routing.interfaces];
      [newInterfaces[index], newInterfaces[index + 1]] = [newInterfaces[index + 1], newInterfaces[index]];
      setFormData({
        ...formData,
        routing: {
          ...formData.routing,
          interfaces: newInterfaces,
        },
      });
    }
  };

  // IPTables Rules functions
  const addIPTablesRule = () => {
    setFormData({
      ...formData,
      iptables_rule: [
        ...(formData.iptables_rule || []),
        { chain: '', table: '', rule: [] },
      ],
    });
  };

  const removeIPTablesRule = (index: number) => {
    if (formData.iptables_rule && formData.iptables_rule.length > 1) {
      setFormData({
        ...formData,
        iptables_rule: formData.iptables_rule.filter((_, i) => i !== index),
      });
    }
  };

  const updateIPTablesRule = (index: number, field: keyof IPTablesRule, value: string | string[]) => {
    if (!formData.iptables_rule) return;

    const newRules = [...formData.iptables_rule];
    newRules[index] = {
      ...newRules[index],
      [field]: value,
    };
    setFormData({
      ...formData,
      iptables_rule: newRules,
    });
  };

  const insertTemplateVar = (ruleIndex: number, templateVar: string) => {
    if (!formData.iptables_rule) return;

    const currentRule = formData.iptables_rule[ruleIndex];
    const ruleString = currentRule.rule.join(' ');
    const newRuleString = ruleString ? `${ruleString} ${templateVar}` : templateVar;

    updateIPTablesRule(ruleIndex, 'rule', newRuleString.split(' '));
  };

  // Get interface options (keep full objects for UP/DOWN state)
  const interfaceOptions = interfacesData || [];

  const isPending = isEditMode ? updateIPSet.isPending : createIPSet.isPending;

  return (
    <ResponsiveDialog open={open} onOpenChange={onOpenChange}>
      <ResponsiveDialogContent className="max-w-3xl">
        <ResponsiveDialogHeader>
          <ResponsiveDialogTitle>
            {isEditMode ? t('routingRules.dialog.editTitle') : t('routingRules.dialog.createTitle')}
          </ResponsiveDialogTitle>
          <ResponsiveDialogDescription>
            {isEditMode ? t('routingRules.dialog.editDescription') : t('routingRules.dialog.createDescription')}
          </ResponsiveDialogDescription>
        </ResponsiveDialogHeader>

        <form onSubmit={handleSubmit}>
          <FieldGroup>
            {/* Basic Info Section */}
            <div className="space-y-4">
              <h3 className="text-sm font-medium">{t('routingRules.dialog.basicInfo')}</h3>

              <Field>
                <FieldLabel htmlFor="ipset_name">{t('routingRules.dialog.ipsetName')}</FieldLabel>
                <Input
                  id="ipset_name"
                  value={formData.ipset_name}
                  onChange={(e) => setFormData({ ...formData, ipset_name: e.target.value })}
                  placeholder={t('routingRules.dialog.ipsetNamePlaceholder')}
                  pattern="^[a-z][a-z0-9_]*$"
                  disabled={isEditMode}
                  required
                />
                <FieldDescription>
                  {t('routingRules.dialog.ipsetNameDescriptionLocked')}
                </FieldDescription>
              </Field>

              <Field>
                <FieldLabel>{t('routingRules.dialog.ipVersion')}</FieldLabel>
                <RadioGroup
                  value={formData.ip_version.toString()}
                  onValueChange={(value) => setFormData({ ...formData, ip_version: parseInt(value) as 4 | 6 })}
                >
                  <div className="flex items-center space-x-4">
                    <div className="flex items-center space-x-2">
                      <RadioGroupItem value="4" id="ipv4" />
                      <Label htmlFor="ipv4">{t('routingRules.dialog.ipv4')}</Label>
                    </div>
                    <div className="flex items-center space-x-2">
                      <RadioGroupItem value="6" id="ipv6" />
                      <Label htmlFor="ipv6">{t('routingRules.dialog.ipv6')}</Label>
                    </div>
                  </div>
                </RadioGroup>
              </Field>
            </div>

            {/* Lists Section */}
            <div className="space-y-4">
              <h3 className="text-sm font-medium">{t('routingRules.dialog.lists')}</h3>
              <Field>
                <FieldLabel>{t('routingRules.dialog.selectLists')}</FieldLabel>
                <FieldDescription>
                  {t('routingRules.dialog.selectListsDescription')}
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
                      {t('routingRules.dialog.addList')}
                      <ChevronsUpDown className="ml-2 h-4 w-4 shrink-0 opacity-50" />
                    </Button>
                  </PopoverTrigger>
                  <PopoverContent className="w-full p-0">
                    <Command>
                      <CommandInput placeholder={t('routingRules.dialog.searchLists')} />
                      <CommandList>
                        <CommandEmpty>{t('routingRules.dialog.noLists')}</CommandEmpty>
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

                {formData.lists.length > 0 ? (
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
                ) : (
                  <Empty className="mt-2 border">
                    <EmptyHeader>
                      <EmptyMedia variant="icon">
                        <ListPlus className="h-5 w-5" />
                      </EmptyMedia>
                      <EmptyTitle className="text-base">No lists selected</EmptyTitle>
                      <EmptyDescription>
                        Click "Add list" above to select at least one list for this routing rule.
                      </EmptyDescription>
                    </EmptyHeader>
                  </Empty>
                )}
              </Field>
            </div>

            {/* Routing Section */}
            {formData.routing && (
              <div className="space-y-4">
                <h3 className="text-sm font-medium">{t('routingRules.dialog.routingConfig')}</h3>

                <Field>
                  <FieldLabel>{t('routingRules.dialog.interfaces')}</FieldLabel>
                  <FieldDescription>
                    {t('routingRules.dialog.interfacesDescription')}
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
                        <CommandList className="max-h-[200px]">
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
                                disabled={formData.routing?.interfaces.includes(iface.name)}
                              >
                                {iface.is_up ? (
                                  <Unplug className="mr-2 h-4 w-4 text-green-600" />
                                ) : (
                                  <Unplug className="mr-2 h-4 w-4 text-red-600" />
                                )}
                                <Check
                                  className={cn(
                                    "mr-2 h-4 w-4",
                                    formData.routing?.interfaces.includes(iface.name) ? "opacity-100" : "opacity-0"
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

                  {formData.routing.interfaces.length > 0 ? (
                    <div className="mt-2 space-y-1 border rounded-md p-2">
                      {formData.routing.interfaces.map((iface, index) => {
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
                              disabled={index === formData.routing!.interfaces.length - 1}
                            >
                              <ChevronDown className="h-3 w-3" />
                            </Button>
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
                      )})}
                    </div>
                  ) : (
                    <Empty className="mt-2 border">
                      <EmptyHeader>
                        <EmptyMedia variant="icon">
                          <Network className="h-5 w-5" />
                        </EmptyMedia>
                        <EmptyTitle className="text-base">No interfaces selected</EmptyTitle>
                        <EmptyDescription>
                          Click "Add interface" above to select interfaces for routing (optional).
                        </EmptyDescription>
                      </EmptyHeader>
                    </Empty>
                  )}
                </Field>

                <div className="grid grid-cols-3 gap-4">
                  <Field>
                    <FieldLabel htmlFor="priority">{t('routingRules.dialog.priority')}</FieldLabel>
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
                    <FieldLabel htmlFor="table">{t('routingRules.dialog.table')}</FieldLabel>
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
                    <FieldLabel htmlFor="fwmark">{t('routingRules.dialog.fwMark')}</FieldLabel>
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
                  <FieldLabel htmlFor="override_dns">{t('routingRules.dialog.dnsOverride')}</FieldLabel>
                  <FieldDescription>
                    {t('routingRules.dialog.dnsOverrideDescription')}
                  </FieldDescription>
                  <Input
                    id="override_dns"
                    value={formData.routing.override_dns || ''}
                    onChange={(e) => setFormData({
                      ...formData,
                      routing: { ...formData.routing!, override_dns: e.target.value || undefined },
                    })}
                    placeholder={t('routingRules.dialog.dnsOverridePlaceholder')}
                  />
                </Field>
              </div>
            )}

            {/* IPTables Rules Section */}
            <Accordion type="single" collapsible className="border rounded-md">
              <AccordionItem value="iptables" className="border-0">
                <AccordionTrigger className="px-4 hover:no-underline">
                  <h3 className="text-sm font-medium">{t('routingRules.dialog.iptablesTitle')}</h3>
                </AccordionTrigger>
                <AccordionContent className="px-4 pb-4">
                  <div className="space-y-4">
                    <p className="text-sm text-muted-foreground">
                      {t('routingRules.dialog.iptablesDescription', { vars: TEMPLATE_VARS.join(', ') })}
                    </p>

                    {formData.iptables_rule?.map((rule, index) => (
                      <div key={index} className="border rounded-md p-4 space-y-3">
                        <div className="flex items-center justify-between mb-2">
                          <span className="text-sm font-medium">{t('routingRules.dialog.iptablesRuleNumber', { number: index + 1 })}</span>
                          {formData.iptables_rule && formData.iptables_rule.length > 1 && (
                            <Button
                              type="button"
                              variant="ghost"
                              size="sm"
                              onClick={() => removeIPTablesRule(index)}
                            >
                              <X className="h-4 w-4" />
                            </Button>
                          )}
                        </div>

                        <div className="grid grid-cols-2 gap-3">
                          <Field>
                            <FieldLabel>{t('routingRules.dialog.iptablesChain')}</FieldLabel>
                            <Input
                              value={rule.chain}
                              onChange={(e) => updateIPTablesRule(index, 'chain', e.target.value)}
                              placeholder={t('routingRules.dialog.iptablesChainPlaceholder')}
                            />
                          </Field>

                          <Field>
                            <FieldLabel>{t('routingRules.dialog.iptablesTable')}</FieldLabel>
                            <Input
                              value={rule.table}
                              onChange={(e) => updateIPTablesRule(index, 'table', e.target.value)}
                              placeholder={t('routingRules.dialog.iptablesTablePlaceholder')}
                            />
                          </Field>
                        </div>

                        <Field>
                          <div className="flex items-center justify-between mb-2">
                            <FieldLabel>{t('routingRules.dialog.iptablesRuleArguments')}</FieldLabel>
                            <Select
                              key={`select-${index}-${rule.rule.join(' ')}`}
                              onValueChange={(value) => insertTemplateVar(index, value)}
                            >
                              <SelectTrigger className="w-[180px] h-8">
                                <SelectValue placeholder={t('routingRules.dialog.iptablesInsertVariable')} />
                              </SelectTrigger>
                              <SelectContent>
                                {TEMPLATE_VARS.map((varName) => (
                                  <SelectItem key={varName} value={varName}>
                                    <code className="text-xs">{varName}</code>
                                  </SelectItem>
                                ))}
                              </SelectContent>
                            </Select>
                          </div>
                          <Textarea
                            value={rule.rule.join(' ')}
                            onChange={(e) => updateIPTablesRule(index, 'rule', e.target.value.split(' '))}
                            placeholder="-m mark --mark 0x0/0xffffffff -m set --match-set {{ipset_name}} dst,src -j MARK --set-mark {{fwmark}}"
                            rows={3}
                            className="font-mono text-xs"
                          />
                          <FieldDescription className="text-xs">
                            {t('routingRules.dialog.iptablesRuleDescription')}
                          </FieldDescription>
                        </Field>
                      </div>
                    ))}

                    <Button
                      type="button"
                      variant="outline"
                      size="sm"
                      onClick={addIPTablesRule}
                      className="w-full"
                    >
                      <Plus className="mr-2 h-4 w-4" />
                      {t('routingRules.dialog.iptablesAddRule')}
                    </Button>
                  </div>
                </AccordionContent>
              </AccordionItem>
            </Accordion>

            {/* Options Section */}
            <div className="space-y-4">
              <h3 className="text-sm font-medium">{t('routingRules.dialog.options')}</h3>

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
                  {t('routingRules.dialog.flushBeforeApplying')}
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
                    {t('routingRules.dialog.killSwitch')}
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
              disabled={isPending}
            >
              {t('common.cancel')}
            </Button>
            <Button type="submit" disabled={isPending}>
              {isPending && <Loader2 className="mr-2 h-4 w-4 animate-spin" />}
              {isEditMode ? t('common.save') : t('common.create')}
            </Button>
          </ResponsiveDialogFooter>
        </form>
      </ResponsiveDialogContent>
    </ResponsiveDialog>
  );
}
