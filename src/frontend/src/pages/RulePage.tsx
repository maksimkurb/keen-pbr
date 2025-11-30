import { Plus, X } from 'lucide-react';
import { useMemo, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useNavigate, useParams } from 'react-router-dom';
import { toast } from 'sonner';
import {
  Accordion,
  AccordionContent,
  AccordionItem,
  AccordionTrigger,
} from '../../components/ui/accordion';
import { BaseFormPage } from '../../components/ui/base-form-page';
import { Button } from '../../components/ui/button';
import { Checkbox } from '../../components/ui/checkbox';
import {
  Field,
  FieldDescription,
  FieldError,
  FieldGroup,
  FieldLabel,
} from '../../components/ui/field';
import { Input } from '../../components/ui/input';
import { InterfaceSelector } from '../../components/ui/interface-selector';
import { Label } from '../../components/ui/label';
import { ListSelector } from '../../components/ui/list-selector';
import { RadioGroup, RadioGroupItem } from '../../components/ui/radio-group';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '../../components/ui/select';
import { StringArrayInput } from '../../components/ui/string-array-input';
import { Textarea } from '../../components/ui/textarea';
import type { IPSetConfig, IPTablesRule } from '../api/client';
import { KeenPBRAPIError } from '../api/client';
import { useCreateIPSet, useIPSets, useUpdateIPSet } from '../hooks/useIPSets';
import { useLists } from '../hooks/useLists';
import { getFieldError, mapValidationErrors } from '../utils/formValidation';

// Default iptables rule
const DEFAULT_IPTABLES_RULE: IPTablesRule = {
  chain: 'PREROUTING',
  table: 'mangle',
  rule: [
    '-m',
    'mark',
    '--mark',
    '0x0/0xffffffff',
    '-m',
    'set',
    '--match-set',
    '{{ipset_name}}',
    'dst,src',
    '-j',
    'MARK',
    '--set-mark',
    '{{fwmark}}',
  ],
};

// Available template variables
const TEMPLATE_VARS = [
  '{{ipset_name}}',
  '{{fwmark}}',
  '{{table}}',
  '{{priority}}',
];

