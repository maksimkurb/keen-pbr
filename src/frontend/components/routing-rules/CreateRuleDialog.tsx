import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
import { Loader2, X, Plus } from 'lucide-react';
import { useCreateIPSet } from '../../src/hooks/useIPSets';
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from '../ui/dialog';
import { Field, FieldLabel, FieldDescription, FieldGroup } from '../ui/field';
import { Input } from '../ui/input';
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '../ui/select';
import { Checkbox } from '../ui/checkbox';
import { Button } from '../ui/button';
import { Badge } from '../ui/badge';
import type { CreateIPSetRequest } from '../../src/api/client';

interface CreateRuleDialogProps {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  availableLists: string[];
}

export function CreateRuleDialog({ open, onOpenChange, availableLists }: CreateRuleDialogProps) {
  const { t } = useTranslation();
  const createIPSet = useCreateIPSet();

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

  const [interfaceInput, setInterfaceInput] = useState('');

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
    setInterfaceInput('');
  };

  const toggleList = (listName: string) => {
    setFormData({
      ...formData,
      lists: formData.lists.includes(listName)
        ? formData.lists.filter((l) => l !== listName)
        : [...formData.lists, listName],
    });
  };

  const addInterface = () => {
    if (interfaceInput.trim() && formData.routing) {
      setFormData({
        ...formData,
        routing: {
          ...formData.routing,
          interfaces: [...formData.routing.interfaces, interfaceInput.trim()],
        },
      });
      setInterfaceInput('');
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

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="max-w-2xl max-h-[90vh] overflow-y-auto">
        <DialogHeader>
          <DialogTitle>{t('routingRules.newRule')}</DialogTitle>
          <DialogDescription>
            Create a new routing rule for policy-based routing
          </DialogDescription>
        </DialogHeader>

        <form onSubmit={handleSubmit}>
          <FieldGroup>
            {/* Basic Info Section */}
            <div className="space-y-4">
              <h3 className="text-sm font-medium">Basic Information</h3>

              <Field>
                <FieldLabel htmlFor="ipset_name">IPSet Name</FieldLabel>
                <FieldDescription>
                  Must match pattern: lowercase letters, numbers, and underscores
                </FieldDescription>
                <Input
                  id="ipset_name"
                  value={formData.ipset_name}
                  onChange={(e) => setFormData({ ...formData, ipset_name: e.target.value })}
                  placeholder="my_vpn_ipset"
                  pattern="^[a-z][a-z0-9_]*$"
                  required
                />
              </Field>

              <Field>
                <FieldLabel htmlFor="ip_version">IP Version</FieldLabel>
                <Select
                  value={formData.ip_version.toString()}
                  onValueChange={(value) => setFormData({ ...formData, ip_version: parseInt(value) as 4 | 6 })}
                >
                  <SelectTrigger>
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="4">IPv4</SelectItem>
                    <SelectItem value="6">IPv6</SelectItem>
                  </SelectContent>
                </Select>
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
                <div className="flex flex-wrap gap-2 border rounded-md p-3 min-h-[60px]">
                  {availableLists.map((list) => (
                    <Badge
                      key={list}
                      variant={formData.lists.includes(list) ? 'default' : 'outline'}
                      className="cursor-pointer"
                      onClick={() => toggleList(list)}
                    >
                      {list}
                      {formData.lists.includes(list) && (
                        <X className="ml-1 h-3 w-3" />
                      )}
                    </Badge>
                  ))}
                  {availableLists.length === 0 && (
                    <span className="text-sm text-muted-foreground">No lists available</span>
                  )}
                </div>
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
                  <div className="space-y-2">
                    <div className="flex gap-2">
                      <Input
                        value={interfaceInput}
                        onChange={(e) => setInterfaceInput(e.target.value)}
                        placeholder="nwg0"
                        onKeyDown={(e) => {
                          if (e.key === 'Enter') {
                            e.preventDefault();
                            addInterface();
                          }
                        }}
                      />
                      <Button type="button" size="sm" onClick={addInterface}>
                        <Plus className="h-4 w-4" />
                      </Button>
                    </div>
                    <div className="flex flex-wrap gap-2">
                      {formData.routing.interfaces.map((iface, index) => (
                        <Badge key={`${iface}-${index}`} variant="secondary">
                          {index + 1}. {iface}
                          <X
                            className="ml-1 h-3 w-3 cursor-pointer"
                            onClick={() => removeInterface(iface)}
                          />
                        </Badge>
                      ))}
                    </div>
                  </div>
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

          <DialogFooter className="mt-6">
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
          </DialogFooter>
        </form>
      </DialogContent>
    </Dialog>
  );
}
