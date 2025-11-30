import { useState, useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import { useSearchParams, useNavigate } from 'react-router-dom';
import { Pencil, Trash2, ExternalLink, RefreshCw } from 'lucide-react';
import { toast } from 'sonner';
import { Button } from '../ui/button';
import { Badge } from '../ui/badge';
import { StatsDisplay } from '../shared/StatsDisplay';
import { DeleteListConfirmation } from './DeleteListConfirmation';
import { useDownloadList } from '../../src/hooks/useLists';
import type { ListInfo, IPSetConfig } from '../../src/api/client';

interface ListsTableProps {
  lists: ListInfo[];
  ipsets: IPSetConfig[];
}

export function ListsTable({ lists, ipsets }: ListsTableProps) {
  const { t } = useTranslation();
  const navigate = useNavigate();
  const [searchParams] = useSearchParams();
  const [deletingList, setDeletingList] = useState<string | null>(null);
  const downloadList = useDownloadList();

  const handleDownloadList = async (listName: string) => {
    try {
      const result = await downloadList.mutateAsync(listName);
      if (result.changed) {
        toast.success(t('lists.download.success', { name: listName }));
      } else {
        toast.info(t('lists.download.unchanged', { name: listName }));
      }
    } catch (error) {
      toast.error(t('lists.download.error', { error: String(error) }));
    }
  };

  // Get filter values from URL
  const searchQuery = searchParams.get('search')?.toLowerCase() || '';
  const ruleFilter = searchParams.get('rule') || '';

  // Filter lists based on search and rule filters
  const filteredLists = useMemo(() => {
    return lists.filter((list) => {
      // Text search filter
      if (searchQuery && !list.list_name.toLowerCase().includes(searchQuery)) {
        return false;
      }

      // Rule filter (used in ipset)
      if (ruleFilter) {
        const usedInIPSets = ipsets
          .filter((ipset) => ipset.lists.includes(list.list_name))
          .map((ipset) => ipset.ipset_name);

        if (!usedInIPSets.includes(ruleFilter)) {
          return false;
        }
      }

      return true;
    });
  }, [lists, ipsets, searchQuery, ruleFilter]);

  // Get IPSets that use a specific list
  const getIPSetsUsingList = (listName: string) => {
    return ipsets
      .filter((ipset) => ipset.lists.includes(listName))
      .map((ipset) => ipset.ipset_name);
  };

  const getTypeBadgeVariant = (type: string) => {
    switch (type) {
      case 'url':
        return 'default';
      case 'file':
        return 'secondary';
      case 'hosts':
        return 'outline';
      default:
        return 'secondary';
    }
  };

  const formatLastModified = (lastModified?: string) => {
    if (!lastModified) return null;
    const date = new Date(lastModified);
    return date.toLocaleString();
  };

  if (filteredLists.length === 0) {
    return (
      <div className="flex flex-col items-center justify-center py-12 text-center">
        <p className="text-lg font-semibold">{t('lists.empty.title')}</p>
        <p className="mt-2 text-sm text-muted-foreground">
          {searchQuery || ruleFilter
            ? t('lists.empty.clearFilters')
            : t('lists.empty.description')}
        </p>
      </div>
    );
  }

  return (
    <>
      <div className="rounded-md border overflow-x-auto">
        <table className="w-full min-w-[640px]">
          <thead>
            <tr className="border-b bg-muted/50">
              <th className="p-3 text-left text-sm font-medium">
                {t('lists.columns.name')}
              </th>
              <th className="p-3 text-left text-sm font-medium">
                {t('lists.columns.type')}
              </th>
              <th className="p-3 text-left text-sm font-medium">
                {t('lists.columns.stats')}
              </th>
              <th className="p-3 text-left text-sm font-medium">
                {t('lists.columns.rules')}
              </th>
              <th className="p-3 text-right text-sm font-medium">
                {t('lists.columns.actions')}
              </th>
            </tr>
          </thead>
          <tbody>
            {filteredLists.map((list) => {
              const usedByIPSets = getIPSetsUsingList(list.list_name);

              return (
                <tr
                  key={list.list_name}
                  className="border-b last:border-0 hover:bg-muted/50"
                >
                  <td className="p-3">
                    <div className="flex items-center gap-2">
                      <span className="font-medium">{list.list_name}</span>
                      {list.url && (
                        <a
                          href={list.url}
                          target="_blank"
                          rel="noopener noreferrer"
                          className="text-muted-foreground hover:text-foreground"
                        >
                          <ExternalLink className="h-3 w-3" />
                        </a>
                      )}
                    </div>
                    {list.file && (
                      <div className="mt-1 text-xs text-muted-foreground">
                        {list.file}
                      </div>
                    )}
                    {list.url && list.stats?.last_modified && (
                      <div className="mt-1 text-xs text-muted-foreground">
                        {t('lists.lastUpdated')}{' '}
                        {formatLastModified(list.stats.last_modified)}
                      </div>
                    )}
                  </td>
                  <td className="p-3">
                    <Badge variant={getTypeBadgeVariant(list.type)}>
                      {t(`lists.types.${list.type}`)}
                    </Badge>
                  </td>
                  <td className="p-3">
                    {list.stats ? (
                      <StatsDisplay
                        totalHosts={list.stats.total_hosts}
                        ipv4Subnets={list.stats.ipv4_subnets}
                        ipv6Subnets={list.stats.ipv6_subnets}
                      />
                    ) : (
                      <span className="text-sm text-muted-foreground">-</span>
                    )}
                  </td>
                  <td className="p-3">
                    {usedByIPSets.length > 0 ? (
                      <span className="text-sm text-muted-foreground">
                        {t('lists.ruleCount', { count: usedByIPSets.length })}
                      </span>
                    ) : (
                      <span className="text-sm text-muted-foreground">-</span>
                    )}
                  </td>
                  <td className="p-3">
                    <div className="flex justify-end gap-2">
                      {list.url && (
                        <Button
                          variant="ghost"
                          size="sm"
                          onClick={() => handleDownloadList(list.list_name)}
                          disabled={downloadList.isPending}
                          title={t('lists.refreshTitle')}
                        >
                          <RefreshCw
                            className={`h-4 w-4 ${downloadList.isPending ? 'animate-spin' : ''}`}
                          />
                        </Button>
                      )}
                      <Button
                        variant="ghost"
                        size="sm"
                        onClick={() =>
                          navigate(`/lists/${list.list_name}/edit`)
                        }
                      >
                        <Pencil className="h-4 w-4" />
                      </Button>
                      <Button
                        variant="ghost"
                        size="sm"
                        onClick={() => setDeletingList(list.list_name)}
                      >
                        <Trash2 className="h-4 w-4" />
                      </Button>
                    </div>
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>

      <DeleteListConfirmation
        listName={deletingList}
        open={!!deletingList}
        onOpenChange={(open) => !open && setDeletingList(null)}
        usedByIPSets={deletingList ? getIPSetsUsingList(deletingList) : []}
      />
    </>
  );
}
