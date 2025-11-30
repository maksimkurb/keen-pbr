import { Loader2 } from 'lucide-react';
import { useEffect, useRef } from 'react';
import { useTranslation } from 'react-i18next';
import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
  AlertDialogTrigger,
} from './alert-dialog';

interface DeleteConfirmationDialogProps {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  title: string;
  description?: React.ReactNode;
  children?: React.ReactNode;
  onConfirm: () => void;
  isPending?: boolean;
  confirmDisabled?: boolean;
  trigger?: React.ReactNode;
}

export function DeleteConfirmationDialog({
  open,
  onOpenChange,
  title,
  description,
  children,
  onConfirm,
  isPending = false,
  confirmDisabled = false,
  trigger,
}: DeleteConfirmationDialogProps) {
  const { t } = useTranslation();

  // Cache the content while the dialog is open to prevent flashing during close animation
  const cachedTitle = useRef(title);
  const cachedDescription = useRef(description);
  const cachedChildren = useRef(children);

  useEffect(() => {
    if (open) {
      cachedTitle.current = title;
      cachedDescription.current = description;
      cachedChildren.current = children;
    }
  }, [open, title, description, children]);

  return (
    <AlertDialog open={open} onOpenChange={onOpenChange}>
      {trigger && <AlertDialogTrigger asChild>{trigger}</AlertDialogTrigger>}
      <AlertDialogContent>
        <AlertDialogHeader>
          <AlertDialogTitle>{cachedTitle.current}</AlertDialogTitle>
          {cachedDescription.current && (
            <AlertDialogDescription>
              {cachedDescription.current}
            </AlertDialogDescription>
          )}
        </AlertDialogHeader>

        {cachedChildren.current}

        <AlertDialogFooter>
          <AlertDialogCancel disabled={isPending}>
            {t('common.cancel')}
          </AlertDialogCancel>
          <AlertDialogAction
            onClick={(e) => {
              e.preventDefault();
              onConfirm();
            }}
            disabled={isPending || confirmDisabled}
            className="bg-destructive text-destructive-foreground hover:bg-destructive/90"
          >
            {isPending && <Loader2 className="mr-2 h-4 w-4 animate-spin" />}
            {t('common.delete')}
          </AlertDialogAction>
        </AlertDialogFooter>
      </AlertDialogContent>
    </AlertDialog>
  );
}
