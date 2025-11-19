import { useState, useMemo } from 'react';
import { useTranslation } from 'react-i18next';
import { useSearchParams, useNavigate } from 'react-router-dom';
import { Pencil, Trash2, Check, X } from 'lucide-react';
import { Button } from '../ui/button';
import { Badge } from '../ui/badge';
import { RuleDialog } from './RuleDialog';
import { DeleteRuleConfirmation } from './DeleteRuleConfirmation';
import { useLists } from '../../src/hooks/useLists';
import type { IPSetConfig } from '../../src/api/client';

interface RoutingRulesTableProps {
  ipsets: IPSetConfig[];
}

export function RoutingRulesTable({ ipsets }: RoutingRulesTableProps) {
  const { t } = useTranslation();
  const navigate = useNavigate();
  const [searchParams] = useSearchParams();
  const [editingIPSet, setEditingIPSet] = useState<IPSetConfig | null>(null);
  const [deletingIPSet, setDeletingIPSet] = useState<string | null>(null);
  const { data: lists } = useLists();

  const availableLists = lists?.map((l) => l.list_name) || [];

  // Get filter values from URL
  const searchQuery = searchParams.get('search')?.toLowerCase() || '';
  const listFilter = searchParams.get('list') || '';
  const versionFilter = searchParams.get('version') || '';

  // Filter ipsets based on search and filters
  const filteredIPSets = useMemo(() => {
    return ipsets.filter((ipset) => {
      // Text search filter
      if (searchQuery && !ipset.ipset_name.toLowerCase().includes(searchQuery)) {
        return false;
      }

      // List filter
      if (listFilter && !ipset.lists.includes(listFilter)) {
        return false;
      }

      // Version filter
      if (versionFilter && ipset.ip_version.toString() !== versionFilter) {
        return false;
      }

      return true;
    });
  }, [ipsets, searchQuery, listFilter, versionFilter]);

  const getVersionBadgeVariant = (version: number) => {
    return version === 4 ? 'default' : 'secondary';
  };

  const handleListClick = (listName: string) => {
    navigate(`/lists?list=${encodeURIComponent(listName)}`);
  };

  if (filteredIPSets.length === 0) {
    return (
      <div className="flex flex-col items-center justify-center py-12 text-center">
        <p className="text-lg font-semibold">{t('routingRules.empty.title')}</p>
        <p className="mt-2 text-sm text-muted-foreground">
          {searchQuery || listFilter || versionFilter
            ? t('routingRules.empty.clearFilters')
            : t('routingRules.empty.description')}
        </p>
      </div>
    );
  }

  return (
    <>
      <div className="rounded-md border overflow-x-auto">
        <table className="w-full min-w-[800px]">
          <thead>
            <tr className="border-b bg-muted/50">
              <th className="p-3 text-left text-sm font-medium">{t('routingRules.columns.name')}</th>
              <th className="p-3 text-left text-sm font-medium">{t('routingRules.columns.routing')}</th>
              <th className="p-3 text-left text-sm font-medium">{t('routingRules.columns.version')}</th>
              <th className="p-3 text-left text-sm font-medium">{t('routingRules.columns.lists')}</th>
              <th className="p-3 text-left text-sm font-medium">{t('routingRules.columns.interfaces')}</th>
              <th className="p-3 text-left text-sm font-medium">{t('routingRules.columns.killSwitch')}</th>
              <th className="p-3 text-right text-sm font-medium">{t('routingRules.columns.actions')}</th>
            </tr>
          </thead>
          <tbody>
            {filteredIPSets.map((ipset) => (
              <tr key={ipset.ipset_name} className="border-b last:border-0 hover:bg-muted/50">
                <td className="p-3">
                  <span className="font-medium">{ipset.ipset_name}</span>
                </td>
                <td className="p-3">
                  {ipset.routing ? (
                    <div className="text-sm">
                      <div>priority: {ipset.routing.priority}, table: {ipset.routing.table}</div>
                      <div className="text-muted-foreground">fwmark: {ipset.routing.fwmark}</div>
                    </div>
                  ) : (
                    <span className="text-sm text-muted-foreground">-</span>
                  )}
                </td>
                <td className="p-3">
                  <Badge variant={getVersionBadgeVariant(ipset.ip_version)}>
                    {t(`routingRules.ipVersion.${ipset.ip_version}`)}
                  </Badge>
                </td>
                <td className="p-3">
                  <div className="flex flex-wrap gap-1">
                    {ipset.lists.map((list) => (
                      <Badge
                        key={list}
                        variant="outline"
                        className="cursor-pointer hover:bg-accent"
                        onClick={() => handleListClick(list)}
                      >
                        {list}
                      </Badge>
                    ))}
                  </div>
                </td>
                <td className="p-3">
                  {ipset.routing?.interfaces ? (
                    <div className="flex flex-wrap gap-1">
                      {ipset.routing.interfaces.map((iface, index) => (
                        <Badge key={`${iface}-${index}`} variant="secondary">
                          {iface}
                        </Badge>
                      ))}
                    </div>
                  ) : (
                    <span className="text-sm text-muted-foreground">-</span>
                  )}
                </td>
                <td className="p-3">
                  {ipset.routing?.kill_switch !== undefined ? (
                    ipset.routing.kill_switch ? (
                      <Check className="h-5 w-5 text-green-600" />
                    ) : (
                      <X className="h-5 w-5 text-muted-foreground" />
                    )
                  ) : (
                    <span className="text-sm text-muted-foreground">-</span>
                  )}
                </td>
                <td className="p-3">
                  <div className="flex justify-end gap-2">
                    <Button
                      variant="ghost"
                      size="sm"
                      onClick={() => setEditingIPSet(ipset)}
                    >
                      <Pencil className="h-4 w-4" />
                    </Button>
                    <Button
                      variant="ghost"
                      size="sm"
                      onClick={() => setDeletingIPSet(ipset.ipset_name)}
                    >
                      <Trash2 className="h-4 w-4" />
                    </Button>
                  </div>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      <RuleDialog
        ipset={editingIPSet}
        open={!!editingIPSet}
        onOpenChange={(open) => !open && setEditingIPSet(null)}
        availableLists={availableLists}
      />

      <DeleteRuleConfirmation
        ipsetName={deletingIPSet}
        open={!!deletingIPSet}
        onOpenChange={(open) => !open && setDeletingIPSet(null)}
      />
    </>
  );
}
