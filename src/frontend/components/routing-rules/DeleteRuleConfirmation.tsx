import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
import { Loader2 } from 'lucide-react';
import { useDeleteIPSet } from '../../src/hooks/useIPSets';
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

interface DeleteRuleConfirmationProps {
  ipsetName: string | null;
  open: boolean;
  onOpenChange: (open: boolean) => void;
}

export function DeleteRuleConfirmation({ ipsetName, open, onOpenChange }: DeleteRuleConfirmationProps) {
  const { t } = useTranslation();
  const deleteIPSet = useDeleteIPSet();

  const handleDelete = async () => {
    if (!ipsetName) return;

    try {
      await deleteIPSet.mutateAsync(ipsetName);
      toast.success(`Routing rule "${ipsetName}" deleted successfully`);
      onOpenChange(false);
    } catch (error) {
      toast.error(`Failed to delete routing rule: ${String(error)}`);
    }
  };

  return (
    <AlertDialog open={open} onOpenChange={onOpenChange}>
      <AlertDialogContent>
        <AlertDialogHeader>
          <AlertDialogTitle>Delete Routing Rule</AlertDialogTitle>
          <AlertDialogDescription>
            Are you sure you want to delete the routing rule "{ipsetName}"? This action cannot be undone and will remove all associated IPSet and routing configurations.
          </AlertDialogDescription>
        </AlertDialogHeader>
        <AlertDialogFooter>
          <AlertDialogCancel disabled={deleteIPSet.isPending}>
            {t('common.cancel')}
          </AlertDialogCancel>
          <AlertDialogAction
            onClick={(e) => {
              e.preventDefault();
              handleDelete();
            }}
            disabled={deleteIPSet.isPending}
            className="bg-destructive text-destructive-foreground hover:bg-destructive/90"
          >
            {deleteIPSet.isPending && <Loader2 className="mr-2 h-4 w-4 animate-spin" />}
            {t('common.delete')}
          </AlertDialogAction>
        </AlertDialogFooter>
      </AlertDialogContent>
    </AlertDialog>
  );
}
