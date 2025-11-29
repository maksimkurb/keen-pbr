import { useTranslation } from 'react-i18next';
import { Loader2, Plus, Download } from 'lucide-react';
import { toast } from 'sonner';
import { useNavigate } from 'react-router-dom';
import { useLists, useDownloadAllLists } from '../hooks/useLists';
import { useIPSets } from '../hooks/useIPSets';
import { Button } from '../../components/ui/button';
import { Alert } from '../../components/ui/alert';
import { ListFilters } from '../../components/lists/ListFilters';
import { ListsTable } from '../../components/lists/ListsTable';
import { formatError } from '../utils/errorUtils';

export default function Lists() {
  const { t } = useTranslation();
  const navigate = useNavigate();
  const { data: lists, isLoading: listsLoading, error: listsError } = useLists();
  const { data: ipsets, isLoading: ipsetsLoading } = useIPSets();
  const downloadAllLists = useDownloadAllLists();

  const isLoading = listsLoading || ipsetsLoading;

  const handleDownloadAllLists = async () => {
    try {
      await downloadAllLists.mutateAsync();
      toast.success(t('lists.downloadAll.success'));
    } catch (error) {
      toast.error(t('lists.downloadAll.error', { error: formatError(error) }));
    }
  };

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
            {listsError instanceof Error ? listsError.message : t('lists.loadError')}
          </p>
        </div>
      </Alert>
    );
  }

  const ipsetNames = ipsets?.map((ipset) => ipset.ipset_name) || [];

  return (
    <div className="space-y-4 md:space-y-6">
      {/* Header */}
      <div className="flex flex-col gap-4 md:flex-row md:items-center md:justify-between">
        <div>
          <h1 className="text-2xl md:text-3xl font-bold">{t('lists.title')}</h1>
          <p className="mt-1 md:mt-2 text-sm md:text-base text-muted-foreground">
            {t('lists.description', { count: lists?.length || 0 })}
          </p>
        </div>
        <div className="flex flex-col sm:flex-row gap-2">
          <Button
            variant="outline"
            onClick={handleDownloadAllLists}
            disabled={downloadAllLists.isPending}
            className="w-full sm:w-auto"
          >
            <Download className={`mr-2 h-4 w-4 ${downloadAllLists.isPending ? 'animate-spin' : ''}`} />
            <span className="hidden sm:inline">{t('lists.downloadAll.button')}</span>
            <span className="sm:hidden">{t('lists.downloadAll.buttonShort')}</span>
          </Button>
          <Button onClick={() => navigate('/lists/new')} className="w-full sm:w-auto">
            <Plus className="mr-2 h-4 w-4" />
            {t('lists.newList')}
          </Button>
        </div>
      </div>

      {/* Filters */}
      <ListFilters ipsets={ipsetNames} />

      {/* Table */}
      {lists && ipsets && (
        <ListsTable lists={lists} ipsets={ipsets} />
      )}
    </div>
  );
}