export default function RulePage() {
  const { t } = useTranslation();
  const navigate = useNavigate();
  const { name } = useParams<{ name: string }>();
  const isEditMode = !!name;

  const createIPSet = useCreateIPSet();
  const updateIPSet = useUpdateIPSet();
  const { data: ipsets, isLoading: isLoadingIPSets } = useIPSets();
  const { data: lists } = useLists();

  const availableLists = lists || [];

  // Validation errors from API
  const [validationErrors, setValidationErrors] = useState<
    Record<string, string>
  >({});

  // Determine initial data for edit mode
  const initialData: IPSetConfig | undefined = useMemo(() => {
    if (!isEditMode || !ipsets) return undefined;

    const ipset = ipsets.find((i) => i.ipset_name === name);
    if (!ipset) return undefined;

    const routing = ipset.routing || {
      interfaces: [],
      kill_switch: true,
      fwmark: 100,
      table: 100,
      priority: 100,
    };

    return {
      ipset_name: ipset.ipset_name,
      lists: ipset.lists,
      ip_version: ipset.ip_version,
      flush_before_applying: ipset.flush_before_applying,
      routing,
      iptables_rule:
        ipset.iptables_rule && ipset.iptables_rule.length > 0
          ? ipset.iptables_rule
          : [{ ...DEFAULT_IPTABLES_RULE }],
    };
  }, [isEditMode, ipsets, name]);

  const handleSubmit = async (formData: IPSetConfig) => {
    // At least one list is required
    if (formData.lists.length === 0) {
      toast.error(t('routingRules.dialog.selectListsError'));
      throw new Error('Validation failed');
    }

    try {
      if (isEditMode) {
        if (!name) throw new Error('Name is required for update');
        await updateIPSet.mutateAsync({ name, data: formData });
        toast.success(
          t('routingRules.dialog.updateSuccess', { name: formData.ipset_name }),
        );
      } else {
        await createIPSet.mutateAsync(formData);
        toast.success(
          t('routingRules.dialog.createSuccess', { name: formData.ipset_name }),
        );
      }
      setValidationErrors({});
      navigate('/routing-rules');
    } catch (error) {
      if (error instanceof KeenPBRAPIError) {
        const errors = error.getValidationErrors();
        if (errors) {
          setValidationErrors(mapValidationErrors(errors));
          toast.error(t('routingRules.dialog.validationError'));
          throw error;
        }
      }
      toast.error(
        t('routingRules.dialog.saveError', {
          action: isEditMode
            ? t('common.update')
            : t('common.create').toLowerCase(),
        }),
      );
      throw error;
    }
  };

  const isPending = isEditMode ? updateIPSet.isPending : createIPSet.isPending;

  const defaultData: IPSetConfig = {
    ipset_name: '',
    lists: [],
    ip_version: 4,
    flush_before_applying: false,
    routing: {
      interfaces: [],
      kill_switch: true,
      fwmark: 0,
      table: 0,
      priority: 0,
    },
    iptables_rule: [{ ...DEFAULT_IPTABLES_RULE }],
  };

  return (
    <BaseFormPage
      title={
        isEditMode
          ? t('routingRules.dialog.editTitle')
          : t('routingRules.dialog.createTitle')
      }
      description={
        isEditMode
          ? t('routingRules.dialog.editDescription')
          : t('routingRules.dialog.createDescription')
      }
      backPath="/routing-rules"
      isEditMode={isEditMode}
      initialData={initialData}
      isLoading={isLoadingIPSets}
      isPending={isPending}
      onSubmit={handleSubmit}
      defaultData={defaultData}
      canSubmit={(formData) => formData.lists.length > 0}
    >
      {(formData, setFormData) => {
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
              iptables_rule: formData.iptables_rule.filter(
                (_, i) => i !== index,
              ),
            });
          }
        };

        const updateIPTablesRule = (
          index: number,
          field: keyof IPTablesRule,
          value: string | string[],
        ) => {
          if (!formData.iptables_rule) return;

          const newRules = [...formData.iptables_rule];
          const currentRule = newRules[index];
          if (!currentRule) return;

          newRules[index] = {
            ...currentRule,
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
          if (!currentRule) return;

          const ruleString = currentRule.rule.join(' ');
          const newRuleString = ruleString
            ? `${ruleString} ${templateVar}`
            : templateVar;

          updateIPTablesRule(ruleIndex, 'rule', newRuleString.split(' '));
        };

        return (
          <FieldGroup>
            {/* Basic Info Section */}
            <div className="space-y-4">
              <h3 className="text-lg font-medium">
                {t('routingRules.dialog.basicInfo')}
              </h3>

              <Field>
                <FieldLabel htmlFor="ipset_name">
                  {t('routingRules.dialog.ipsetName')}
                </FieldLabel>
                <Input
                  id="ipset_name"
                  value={formData.ipset_name}
                  onChange={(e) =>
                    setFormData({ ...formData, ipset_name: e.target.value })
                  }
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
                  onValueChange={(value) =>
                    setFormData({
                      ...formData,
                      ip_version: parseInt(value) as 4 | 6,
                    })
                  }
                >
                  <div className="flex items-center space-x-4">
                    <div className="flex items-center space-x-2">
                      <RadioGroupItem value="4" id="ipv4" />
                      <Label htmlFor="ipv4">
                        {t('routingRules.dialog.ipv4')}
                      </Label>
                    </div>
                    <div className="flex items-center space-x-2">
                      <RadioGroupItem value="6" id="ipv6" />
                      <Label htmlFor="ipv6">
                        {t('routingRules.dialog.ipv6')}
                      </Label>
                    </div>
                  </div>
                </RadioGroup>
              </Field>
            </div>

            {/* Lists Section */}
            <div className="space-y-4">
              <h3 className="text-lg font-medium">
                {t('routingRules.dialog.lists')}
              </h3>
              <Field>
                <FieldLabel>{t('routingRules.dialog.selectLists')}</FieldLabel>
                <FieldDescription>
                  {t('routingRules.dialog.selectListsDescription')}
                </FieldDescription>

                <ListSelector
                  value={formData.lists}
                  onChange={(lists) => setFormData({ ...formData, lists })}
                  availableLists={availableLists}
                />
              </Field>
            </div>

            {/* Routing Section */}
            {formData.routing && (
              <div className="space-y-4">
                <h3 className="text-lg font-medium">
                  {t('routingRules.dialog.routingConfig')}
                </h3>

                <Field
                  data-invalid={
                    !!getFieldError('routing.interfaces', validationErrors)
                  }
                >
                  <FieldLabel>{t('routingRules.dialog.interfaces')}</FieldLabel>
                  <FieldDescription>
                    {t('routingRules.dialog.interfacesDescription')}
                  </FieldDescription>

                  <InterfaceSelector
                    value={formData.routing.interfaces}
                    onChange={(interfaces) =>
                      setFormData({
                        ...formData,
                        routing: formData.routing
                          ? {
                              ...formData.routing,
                              interfaces,
                            }
                          : undefined,
                      })
                    }
                    allowReorder={true}
                  />
                  {getFieldError('routing.interfaces', validationErrors) && (
                    <FieldError>
                      {getFieldError('routing.interfaces', validationErrors)}
                    </FieldError>
                  )}
                </Field>

                <Field>
                  <FieldLabel htmlFor="default_gateway">
                    {t('routingRules.dialog.defaultGateway')}
                  </FieldLabel>
                  <FieldDescription>
                    {t('routingRules.dialog.defaultGatewayDescription')}
                  </FieldDescription>
                  <Input
                    id="default_gateway"
                    value={formData.routing.default_gateway || ''}
                    onChange={(e) =>
                      setFormData({
                        ...formData,
                        routing: formData.routing
                          ? {
                              ...formData.routing,
                              default_gateway: e.target.value || undefined,
                            }
                          : undefined,
                      })
                    }
                    placeholder={t(
                      'routingRules.dialog.defaultGatewayPlaceholder',
                    )}
                  />
                </Field>

                <Field>
                  <FieldLabel>
                    {t('routingRules.dialog.dnsOverride')}
                  </FieldLabel>
                  <FieldDescription>
                    {t('routingRules.dialog.dnsOverrideDescription')}
                    <ul className="list-disc list-inside mt-1 space-y-0.5">
                      {(
                        t('settings.dnsFormats', {
                          returnObjects: true,
                        }) as string[]
                      ).map((format, index) => (
                        <li key={index}>
                          <code>{format}</code>
                        </li>
                      ))}
                    </ul>
                    <div className="mt-2">
                      {t('settings.dnsUpstreamAdditionalInfo')}
                    </div>
                  </FieldDescription>
                  <StringArrayInput
                    value={formData.routing.dns?.upstreams || []}
                    onChange={(upstreams) =>
                      setFormData({
                        ...formData,
                        routing: formData.routing
                          ? {
                              ...formData.routing,
                              dns:
                                upstreams.length > 0
                                  ? { upstreams }
                                  : undefined,
                            }
                          : undefined,
                      })
                    }
                    placeholder={t(
                      'routingRules.dialog.dnsOverridePlaceholder',
                    )}
                    minItems={0}
                    addButtonLabel={t('settings.addUpstream')}
                  />
                </Field>
              </div>
            )}

            {/* Advanced Configuration Section */}
            <div className="space-y-4">
              <h3 className="text-lg font-medium">
                {t('routingRules.dialog.advancedConfiguration', {
                  defaultValue: 'Advanced Configuration',
                })}
              </h3>

              {/* IPTables Rules Accordion */}
              <Accordion
                type="single"
                collapsible
                className="border rounded-md"
              >
                <AccordionItem value="iptables" className="border-0">
                  <AccordionTrigger className="px-4 hover:no-underline">
                    <h4 className="text-sm font-medium">
                      {t('routingRules.dialog.iptablesTitle')}
                    </h4>
                  </AccordionTrigger>
                  <AccordionContent className="px-4 pb-4">
                    <div className="space-y-4">
                      <p className="text-sm text-muted-foreground">
                        {t('routingRules.dialog.iptablesDescription', {
                          vars: TEMPLATE_VARS.join(', '),
                        })}
                      </p>

                      {formData.iptables_rule
                        ?.filter((rule): rule is IPTablesRule => rule !== null)
                        .map((rule, index) => (
                          <div
                            key={index}
                            className="border rounded-md p-4 space-y-3"
                          >
                            <div className="flex items-center justify-between mb-2">
                              <span className="text-sm font-medium">
                                {t('routingRules.dialog.iptablesRuleNumber', {
                                  number: index + 1,
                                })}
                              </span>
                              {formData.iptables_rule &&
                                formData.iptables_rule.length > 1 && (
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
                                <FieldLabel>
                                  {t('routingRules.dialog.iptablesChain')}
                                </FieldLabel>
                                <Input
                                  value={rule.chain}
                                  onChange={(e) =>
                                    updateIPTablesRule(
                                      index,
                                      'chain',
                                      e.target.value,
                                    )
                                  }
                                  placeholder={t(
                                    'routingRules.dialog.iptablesChainPlaceholder',
                                  )}
                                />
                              </Field>

                              <Field>
                                <FieldLabel>
                                  {t('routingRules.dialog.iptablesTable')}
                                </FieldLabel>
                                <Input
                                  value={rule.table}
                                  onChange={(e) =>
                                    updateIPTablesRule(
                                      index,
                                      'table',
                                      e.target.value,
                                    )
                                  }
                                  placeholder={t(
                                    'routingRules.dialog.iptablesTablePlaceholder',
                                  )}
                                />
                              </Field>
                            </div>

                            <Field>
                              <div className="flex items-center justify-between mb-2">
                                <FieldLabel>
                                  {t(
                                    'routingRules.dialog.iptablesRuleArguments',
                                  )}
                                </FieldLabel>
                                <Select
                                  key={`select-${index}-${rule.rule.join(' ')}`}
                                  onValueChange={(value) =>
                                    insertTemplateVar(index, value)
                                  }
                                >
                                  <SelectTrigger className="w-[180px] h-8">
                                    <SelectValue
                                      placeholder={t(
                                        'routingRules.dialog.iptablesInsertVariable',
                                      )}
                                    />
                                  </SelectTrigger>
                                  <SelectContent>
                                    {TEMPLATE_VARS.map((varName) => (
                                      <SelectItem key={varName} value={varName}>
                                        <code className="text-xs">
                                          {varName}
                                        </code>
                                      </SelectItem>
                                    ))}
                                  </SelectContent>
                                </Select>
                              </div>
                              <Textarea
                                value={rule.rule.join(' ')}
                                onChange={(e) =>
                                  updateIPTablesRule(
                                    index,
                                    'rule',
                                    e.target.value.split(' '),
                                  )
                                }
                                placeholder="-m mark --mark 0x0/0xffffffff -m set --match-set {{ipset_name}} dst,src -j MARK --set-mark {{fwmark}}"
                                rows={3}
                                className="font-mono text-xs"
                              />
                              <FieldDescription className="text-xs">
                                {t(
                                  'routingRules.dialog.iptablesRuleDescription',
                                )}
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

              {/* Priority/Table/FwMark Accordion */}
              <Accordion
                type="single"
                collapsible
                className="border rounded-md"
              >
                <AccordionItem value="advanced-routing" className="border-0">
                  <AccordionTrigger className="px-4 hover:no-underline">
                    <h4 className="text-sm font-medium">
                      {t('routingRules.dialog.advancedRouting', {
                        defaultValue: 'Priority / Table / FwMark',
                      })}
                    </h4>
                  </AccordionTrigger>
                  <AccordionContent className="px-4 pb-4">
                    <div className="space-y-4">
                      <p className="text-sm text-muted-foreground">
                        {t('routingRules.dialog.advancedRoutingDescription', {
                          defaultValue:
                            'Configure priority, table, and fwmark. Leave empty to auto-assign values (500-1000 range).',
                        })}
                      </p>

                      <div className="grid grid-cols-3 gap-4">
                        <Field>
                          <FieldLabel htmlFor="priority">
                            {t('routingRules.dialog.priority')}
                          </FieldLabel>
                          <Input
                            id="priority"
                            type="number"
                            value={formData.routing?.priority || ''}
                            onChange={(e) => {
                              const value = e.target.value;
                              setFormData({
                                ...formData,
                                routing: formData.routing
                                  ? {
                                      ...formData.routing,
                                      priority: value ? parseInt(value) : 0,
                                    }
                                  : undefined,
                              });
                            }}
                            placeholder={t(
                              'routingRules.dialog.priorityPlaceholder',
                              { defaultValue: 'Auto (500-1000)' },
                            )}
                          />
                          <FieldDescription className="text-xs">
                            {t('routingRules.dialog.priorityDescription', {
                              defaultValue: 'Recommended: 500-1000',
                            })}
                          </FieldDescription>
                        </Field>

                        <Field>
                          <FieldLabel htmlFor="table">
                            {t('routingRules.dialog.table')}
                          </FieldLabel>
                          <Input
                            id="table"
                            type="number"
                            value={formData.routing?.table || ''}
                            onChange={(e) => {
                              const value = e.target.value;
                              setFormData({
                                ...formData,
                                routing: formData.routing
                                  ? {
                                      ...formData.routing,
                                      table: value ? parseInt(value) : 0,
                                    }
                                  : undefined,
                              });
                            }}
                            placeholder={t(
                              'routingRules.dialog.tablePlaceholder',
                              { defaultValue: 'Auto' },
                            )}
                          />
                          <FieldDescription className="text-xs">
                            {t('routingRules.dialog.tableDescription', {
                              defaultValue: 'Defaults to priority value',
                            })}
                          </FieldDescription>
                        </Field>

                        <Field>
                          <FieldLabel htmlFor="fwmark">
                            {t('routingRules.dialog.fwMark')}
                          </FieldLabel>
                          <Input
                            id="fwmark"
                            type="number"
                            value={formData.routing?.fwmark || ''}
                            onChange={(e) => {
                              const value = e.target.value;
                              setFormData({
                                ...formData,
                                routing: formData.routing
                                  ? {
                                      ...formData.routing,
                                      fwmark: value ? parseInt(value) : 0,
                                    }
                                  : undefined,
                              });
                            }}
                            placeholder={t(
                              'routingRules.dialog.fwmarkPlaceholder',
                              { defaultValue: 'Auto' },
                            )}
                          />
                          <FieldDescription className="text-xs">
                            {t('routingRules.dialog.fwmarkDescription', {
                              defaultValue: 'Defaults to priority value',
                            })}
                          </FieldDescription>
                        </Field>
                      </div>
                    </div>
                  </AccordionContent>
                </AccordionItem>
              </Accordion>
            </div>

            {/* Options Section */}
            <div className="space-y-4">
              <h3 className="text-lg font-medium">
                {t('routingRules.dialog.options')}
              </h3>

              <div className="flex items-center space-x-2">
                <Checkbox
                  id="flush_before_applying"
                  checked={formData.flush_before_applying}
                  onCheckedChange={(checked) =>
                    setFormData({
                      ...formData,
                      flush_before_applying: checked as boolean,
                    })
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
                        routing: formData.routing
                          ? {
                              ...formData.routing,
                              kill_switch: checked as boolean,
                            }
                          : undefined,
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
        );
      }}
    </BaseFormPage>
  );
}
