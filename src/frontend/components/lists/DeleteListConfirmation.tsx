import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
import { Loader2, AlertTriangle } from 'lucide-react';
import { useDeleteList } from '../../src/hooks/useLists';
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
import { Alert } from '../ui/alert';

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
        description: `List "${listName}" deleted successfully`,
      });

      onOpenChange(false);
    } catch (error) {
      toast.error(t('common.error'), {
        description: error instanceof Error ? error.message : 'Failed to delete list',
      });
    }
  };

  const isUsedByIPSets = usedByIPSets.length > 0;

  return (
    <AlertDialog open={open} onOpenChange={onOpenChange}>
      <AlertDialogContent>
        <AlertDialogHeader>
          <AlertDialogTitle>Delete List</AlertDialogTitle>
          <AlertDialogDescription>
            Are you sure you want to delete the list "{listName}"?
          </AlertDialogDescription>
        </AlertDialogHeader>

        {isUsedByIPSets && (
          <Alert variant="destructive">
            <AlertTriangle className="h-4 w-4" />
            <div className="ml-3">
              <strong>Warning:</strong> This list is referenced by the following IPSets:
              <ul className="mt-2 list-disc pl-4">
                {usedByIPSets.map((ipset) => (
                  <li key={ipset}>{ipset}</li>
                ))}
              </ul>
              <p className="mt-2 text-sm">
                You must remove these references before deleting this list.
              </p>
            </div>
          </Alert>
        )}

        {!isUsedByIPSets && (
          <p className="text-sm text-muted-foreground">
            This action cannot be undone. The list configuration will be permanently deleted.
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
            {deleteList.isPending && <Loader2 className="mr-2 h-4 w-4 animate-spin" />}
            {t('common.delete')}
          </AlertDialogAction>
        </AlertDialogFooter>
      </AlertDialogContent>
    </AlertDialog>
  );
}
