import { Search, X } from 'lucide-react';
import { useTranslation } from 'react-i18next';
import { useSearchParams } from 'react-router-dom';
import { Button } from '../ui/button';
import { Input } from '../ui/input';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '../ui/select';

interface ListFiltersProps {
  ipsets: string[]; // List of all available ipsets for filtering
}

export function ListFilters({ ipsets }: ListFiltersProps) {
  const { t } = useTranslation();
  const [searchParams, setSearchParams] = useSearchParams();

  const searchQuery = searchParams.get('search') || '';
  const usedInRule = searchParams.get('rule') || '';

  const hasActiveFilters = searchQuery || usedInRule;

  const handleSearchChange = (value: string) => {
    const newParams = new URLSearchParams(searchParams);
    if (value) {
      newParams.set('search', value);
    } else {
      newParams.delete('search');
    }
    setSearchParams(newParams);
  };

  const handleRuleFilterChange = (value: string) => {
    const newParams = new URLSearchParams(searchParams);
    if (value && value !== 'all') {
      newParams.set('rule', value);
    } else {
      newParams.delete('rule');
    }
    setSearchParams(newParams);
  };

  const clearFilters = () => {
    setSearchParams(new URLSearchParams());
  };

  return (
    <div className="flex flex-col gap-4 md:flex-row md:items-center md:justify-between">
      <div className="flex flex-1 flex-col gap-4 md:flex-row md:items-center">
        {/* Search Input */}
        <div className="relative flex-1 sm:max-w-xs">
          <Search className="absolute left-3 top-1/2 h-4 w-4 -translate-y-1/2 text-muted-foreground" />
          <Input
            type="text"
            placeholder={t('lists.searchPlaceholder')}
            value={searchQuery}
            onChange={(e) => handleSearchChange(e.target.value)}
            className="pl-9"
          />
        </div>

        <Select
          value={usedInRule || 'all'}
          onValueChange={handleRuleFilterChange}
        >
          <SelectTrigger className="md:w-48">
            <SelectValue placeholder={t('lists.usedInFilter')} />
          </SelectTrigger>
          <SelectContent>
            <SelectItem value="all">{t('lists.allRules')}</SelectItem>
            {ipsets.map((ipset) => (
              <SelectItem key={ipset} value={ipset}>
                {ipset}
              </SelectItem>
            ))}
          </SelectContent>
        </Select>

        {hasActiveFilters && (
          <Button variant="ghost" className='text-destructive' onClick={clearFilters}>
            <X className="h-4 w-4" />
            {t('lists.filters.clearFilters')}
          </Button>
        )}
      </div>
    </div>
  );
}
