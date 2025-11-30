import { Loader2 } from 'lucide-react';
import { useTranslation } from 'react-i18next';
import { Button } from './button';

export interface BaseFormActionsProps {
  /** Whether the form is currently saving */
  isSaving: boolean;
  /** Whether the cancel button should be disabled */
  disableCancel?: boolean;
  /** Whether the save button should be disabled */
  disableSave?: boolean;
  /** Callback when cancel is clicked */
  onCancel: () => void;
  /** Text for the save button (defaults to translation) */
  saveText?: string;
  /** Text for the cancel button (defaults to translation) */
  cancelText?: string;
  /** Whether this is an edit mode (affects save button text) */
  isEditMode?: boolean;
}

/**
 * Reusable form action buttons component with save/cancel buttons.
 * Displays a loading spinner when saving.
 */
export function BaseFormActions({
  isSaving,
  disableCancel = false,
  disableSave = false,
  onCancel,
  saveText,
  cancelText,
  isEditMode = false,
}: BaseFormActionsProps) {
  const { t } = useTranslation();

  return (
    <div className="flex justify-end gap-4">
      <Button
        type="button"
        variant="outline"
        onClick={onCancel}
        disabled={disableCancel || isSaving}
      >
        {cancelText || t('common.cancel')}
      </Button>
      <Button type="submit" disabled={disableSave || isSaving}>
        {isSaving && <Loader2 className="mr-2 h-4 w-4 animate-spin" />}
        {saveText || (isEditMode ? t('common.save') : t('common.create'))}
      </Button>
    </div>
  );
}
