import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Plus } from 'lucide-react';
import { Button } from '../../components/ui/button';
import { useIPSets } from '../hooks/useIPSets';
import { useLists } from '../hooks/useLists';
import { RuleFilters } from '../../components/routing-rules/RuleFilters';
import { RoutingRulesTable } from '../../components/routing-rules/RoutingRulesTable';
import { CreateRuleDialog } from '../../components/routing-rules/CreateRuleDialog';

export default function RoutingRules() {
  const { t } = useTranslation();
  const { data: ipsets, isLoading: ipsetsLoading, error: ipsetsError } = useIPSets();
  const { data: lists } = useLists();
  const [createDialogOpen, setCreateDialogOpen] = useState(false);

  const availableLists = lists?.map((l) => l.list_name) || [];

  if (ipsetsLoading) {
    return (
      <div className="flex items-center justify-center py-12">
        <p className="text-muted-foreground">{t('common.loading')}</p>
      </div>
    );
  }

  if (ipsetsError) {
    return (
      <div className="flex items-center justify-center py-12">
        <p className="text-destructive">
          {t('common.error')}: {String(ipsetsError)}
        </p>
      </div>
    );
  }

  return (
    <div className="space-y-6">
      {/* Header */}
      <div className="flex items-start justify-between">
        <div>
          <h1 className="text-3xl font-bold">{t('routingRules.title')}</h1>
          <p className="text-muted-foreground mt-2">
            Manage IPSet routing configurations for policy-based routing
          </p>
        </div>
        <Button onClick={() => setCreateDialogOpen(true)}>
          <Plus className="mr-2 h-4 w-4" />
          {t('routingRules.newRule')}
        </Button>
      </div>

      {/* Filters */}
      <RuleFilters lists={availableLists} />

      {/* Table */}
      <RoutingRulesTable ipsets={ipsets || []} />

      {/* Create Dialog */}
      <CreateRuleDialog
        open={createDialogOpen}
        onOpenChange={setCreateDialogOpen}
        availableLists={availableLists}
      />
    </div>
  );
}
