import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Loader2, Plus } from 'lucide-react';
import { useLists } from '../hooks/useLists';
import { useIPSets } from '../hooks/useIPSets';
import { Button } from '../../components/ui/button';
import { Alert } from '../../components/ui/alert';
import { ListFilters } from '../../components/lists/ListFilters';
import { ListsTable } from '../../components/lists/ListsTable';
import { CreateListDialog } from '../../components/lists/CreateListDialog';

export default function Lists() {
  const { t } = useTranslation();
  const { data: lists, isLoading: listsLoading, error: listsError } = useLists();
  const { data: ipsets, isLoading: ipsetsLoading } = useIPSets();
  const [createDialogOpen, setCreateDialogOpen] = useState(false);

  const isLoading = listsLoading || ipsetsLoading;

  if (isLoading) {
    return (
      <div className="flex items-center justify-center py-12">
        <Loader2 className="h-8 w-8 animate-spin text-muted-foreground" />
      </div>
    );
  }

  if (listsError) {
    return (
      <Alert variant="destructive">
        <div>
          <strong>{t('common.error')}</strong>
          <p className="mt-1 text-sm">
            {listsError instanceof Error ? listsError.message : 'Failed to load lists'}
          </p>
        </div>
      </Alert>
    );
  }

  const ipsetNames = ipsets?.map((ipset) => ipset.ipset_name) || [];

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex items-center justify-between">
        <div>
          <h1 className="text-3xl font-bold">{t('lists.title')}</h1>
          <p className="mt-2 text-muted-foreground">
            Manage IP/domain lists ({lists?.length || 0})
          </p>
        </div>
        <Button onClick={() => setCreateDialogOpen(true)}>
          <Plus className="mr-2 h-4 w-4" />
          {t('lists.newList')}
        </Button>
      </div>

      {/* Filters */}
      <ListFilters ipsets={ipsetNames} />

      {/* Table */}
      {lists && ipsets && (
        <ListsTable lists={lists} ipsets={ipsets} />
      )}

      {/* Create Dialog */}
      <CreateListDialog
        open={createDialogOpen}
        onOpenChange={setCreateDialogOpen}
      />
    </div>
  );
}
