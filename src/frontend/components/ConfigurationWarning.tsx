import { AlertTriangle, ArrowRightIcon } from 'lucide-react';
import { useTranslation } from 'react-i18next';
import { Link, useLocation } from 'react-router-dom';
import { useStatus } from '../src/hooks/useStatus';
import { Alert, AlertDescription } from './ui/alert';

export function ConfigurationWarning() {
  const { t } = useTranslation();
  const { data, isLoading } = useStatus();
  const location = useLocation();

  // Don't show anything while loading or if no data
  if (isLoading || !data) {
    return null;
  }

  // Check if configuration is outdated
  const configOutdated = data.configuration_outdated || false;

  if (!configOutdated) {
    return null;
  }

  return (
    <Alert variant="warning" className="mb-4">
      <AlertTriangle className="h-4 w-4" />
      <AlertDescription>
        {t('dashboard.configurationOutdated')}

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
