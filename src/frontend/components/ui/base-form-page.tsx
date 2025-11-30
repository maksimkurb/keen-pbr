import { useState, useEffect, type ReactNode } from 'react';
import { useTranslation } from 'react-i18next';
import { useNavigate } from 'react-router-dom';
import { Loader2, ArrowLeft } from 'lucide-react';
import { Button } from './button';
import { BaseFormActions } from './base-form-actions';

export interface BaseFormPageProps<T> {
  /** Page title */
  title: string;
  /** Page description */
  description: string;
  /** Back navigation path */
  backPath: string;
  /** Edit mode flag */
  isEditMode: boolean;
  /** Initial data (for edit mode) */
  initialData?: T;
  /** Loading state (for edit mode data fetch) */
  isLoading?: boolean;
  /** Save/create mutation pending state */
  isPending: boolean;
  /** Callback when form is submitted */
  onSubmit: (data: T) => Promise<void>;
  /** Function to render form fields - receives current form data and setter */
  children: (
    formData: T,
    setFormData: React.Dispatch<React.SetStateAction<T>>,
  ) => ReactNode;
  /** Default form data for create mode */
  defaultData: T;
  /** Optional: Custom validation */
  canSubmit?: (formData: T) => boolean;
}

/**
 * Reusable form page component for create/edit operations with:
 * - Back button navigation
 * - Loading state handling
 * - Form state management
 * - TypeScript type safety
 */
export function BaseFormPage<T>({
  title,
  description,
  backPath,
  isEditMode,
  initialData,
  isLoading = false,
  isPending,
  onSubmit,
  children,
  defaultData,
  canSubmit,
}: BaseFormPageProps<T>) {
  const { t } = useTranslation();
  const navigate = useNavigate();

  const [formData, setFormData] = useState<T>(defaultData);

  // Initialize form data with initial data in edit mode
  useEffect(() => {
    if (isEditMode && initialData) {
      setFormData(initialData);
    }
  }, [isEditMode, initialData]);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    await onSubmit(formData);
  };

  const handleCancel = () => {
    navigate(backPath);
  };

  // Show loading state in edit mode
  if (isEditMode && isLoading) {
    return (
      <div className="flex items-center justify-center py-12">
        <Loader2 className="h-8 w-8 animate-spin text-muted-foreground" />
      </div>
    );
  }

  const disableSubmit = canSubmit ? !canSubmit(formData) : false;

  return (
    <div className="max-w-3xl mx-auto space-y-6">
      <div className="flex items-center gap-4">
        <Button variant="ghost" size="icon" onClick={handleCancel}>
          <ArrowLeft className="h-4 w-4" />
        </Button>
        <div>
          <h1 className="text-2xl font-bold">{title}</h1>
          <p className="text-muted-foreground">{description}</p>
        </div>
      </div>

      <form onSubmit={handleSubmit} className="space-y-8">
        {children(formData, setFormData)}

        <BaseFormActions
          isSaving={isPending}
          disableSave={disableSubmit}
          onCancel={handleCancel}
          isEditMode={isEditMode}
        />
      </form>
    </div>
  );
}
