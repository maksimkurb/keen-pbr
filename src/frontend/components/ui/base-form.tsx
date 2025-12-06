import { ArrowLeft, Loader2 } from 'lucide-react';
import type { ReactNode } from 'react';
import { useEffect, useMemo } from 'react';
import {
  type FieldValues,
  FormProvider,
  type UseFormReturn,
} from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { useNavigate } from 'react-router-dom';
import { formatError } from '../../src/utils/errorUtils';
import { Alert } from './alert';
import { Button } from './button';

export interface BaseFormProps<T extends FieldValues> {
  /** react-hook-form methods from parent component */
  methods: UseFormReturn<T>;

  /** Initial data from API (for edit mode) */
  data?: T;

  /** Loading initial data state */
  isLoading: boolean;
  /** Save/create operation in progress */
  isPending: boolean;
  /** Error loading data */
  error?: Error | null;

  /** Callback when form is submitted */
  onSubmit: (data: T) => Promise<void>;

  /** Edit vs Create mode */
  isEditMode?: boolean;

  /** If provided, shows back button */
  backPath?: string;
  /** Custom cancel handler (default: navigate to backPath or reset form) */
  onCancel?: () => void;

  /** Page title (optional) */
  title?: string;
  /** Page description (optional) */
  description?: string;

  /** Custom text for save button */
  saveText?: string;
  /** Custom text for cancel button */
  cancelText?: string;

  /** Function to render form fields */
  children: ReactNode;
}

/**
 * Universal form component that combines the functionality of BaseSettingsForm
 * and BaseFormPage. Now uses react-hook-form for validation and form state management.
 * Supports:
 * - react-hook-form integration with full validation support
 * - Automatic change detection (optional, enabled by default in edit mode)
 * - Sticky footer with Reset/Save buttons
 * - Optional back button navigation
 * - Optional header with title and description
 * - Full-height layout with scrollable content
 * - TypeScript type safety
 */
export function BaseForm<T extends FieldValues>({
  methods,
  data,
  isLoading,
  isPending,
  error,
  onSubmit,
  isEditMode = false,
  backPath,
  onCancel,
  title,
  description,
  saveText,
  cancelText,
  children,
}: BaseFormProps<T>) {
  const { t } = useTranslation();
  const navigate = useNavigate();

  const { handleSubmit, reset, formState } = methods;

  // Store original data for reset functionality
  const originalData = useMemo(() => {
    return data ?? ({} as T);
  }, [data]);

  // Sync form data with fetched data
  useEffect(() => {
    if (data) {
      reset(data as any);
    }
  }, [data, reset]);

  const handleBack = () => {
    if (backPath) {
      navigate(backPath);
    }
  };

  const handleCancel = () => {
    if (onCancel) {
      // Use custom cancel handler if provided
      onCancel();
    } else {
      // Reset form to original data
      reset(originalData as any);
    }
  };

  const onSubmitHandler = async (formData: T) => {
    try {
      await onSubmit(formData);
      // Update form state after successful save
      reset(formData as any);
    } catch (error) {
      // Error handling is done by parent component via toast
      console.error('Save error:', formatError(error));
    }
  };

  // Loading state
  if (isLoading) {
    return (
      <div className="flex h-full items-center justify-center">
        <Loader2 className="h-8 w-8 animate-spin text-muted-foreground" />
      </div>
    );
  }

  // Error state
  if (error) {
    return (
      <div className="h-full p-6">
        <Alert variant="destructive">
          <div>
            <strong>{t('common.error')}</strong>
            <p className="mt-1 text-sm">
              {error instanceof Error ? error.message : t('settings.loadError')}
            </p>
          </div>
        </Alert>
      </div>
    );
  }

  return (
    <FormProvider {...methods}>
      <div className="relative max-w-4xl mx-auto">
        {/* Optional Header with Back Button */}
        {(title || backPath) && (
          <div className="flex items-center gap-4 mb-6">
            {backPath && (
              <Button variant="ghost" size="icon" onClick={handleBack}>
                <ArrowLeft className="h-4 w-4" />
              </Button>
            )}
            {title && (
              <div>
                <h1 className="text-2xl font-bold">{title}</h1>
                {description && (
                  <p className="text-muted-foreground">{description}</p>
                )}
              </div>
            )}
          </div>
        )}

        <form onSubmit={handleSubmit(onSubmitHandler)}>
          {/* Scrollable content area with padding for sticky footer */}
          <div className="space-y-6 pb-24">{children}</div>

          {/* Fixed footer with buttons (sticky footer from BaseSettingsForm) */}
          <div className="fixed bottom-0 left-0 right-0 border-t bg-background/95 backdrop-blur supports-backdrop-filter:bg-background/80 shadow-[0_-4px_6px_-1px_rgba(0,0,0,0.1)] z-50">
            <div className="container max-w-4xl mx-auto px-4 py-4">
              <div className="flex justify-end gap-3">
                <Button
                  type="button"
                  variant="outline"
                  onClick={handleCancel}
                  disabled={!formState.isDirty || isPending}
                >
                  {cancelText || t('common.cancel')}
                </Button>
                <Button
                  type="submit"
                  disabled={!formState.isDirty || isPending}
                >
                  {isPending && (
                    <Loader2 className="mr-2 h-4 w-4 animate-spin" />
                  )}
                  {saveText ||
                    (isEditMode ? t('common.save') : t('common.create'))}
                </Button>
              </div>
            </div>
          </div>
        </form>
      </div>
    </FormProvider>
  );
}
