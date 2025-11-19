import { useState, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
import { Loader2 } from 'lucide-react';
import { useCreateList, useUpdateList } from '../../src/hooks/useLists';
import { useQuery } from '@tanstack/react-query';
import { apiClient } from '../../src/api/client';
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
import type { ListInfo, CreateListRequest } from '../../src/api/client';

interface ListDialogProps {
  list?: ListInfo | null; // If provided, dialog is in edit mode
  open: boolean;
  onOpenChange: (open: boolean) => void;
}

export function ListDialog({ list, open, onOpenChange }: ListDialogProps) {
  const { t } = useTranslation();
  const createList = useCreateList();
  const updateList = useUpdateList();

  const isEditMode = !!list;

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
    enabled: isEditMode && list.type === 'hosts',
  });

  // Initialize form when list changes (edit mode)
  useEffect(() => {
    if (list) {
      setFormData({
        list_name: list.list_name,
        type: list.type,
        url: list.url,
        file: list.file,
        hosts: fullListData?.hosts ? fullListData.hosts.join('\n') : '',
      });
    } else {
      // Reset form when switching to create mode
      setFormData({
        list_name: '',
        type: 'url',
      });
    }
  }, [list, fullListData]);

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

      if (isEditMode) {
        await updateList.mutateAsync({
          name: list.list_name,
          data: requestData,
        });
        toast.success(t('common.success'), {
          description: t('lists.dialog.updateSuccess', { name: formData.list_name }),
        });
      } else {
        await createList.mutateAsync(requestData);
        toast.success(t('common.success'), {
          description: t('lists.dialog.createSuccess', { name: formData.list_name }),
        });
        // Reset form after create
        setFormData({ list_name: '', type: 'url' });
      }

      onOpenChange(false);
    } catch (error) {
      toast.error(t('common.error'), {
        description: t('lists.dialog.saveError', { action: isEditMode ? t('common.update') : t('common.create').toLowerCase(), error: error instanceof Error ? error.message : String(error) }),
      });
    }
  };

  const isPending = isEditMode ? updateList.isPending : createList.isPending;

  return (
    <ResponsiveDialog open={open} onOpenChange={onOpenChange}>
      <ResponsiveDialogContent className="max-w-2xl">
        <ResponsiveDialogHeader>
          <ResponsiveDialogTitle>
            {isEditMode ? t('lists.dialog.editTitle') : t('lists.dialog.createTitle')}
          </ResponsiveDialogTitle>
          <ResponsiveDialogDescription>
            {isEditMode ? t('lists.dialog.editDescription') : t('lists.dialog.createDescription')}
          </ResponsiveDialogDescription>
        </ResponsiveDialogHeader>

        <form onSubmit={handleSubmit}>
          <FieldGroup>
            <Field>
              <FieldLabel htmlFor="list_name">{t('lists.dialog.listName')}</FieldLabel>
              <Input
                id="list_name"
                value={formData.list_name}
                onChange={(e) => setFormData({ ...formData, list_name: e.target.value })}
                placeholder={t('lists.dialog.listNamePlaceholder')}
                required
                disabled={isEditMode}
              />
              {isEditMode ? (
                <FieldDescription>
                  {t('lists.dialog.listNameDescriptionLocked')}
                </FieldDescription>
              ) : (
                <FieldDescription>
                  {t('lists.dialog.listNameDescription')}
                </FieldDescription>
              )}
            </Field>

            <Field>
              <FieldLabel htmlFor="type">{t('lists.dialog.listType')}</FieldLabel>
              <FieldDescription>
                {t('lists.dialog.listTypeDescription')}
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
                <FieldLabel htmlFor="url">{t('lists.dialog.url')}</FieldLabel>
                <FieldDescription>
                  {t('lists.dialog.urlDescription')}
                </FieldDescription>
                <Input
                  id="url"
                  type="url"
                  value={formData.url || ''}
                  onChange={(e) => setFormData({ ...formData, url: e.target.value })}
                  placeholder={t('lists.dialog.urlPlaceholder')}
                  required
                />
              </Field>
            )}

            {formData.type === 'file' && (
              <Field>
                <FieldLabel htmlFor="file">{t('lists.dialog.filePath')}</FieldLabel>
                <FieldDescription>
                  {t('lists.dialog.filePathDescription')}
                </FieldDescription>
                <Input
                  id="file"
                  value={formData.file || ''}
                  onChange={(e) => setFormData({ ...formData, file: e.target.value })}
                  placeholder={t('lists.dialog.filePathPlaceholder')}
                  required
                />
              </Field>
            )}

            {formData.type === 'hosts' && (
              <Field>
                <FieldLabel htmlFor="hosts">{t('lists.dialog.hosts')}</FieldLabel>
                <FieldDescription>
                  {t('lists.dialog.hostsDescription')}
                </FieldDescription>
                <Textarea
                  id="hosts"
                  value={formData.hosts || ''}
                  onChange={(e) => setFormData({ ...formData, hosts: e.target.value })}
                  placeholder={t('lists.dialog.hostsPlaceholder')}
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
