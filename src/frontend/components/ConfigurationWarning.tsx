import { useTranslation } from 'react-i18next';
import { AlertTriangle } from 'lucide-react';
import { Alert, AlertDescription } from './ui/alert';
import { useStatus } from '../src/hooks/useStatus';

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

  return (
    <Alert className="bg-yellow-50 border-yellow-200 dark:bg-yellow-950 dark:border-yellow-800">
      <AlertTriangle className="h-4 w-4 text-yellow-600 dark:text-yellow-400" />
      <AlertDescription className="text-yellow-800 dark:text-yellow-200">
        {t('dashboard.configurationOutdated')}
      </AlertDescription>
    </Alert>
  );
}
