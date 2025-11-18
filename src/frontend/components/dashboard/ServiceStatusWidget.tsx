import { useQuery } from '@tanstack/react-query';
import { useTranslation } from 'react-i18next';
import { apiClient } from '@/src/api/client';
import { StatusCard } from './StatusCard';
import { Card, CardContent, CardHeader, CardTitle } from '../ui/card';
import { Alert, AlertDescription } from '../ui/alert';

export function ServiceStatusWidget() {
  const { t } = useTranslation();

  const { data, isLoading, error } = useQuery({
    queryKey: ['status'],
    queryFn: () => apiClient.getStatus(),
    refetchInterval: 5000, // Refresh every 5 seconds
  });

  if (error) {
    return (
      <Card>
        <CardHeader>
          <CardTitle>{t('dashboard.systemStatus')}</CardTitle>
        </CardHeader>
        <CardContent>
          <Alert variant="destructive">
            <AlertDescription>
              {error instanceof Error ? error.message : t('common.error')}
            </AlertDescription>
          </Alert>
        </CardContent>
      </Card>
    );
  }

  if (isLoading || !data) {
    return (
      <Card>
        <CardHeader>
          <CardTitle>{t('dashboard.systemStatus')}</CardTitle>
        </CardHeader>
        <CardContent>
          <div className="grid gap-4 md:grid-cols-2 lg:grid-cols-4">
            {[...Array(4)].map((_, i) => (
              <div key={i} className="h-24 bg-muted animate-pulse rounded-lg" />
            ))}
          </div>
        </CardContent>
      </Card>
    );
  }

  return (
    <Card>
      <CardHeader>
        <CardTitle>{t('dashboard.systemStatus')}</CardTitle>
      </CardHeader>
      <CardContent>
        <div className="grid gap-4 md:grid-cols-2 lg:grid-cols-4">
          <StatusCard
            title={t('dashboard.version')}
            value={`${data.version.version} (${data.version.commit})`}
          />
          <StatusCard
            title={t('dashboard.keeneticVersion')}
            value={data.keenetic_version || t('common.notAvailable')}
          />
          <StatusCard
            title={t('dashboard.keenPbrService')}
            value={data.services['keen-pbr']?.status || 'unknown'}
            status={data.services['keen-pbr']?.status || 'unknown'}
          />
          <StatusCard
            title={t('dashboard.dnsmasqService')}
            value={data.services.dnsmasq?.status || 'unknown'}
            status={data.services.dnsmasq?.status || 'unknown'}
          />
        </div>
      </CardContent>
    </Card>
  );
}
