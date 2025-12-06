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

interface RuleFiltersProps {
  lists: string[];
}

export function RuleFilters({ lists }: RuleFiltersProps) {
  const { t } = useTranslation();
  const [searchParams, setSearchParams] = useSearchParams();

  const searchQuery = searchParams.get('search') || '';
  const listFilter = searchParams.get('list') || '';
  const versionFilter = searchParams.get('version') || '';

  const updateSearch = (value: string) => {
    const newParams = new URLSearchParams(searchParams);
    if (value) {
      newParams.set('search', value);
    } else {
      newParams.delete('search');
    }
    setSearchParams(newParams);
  };

  const updateListFilter = (value: string) => {
    const newParams = new URLSearchParams(searchParams);
    if (value && value !== 'all') {
      newParams.set('list', value);
    } else {
      newParams.delete('list');
    }
    setSearchParams(newParams);
  };

  const updateVersionFilter = (value: string) => {
    const newParams = new URLSearchParams(searchParams);
    if (value && value !== 'all') {
      newParams.set('version', value);
    } else {
      newParams.delete('version');
    }
    setSearchParams(newParams);
  };

  const clearFilters = () => {
    setSearchParams(new URLSearchParams());
  };

  const hasActiveFilters = searchQuery || listFilter || versionFilter;

  return (
    <div className="flex flex-col gap-4 sm:flex-row sm:items-end sm:justify-between">
      <div className="flex flex-col gap-4 sm:flex-row sm:flex-1">
        {/* Search Input */}
        <div className="relative flex-1 sm:max-w-xs">
          <Search className="absolute left-3 top-1/2 h-4 w-4 -translate-y-1/2 text-muted-foreground" />
          <Input
            type="text"
            placeholder={t('routingRules.searchPlaceholder')}
            value={searchQuery}
            onChange={(e) => updateSearch(e.target.value)}
            className="pl-9"
          />
        </div>

        {/* List Filter */}
        <div className="w-full sm:w-48">
          <Select value={listFilter || 'all'} onValueChange={updateListFilter}>
            <SelectTrigger className="w-full">
              <SelectValue placeholder={t('routingRules.allLists')} />
            </SelectTrigger>
            <SelectContent>
              <SelectItem value="all">{t('routingRules.allLists')}</SelectItem>
              {lists.map((list) => (
                <SelectItem key={list} value={list}>
                  {list}
                </SelectItem>
              ))}
            </SelectContent>
          </Select>
        </div>

        {/* Version Filter */}
        <div className="w-full sm:w-40">
          <Select
            value={versionFilter || 'all'}
            onValueChange={updateVersionFilter}
          >
            <SelectTrigger className="w-full">
              <SelectValue placeholder={t('routingRules.allVersions')} />
            </SelectTrigger>
            <SelectContent>
              <SelectItem value="all">
                {t('routingRules.allVersions')}
              </SelectItem>
              <SelectItem value="4">{t('routingRules.ipVersion.4')}</SelectItem>
              <SelectItem value="6">{t('routingRules.ipVersion.6')}</SelectItem>
            </SelectContent>
          </Select>
        </div>

        {/* Clear Filters Button */}
        {hasActiveFilters && (
          <Button variant="ghost" onClick={clearFilters}>
            <X className="h-4 w-4" />
            {t('routingRules.filters.clearFilters')}
          </Button>
        )}
      </div>
    </div>
  );
}
