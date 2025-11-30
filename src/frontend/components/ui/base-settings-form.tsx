import { Loader2 } from 'lucide-react';
import type { ReactNode } from 'react';
import { useEffect, useMemo, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Alert } from './alert';
import { Button } from './button';

/**
 * Deep equality comparison for detecting changes in form data
 */
function deepEqual<T>(obj1: T, obj2: T): boolean {
  if (obj1 === obj2) return true;

  if (obj1 == null || obj2 == null) return false;
  if (typeof obj1 !== 'object' || typeof obj2 !== 'object') return false;

  const keys1 = Object.keys(obj1 as object);
  const keys2 = Object.keys(obj2 as object);

  if (keys1.length !== keys2.length) return false;

  for (const key of keys1) {
    const val1 = (obj1 as any)[key];
    const val2 = (obj2 as any)[key];

    const areObjects = val1 != null && typeof val1 === 'object';
    if (
      (areObjects && !deepEqual(val1, val2)) ||
      (!areObjects && val1 !== val2)
    ) {
      return false;
    }
  }

  return true;
}

export interface BaseSettingsFormProps<T> {
  /** Initial data from API */
  data: T | undefined;
  /** Loading state */
  isLoading: boolean;
  /** Error state */
  error: Error | null;
  /** Save mutation state */
  isSaving: boolean;
  /** Callback when form is submitted */
  onSave: (data: T) => Promise<void>;
  /** Function to render form fields - receives current form data and setter */
  children: (
    formData: T,
    setFormData: React.Dispatch<React.SetStateAction<T>>,
  ) => ReactNode;
  /** Optional: Default form data when no data is loaded */
  defaultData?: T;
}

/**
 * Reusable settings form component with:
 * - Automatic change detection
 * - Reset and Save buttons that activate on changes
 * - Full-height layout with scrollable content
 * - Sticky footer buttons
 * - TypeScript type safety
 */
export function BaseSettingsForm<T>({
  data,
  isLoading,
  error,
  isSaving,
  onSave,
  children,
  defaultData,
}: BaseSettingsFormProps<T>) {
  const { t } = useTranslation();

  // Initialize with defaultData if provided, otherwise wait for data
  const [formData, setFormData] = useState<T>(() => {
    return data ?? defaultData ?? ({} as T);
  });

  // Store original data for change detection and reset
  const [originalData, setOriginalData] = useState<T>(() => {
    return data ?? defaultData ?? ({} as T);
  });

  // Sync form data with fetched data
  useEffect(() => {
    if (data) {
      setFormData(data);
      setOriginalData(data);
    }
  }, [data]);

  // Detect if form has changes using deep equality
  const hasChanges = useMemo(() => {
    return !deepEqual(formData, originalData);
  }, [formData, originalData]);

  const handleReset = () => {
    setFormData(originalData);
  };

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();

    try {
      await onSave(formData);
      // Update original data after successful save
      setOriginalData(formData);
    } catch (error) {
      // Error handling is done by parent component via toast
      console.error('Save error:', error);
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
    <div className="relative">
      <form onSubmit={handleSubmit}>
        {/* Scrollable content area with padding for sticky footer */}
        <div className="space-y-6 pb-24">{children(formData, setFormData)}</div>

        {/* Fixed footer with buttons */}
        <div className="fixed bottom-0 left-0 right-0 border-t bg-background/95 backdrop-blur supports-backdrop-filter:bg-background/80 shadow-[0_-4px_6px_-1px_rgba(0,0,0,0.1)] z-50">
          <div className="container max-w-7xl mx-auto px-4 py-4">
            <div className="flex justify-end gap-3">
              <Button
                type="button"
                variant="outline"
                onClick={handleReset}
                disabled={!hasChanges || isSaving}
              >
                {t('common.cancel')}
              </Button>
              <Button type="submit" disabled={!hasChanges || isSaving}>
                {isSaving && <Loader2 className="mr-2 h-4 w-4 animate-spin" />}
                {t('settings.saveChanges')}
              </Button>
            </div>
          </div>
        </div>
      </form>
    </div>
  );
}
