import { useQuery } from '@tanstack/react-query';
import cidrRegex from 'cidr-regex';
import ipRegex from 'ip-regex';
import { useMemo, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useNavigate, useParams } from 'react-router-dom';
import { toast } from 'sonner';
import { BaseFormPage } from '../../components/ui/base-form-page';
import {
  Field,
  FieldDescription,
  FieldError,
  FieldGroup,
  FieldLabel,
} from '../../components/ui/field';
import { Input } from '../../components/ui/input';
import {
  type LineError,
  LineNumberedTextarea,
} from '../../components/ui/line-numbered-textarea';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '../../components/ui/select';
import type { ListSource } from '../api/client';
import { apiClient, KeenPBRAPIError } from '../api/client';
import { useCreateList, useLists, useUpdateList } from '../hooks/useLists';
import { getFieldError, mapValidationErrors } from '../utils/formValidation';

// Validation patterns
const DOMAIN_PATTERN =
  /^([a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?\.)*[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?\.?$|^xn--[a-zA-Z0-9-]+$/;

const CIDR_VALIDATOR = cidrRegex({ exact: true });
const IP_VALIDATOR = ipRegex({ exact: true });

function validateHostLine(line: string): string | null {
  const trimmed = line.trim();

  if (trimmed === '') return null;
  if (trimmed.startsWith('#')) return null;
  if (DOMAIN_PATTERN.test(trimmed)) return null;

  if (CIDR_VALIDATOR.test(trimmed)) {
    return null;
  }

  if (IP_VALIDATOR.test(trimmed)) {
    return null;
  }

  return 'Invalid format: must be empty, comment (#), domain, IPv4, or IPv6';
}

function validateHosts(hosts: string): LineError {
  const lines = hosts.split('\n');
  const errors: LineError = {};

  lines.forEach((line, index) => {
    const lineError = validateHostLine(line);
    if (lineError) {
      errors[index + 1] = lineError;
    }
  });

  return errors;
}

interface FormData {
  list_name: string;
  type: 'url' | 'file' | 'hosts';
  url?: string;
  file?: string;
  hosts?: string;
}

export default function ListPage() {
  const { t } = useTranslation();
  const navigate = useNavigate();
  const { name } = useParams<{ name: string }>();
  const isEditMode = !!name;

  const createList = useCreateList();
  const updateList = useUpdateList();
  const { data: lists, isLoading: isLoadingLists } = useLists();

  // Validation errors from API
  const [validationErrors, setValidationErrors] = useState<
    Record<string, string>
  >({});

  // Fetch full list data when editing an inline hosts list
  const { data: fullListData, isLoading: isLoadingFullList } = useQuery({
    queryKey: ['list', name],
    queryFn: () => apiClient.getList(name || ''),
    enabled: isEditMode && !!name,
  });

  // Determine initial data for edit mode
  const initialData: FormData | undefined = useMemo(() => {
    if (!isEditMode || !lists) return undefined;

    const list = lists.find((l) => l.list_name === name);
    if (!list) return undefined;

    return {
      list_name: list.list_name,
      type: list.type as 'url' | 'file' | 'hosts',
      url: list.url,
      file: list.file,
      hosts: fullListData?.hosts?.join('\n') || '',
    };
  }, [isEditMode, lists, name, fullListData?.hosts]);

  // Validate hosts field
  const getHostsErrors = (formData: FormData): LineError => {
    if (formData.type === 'hosts' && formData.hosts) {
      return validateHosts(formData.hosts);
    }
    return {};
  };

  const handleSubmit = async (formData: FormData) => {
    // Block saving if there are validation errors
    const hostsErrors = getHostsErrors(formData);
    if (Object.keys(hostsErrors).length > 0) {
      toast.error(t('common.error'), {
        description: t('lists.dialog.validationError'),
      });
      throw new Error('Validation failed');
    }

    try {
      const requestData: ListSource = {
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
          name: name || '',
          data: requestData,
        });
        toast.success(t('common.success'), {
          description: t('lists.dialog.updateSuccess', {
            name: formData.list_name,
          }),
        });
      } else {
        await createList.mutateAsync(requestData);
        toast.success(t('common.success'), {
          description: t('lists.dialog.createSuccess', {
            name: formData.list_name,
          }),
        });
      }

      setValidationErrors({});
      navigate('/lists');
    } catch (error) {
      if (error instanceof KeenPBRAPIError) {
        const errors = error.getValidationErrors();
        if (errors) {
          setValidationErrors(mapValidationErrors(errors));
          toast.error(t('lists.dialog.validationError'));
          throw error;
        }
      }
      toast.error(t('common.error'), {
        description: t('lists.dialog.saveError', {
          action: isEditMode
            ? t('common.update')
            : t('common.create').toLowerCase(),
        }),
      });
      throw error;
    }
  };

  // Check if currently loading
  const currentList = lists?.find((l) => l.list_name === name);
  const isLoading =
    isEditMode &&
    (isLoadingLists || (currentList?.type === 'hosts' && isLoadingFullList));

  const isPending = isEditMode ? updateList.isPending : createList.isPending;

  const defaultData: FormData = {
    list_name: '',
    type: 'url',
  };

  return (
    <BaseFormPage
      title={
        isEditMode ? t('lists.dialog.editTitle') : t('lists.dialog.createTitle')
      }
      description={
        isEditMode
          ? t('lists.dialog.editDescription')
          : t('lists.dialog.createDescription')
      }
      backPath="/lists"
      isEditMode={isEditMode}
      initialData={initialData}
      isLoading={isLoading}
      isPending={isPending}
      onSubmit={handleSubmit}
      defaultData={defaultData}
      canSubmit={(formData) =>
        Object.keys(getHostsErrors(formData)).length === 0
      }
    >
      {(formData, setFormData) => {
        const hostsErrors = getHostsErrors(formData);
        const hasHostsErrors = Object.keys(hostsErrors).length > 0;

        return (
          <FieldGroup>
            <Field
              data-invalid={!!getFieldError('list_name', validationErrors)}
            >
              <FieldLabel htmlFor="list_name">
                {t('lists.dialog.listName')}
              </FieldLabel>
              <Input
                id="list_name"
                value={formData.list_name}
                onChange={(e) =>
                  setFormData({ ...formData, list_name: e.target.value })
                }
                placeholder={t('lists.dialog.listNamePlaceholder')}
                required
                disabled={isEditMode}
                aria-invalid={!!getFieldError('list_name', validationErrors)}
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
              {getFieldError('list_name', validationErrors) && (
                <FieldError>
                  {getFieldError('list_name', validationErrors)}
                </FieldError>
              )}
            </Field>

            <Field>
              <FieldLabel htmlFor="type">
                {t('lists.dialog.listType')}
              </FieldLabel>
              <FieldDescription>
                {t('lists.dialog.listTypeDescription')}
              </FieldDescription>
              <Select
                value={formData.type || ''}
                onValueChange={(value: 'url' | 'file' | 'hosts') => {
                  if (value) {
                    setFormData({ ...formData, type: value });
                  }
                }}
              >
                <SelectTrigger>
                  <SelectValue placeholder={t('lists.dialog.selectType')} />
                </SelectTrigger>
                <SelectContent>
                  <SelectItem value="url">{t('lists.types.url')}</SelectItem>
                  <SelectItem value="file">{t('lists.types.file')}</SelectItem>
                  <SelectItem value="hosts">
                    {t('lists.types.hosts')}
                  </SelectItem>
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
                  onChange={(e) =>
                    setFormData({ ...formData, url: e.target.value })
                  }
                  placeholder={t('lists.dialog.urlPlaceholder')}
                  required
                />
              </Field>
            )}

            {formData.type === 'file' && (
              <Field>
                <FieldLabel htmlFor="file">
                  {t('lists.dialog.filePath')}
                </FieldLabel>
                <FieldDescription>
                  {t('lists.dialog.filePathDescription')}
                </FieldDescription>
                <Input
                  id="file"
                  value={formData.file || ''}
                  onChange={(e) =>
                    setFormData({ ...formData, file: e.target.value })
                  }
                  placeholder={t('lists.dialog.filePathPlaceholder')}
                  required
                />
              </Field>
            )}

            {formData.type === 'hosts' && (
              <Field>
                <FieldLabel htmlFor="hosts">
                  {t('lists.dialog.hosts')}
                </FieldLabel>
                <FieldDescription>
                  {t('lists.dialog.hostsDescription')}
                </FieldDescription>
                <LineNumberedTextarea
                  id="hosts"
                  value={formData.hosts || ''}
                  onChange={(e) =>
                    setFormData({ ...formData, hosts: e.target.value })
                  }
                  placeholder={t('lists.dialog.hostsPlaceholder')}
                  errors={hostsErrors}
                  required
                />
                {hasHostsErrors && (
                  <p className="text-sm text-red-600 dark:text-red-400 mt-2">
                    {t('lists.dialog.validationErrors', {
                      count: Object.keys(hostsErrors).length,
                    })}
                  </p>
                )}
              </Field>
            )}
          </FieldGroup>
        );
      }}
    </BaseFormPage>
  );
}
