import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
import { Loader2 } from 'lucide-react';
import { useCreateList } from '../../src/hooks/useLists';
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
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '../ui/select';
import { Textarea } from '../ui/textarea';
import { Button } from '../ui/button';
import type { CreateListRequest } from '../../src/api/client';

interface CreateListDialogProps {
  open: boolean;
  onOpenChange: (open: boolean) => void;
}

export function CreateListDialog({ open, onOpenChange }: CreateListDialogProps) {
  const { t } = useTranslation();
  const createList = useCreateList();

  const [formData, setFormData] = useState<{
    list_name: string;
    type: 'url' | 'file' | 'hosts';
    url?: string;
    file?: string;
    hosts?: string;
  }>({
    list_name: '',
    type: 'url',
  });

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();

    try {
      const requestData: CreateListRequest = {
        list_name: formData.list_name,
      };

      if (formData.type === 'url' && formData.url) {
        requestData.url = formData.url;
      } else if (formData.type === 'file' && formData.file) {
        requestData.file = formData.file;
      } else if (formData.type === 'hosts' && formData.hosts) {
        requestData.hosts = formData.hosts.split('\n').filter((h) => h.trim());
      }

      await createList.mutateAsync(requestData);

      toast.success(t('common.success'), {
        description: `List "${formData.list_name}" created successfully`,
      });

      // Reset form and close dialog
      setFormData({ list_name: '', type: 'url' });
      onOpenChange(false);
    } catch (error) {
      toast.error(t('common.error'), {
        description: error instanceof Error ? error.message : 'Failed to create list',
      });
    }
  };

  return (
    <ResponsiveDialog open={open} onOpenChange={onOpenChange}>
      <ResponsiveDialogContent className="max-w-2xl">
        <ResponsiveDialogHeader>
          <ResponsiveDialogTitle>{t('lists.newList')}</ResponsiveDialogTitle>
          <ResponsiveDialogDescription>
            Create a new list of domains, IPs, or CIDRs
          </ResponsiveDialogDescription>
        </ResponsiveDialogHeader>

        <form onSubmit={handleSubmit}>
          <FieldGroup>
            <Field>
              <FieldLabel htmlFor="list_name">List Name</FieldLabel>
              <FieldDescription>
                A unique name for this list
              </FieldDescription>
              <Input
                id="list_name"
                value={formData.list_name}
                onChange={(e) => setFormData({ ...formData, list_name: e.target.value })}
                placeholder="my-list"
                required
              />
            </Field>

            <Field>
              <FieldLabel htmlFor="type">List Type</FieldLabel>
              <FieldDescription>
                Where the list data comes from
              </FieldDescription>
              <Select
                value={formData.type}
                onValueChange={(value: 'url' | 'file' | 'hosts') =>
                  setFormData({ ...formData, type: value })
                }
              >
                <SelectTrigger>
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="url">{t('lists.types.url')}</SelectItem>
                  <SelectItem value="file">{t('lists.types.file')}</SelectItem>
                  <SelectItem value="hosts">{t('lists.types.hosts')}</SelectItem>
                </SelectContent>
              </Select>
            </Field>

            {formData.type === 'url' && (
              <Field>
                <FieldLabel htmlFor="url">URL</FieldLabel>
                <FieldDescription>
                  HTTP(S) URL to download the list from
                </FieldDescription>
                <Input
                  id="url"
                  type="url"
                  value={formData.url || ''}
                  onChange={(e) => setFormData({ ...formData, url: e.target.value })}
                  placeholder="https://example.com/list.txt"
                  required
                />
              </Field>
            )}

            {formData.type === 'file' && (
              <Field>
                <FieldLabel htmlFor="file">File Path</FieldLabel>
                <FieldDescription>
                  Absolute path to the list file on the system
                </FieldDescription>
                <Input
                  id="file"
                  value={formData.file || ''}
                  onChange={(e) => setFormData({ ...formData, file: e.target.value })}
                  placeholder="/opt/etc/keen-pbr/my-list.txt"
                  required
                />
              </Field>
            )}

            {formData.type === 'hosts' && (
              <Field>
                <FieldLabel htmlFor="hosts">Hosts</FieldLabel>
                <FieldDescription>
                  Enter one domain/IP/CIDR per line
                </FieldDescription>
                <Textarea
                  id="hosts"
                  value={formData.hosts || ''}
                  onChange={(e) => setFormData({ ...formData, hosts: e.target.value })}
                  placeholder="example.com&#10;192.168.1.0/24&#10;10.0.0.1"
                  rows={6}
                  required
                />
              </Field>
            )}
          </FieldGroup>

          <ResponsiveDialogFooter className="mt-6">
            <Button
              type="button"
              variant="outline"
              onClick={() => onOpenChange(false)}
              disabled={createList.isPending}
            >
              {t('common.cancel')}
            </Button>
            <Button type="submit" disabled={createList.isPending}>
              {createList.isPending && <Loader2 className="mr-2 h-4 w-4 animate-spin" />}
              {t('common.create')}
            </Button>
          </ResponsiveDialogFooter>
        </form>
      </ResponsiveDialogContent>
    </ResponsiveDialog>
  );
}
