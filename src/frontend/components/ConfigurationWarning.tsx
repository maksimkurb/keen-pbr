import { useTranslation } from 'react-i18next';
import { AlertTriangle, ArrowRightIcon } from 'lucide-react';
import { Alert, AlertDescription } from './ui/alert';
import { useStatus } from '../src/hooks/useStatus';
import { Link, useLocation } from 'react-router-dom';

export function ConfigurationWarning() {
  const { t } = useTranslation();
  const { data, isLoading } = useStatus();

  // Don't show anything while loading or if no data
  if (isLoading || !data) {
    return null;
  }

  // Check if configuration is outdated
  const configOutdated = data.configuration_outdated || false;

  if (!configOutdated) {
    return null;
  }

  const location = useLocation();

  return (
    <Alert className="bg-yellow-50 border-yellow-200 dark:bg-yellow-950 dark:border-yellow-800 mb-4">
      <AlertTriangle className="h-4 w-4 text-yellow-600 dark:text-yellow-400" />
      <AlertDescription>
        <span className="text-yellow-800 dark:text-yellow-200">
          {t('dashboard.configurationOutdated')}
        </span>

        {location.pathname !== '/' && (
          <Link to="/" className="text-blue-600 dark:text-blue-400">
            {t('dashboard.configurationOutdatedLink')}{' '}
            <ArrowRightIcon className="inline w-[1em] h-[1em]" />
          </Link>
        )}
      </AlertDescription>
    </Alert>
  );
}
