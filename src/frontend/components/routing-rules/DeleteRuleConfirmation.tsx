import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
import { useDeleteIPSet } from '../../src/hooks/useIPSets';
import { DeleteConfirmationDialog } from '../ui/delete-confirmation-dialog';

interface DeleteRuleConfirmationProps {
  ipsetName: string | null;
  open: boolean;
  onOpenChange: (open: boolean) => void;
}

export function DeleteRuleConfirmation({
  ipsetName,
  open,
  onOpenChange,
}: DeleteRuleConfirmationProps) {
  const { t } = useTranslation();
  const deleteIPSet = useDeleteIPSet();

  const handleDelete = async () => {
    if (!ipsetName) return;

    try {
      await deleteIPSet.mutateAsync(ipsetName);
      toast.success(t('routingRules.delete.success', { name: ipsetName }));
      onOpenChange(false);
    } catch (error) {
      toast.error(t('routingRules.delete.error', { error: String(error) }));
    }
  };

  return (
    <DeleteConfirmationDialog
      open={open}
      onOpenChange={onOpenChange}
      title={t('routingRules.delete.title')}
      description={t('routingRules.delete.description', { name: ipsetName })}
      onConfirm={handleDelete}
      isPending={deleteIPSet.isPending}
    />
  );
}
