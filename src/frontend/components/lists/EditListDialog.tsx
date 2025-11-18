import { useState, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
import { Loader2 } from 'lucide-react';
import { useUpdateList } from '../../src/hooks/useLists';
import { useQuery } from '@tanstack/react-query';
import { apiClient } from '../../src/api/client';
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
import { Textarea } from '../ui/textarea';
import { Button } from '../ui/button';
import type { ListInfo, CreateListRequest } from '../../src/api/client';

interface EditListDialogProps {
  list: ListInfo | null;
  open: boolean;
  onOpenChange: (open: boolean) => void;
}

export function EditListDialog({ list, open, onOpenChange }: EditListDialogProps) {
  const { t } = useTranslation();
  const updateList = useUpdateList();

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

  // Fetch full list data when editing an inline hosts list
  const { data: fullListData } = useQuery({
    queryKey: ['list', list?.list_name],
    queryFn: () => apiClient.getList(list!.list_name),
    enabled: !!list && list.type === 'hosts',
  });

  // Initialize form when list changes
  useEffect(() => {
    if (list) {
      setFormData({
        list_name: list.list_name,
        type: list.type,
        url: list.url,
        file: list.file,
        hosts: fullListData?.hosts ? fullListData.hosts.join('\n') : '',
      });
    }
  }, [list, fullListData]);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();

    if (!list) return;

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

      await updateList.mutateAsync({
        name: list.list_name,
        data: requestData,
      });

      toast.success(t('common.success'), {
        description: `List "${formData.list_name}" updated successfully`,
      });

      onOpenChange(false);
    } catch (error) {
      toast.error(t('common.error'), {
        description: error instanceof Error ? error.message : 'Failed to update list',
      });
    }
  };

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="max-w-2xl">
        <DialogHeader>
          <DialogTitle>Edit List</DialogTitle>
          <DialogDescription>
            Update the list configuration
          </DialogDescription>
        </DialogHeader>

        <form onSubmit={handleSubmit}>
          <FieldGroup>
            <Field>
              <FieldLabel htmlFor="list_name">List Name</FieldLabel>
              <FieldDescription>
                Cannot be changed after creation
              </FieldDescription>
              <Input
                id="list_name"
                value={formData.list_name}
                onChange={(e) => setFormData({ ...formData, list_name: e.target.value })}
                placeholder="my-list"
                required
                disabled
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

          <DialogFooter className="mt-6">
            <Button
              type="button"
              variant="outline"
              onClick={() => onOpenChange(false)}
              disabled={updateList.isPending}
            >
              {t('common.cancel')}
            </Button>
            <Button type="submit" disabled={updateList.isPending}>
              {updateList.isPending && <Loader2 className="mr-2 h-4 w-4 animate-spin" />}
              {t('common.save')}
            </Button>
          </DialogFooter>
        </form>
      </DialogContent>
    </Dialog>
  );
}
