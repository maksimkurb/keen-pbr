import { AlertTriangle, Loader2 } from 'lucide-react';
import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
import { useDeleteList } from '../../src/hooks/useLists';
import { Alert } from '../ui/alert';
import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
} from '../ui/alert-dialog';

interface DeleteListConfirmationProps {
  listName: string | null;
  open: boolean;
  onOpenChange: (open: boolean) => void;
  usedByIPSets?: string[]; // IPSets that reference this list
}

export function DeleteListConfirmation({
  listName,
  open,
  onOpenChange,
  usedByIPSets = [],
}: DeleteListConfirmationProps) {
  const { t } = useTranslation();
  const deleteList = useDeleteList();

  const handleDelete = async () => {
    if (!listName) return;

    try {
      await deleteList.mutateAsync(listName);

      toast.success(t('common.success'), {
        description: t('lists.delete.success', { name: listName }),
      });

      onOpenChange(false);
    } catch (error) {
      toast.error(t('common.error'), {
        description:
          error instanceof Error ? error.message : t('lists.delete.error'),
      });
    }
  };

  const isUsedByIPSets = usedByIPSets.length > 0;

  return (
    <AlertDialog open={open} onOpenChange={onOpenChange}>
      <AlertDialogContent>
        <AlertDialogHeader>
          <AlertDialogTitle>{t('lists.delete.title')}</AlertDialogTitle>
          <AlertDialogDescription>
            {t('lists.delete.description', { name: listName })}
          </AlertDialogDescription>
        </AlertDialogHeader>

        {isUsedByIPSets && (
          <Alert variant="destructive">
            <AlertTriangle className="h-4 w-4" />
            <div>
              {t('lists.delete.warningMessage')}
              <ul className="mt-2 list-disc pl-5">
                {usedByIPSets.map((ipset) => (
                  <li key={ipset}>{ipset}</li>
                ))}
              </ul>
              <p className="mt-2 text-sm">
                {t('lists.delete.warningInstruction')}
              </p>
            </div>
          </Alert>
        )}

        {!isUsedByIPSets && (
          <p className="text-sm text-muted-foreground">
            {t('lists.delete.confirmation')}
          </p>
        )}

        <AlertDialogFooter>
          <AlertDialogCancel disabled={deleteList.isPending}>
            {t('common.cancel')}
          </AlertDialogCancel>
          <AlertDialogAction
            onClick={handleDelete}
            disabled={deleteList.isPending || isUsedByIPSets}
            className="bg-destructive text-destructive-foreground hover:bg-destructive/90"
          >
            {deleteList.isPending && (
              <Loader2 className="mr-2 h-4 w-4 animate-spin" />
            )}
            {t('common.delete')}
          </AlertDialogAction>
        </AlertDialogFooter>
      </AlertDialogContent>
    </AlertDialog>
  );
}
