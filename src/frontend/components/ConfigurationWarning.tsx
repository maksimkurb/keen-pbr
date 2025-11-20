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

  // Check if dnsmasq has no config hash (not configured)
  const dnsmasqConfigHash = data.services.dnsmasq?.config_hash;
  const dnsmasqNotConfigured = !dnsmasqConfigHash;

  // Check if dnsmasq config hash doesn't match current config
  const dnsmasqOutdated = dnsmasqConfigHash && data.current_config_hash && dnsmasqConfigHash !== data.current_config_hash;

  const anyServiceOutdated = configOutdated || dnsmasqOutdated || dnsmasqNotConfigured;

  if (!anyServiceOutdated) {
    return null;
  }

  return (
    <Alert
      className={
        dnsmasqNotConfigured
          ? "bg-red-50 border-red-200 dark:bg-red-950 dark:border-red-800"
          : "bg-yellow-50 border-yellow-200 dark:bg-yellow-950 dark:border-yellow-800"
      }
    >
      <AlertTriangle
        className={
          dnsmasqNotConfigured
            ? "h-4 w-4 text-red-600 dark:text-red-400"
            : "h-4 w-4 text-yellow-600 dark:text-yellow-400"
        }
      />
      <AlertDescription
        className={
          dnsmasqNotConfigured
            ? "text-red-800 dark:text-red-200"
            : "text-yellow-800 dark:text-yellow-200"
        }
      >
        {dnsmasqNotConfigured
          ? t('dashboard.dnsmasqNotConfigured')
          : configOutdated && dnsmasqOutdated
          ? t('dashboard.configurationOutdatedBoth')
          : configOutdated
          ? t('dashboard.configurationOutdated')
          : t('dashboard.dnsmasqConfigurationOutdated')}
      </AlertDescription>
    </Alert>
  );
}
