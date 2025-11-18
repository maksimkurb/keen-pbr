import { useState } from 'react';
import { useQuery, useMutation, useQueryClient } from '@tanstack/react-query';
import { useTranslation } from 'react-i18next';
import { apiClient } from '@/src/api/client';
import { StatusCard } from './StatusCard';
import { Card, CardContent, CardHeader, CardTitle } from '../ui/card';
import { Alert, AlertDescription } from '../ui/alert';
import { Button } from '../ui/button';
import { Play, Square, RotateCw } from 'lucide-react';

export function ServiceStatusWidget() {
  const { t } = useTranslation();
  const queryClient = useQueryClient();
  const [controlLoading, setControlLoading] = useState<string | null>(null);

  const { data, isLoading, error } = useQuery({
    queryKey: ['status'],
    queryFn: () => apiClient.getStatus(),
    refetchInterval: 5000, // Refresh every 5 seconds
  });

  const handleServiceControl = async (service: string, action: 'start' | 'stop' | 'restart') => {
    setControlLoading(`${service}-${action}`);
    try {
      if (service === 'keen-pbr') {
        if (action === 'start') {
          await apiClient.controlService('started');
        } else if (action === 'stop') {
          await apiClient.controlService('stopped');
        } else if (action === 'restart') {
          await apiClient.controlService('restarted');
        }
      } else if (service === 'dnsmasq' && action === 'restart') {
        await apiClient.restartDnsmasq();
      }

      // Wait a bit before refreshing status to allow service to change state
      await new Promise(resolve => setTimeout(resolve, 500));
      queryClient.invalidateQueries({ queryKey: ['status'] });
    } catch (err) {
      console.error(`Failed to ${action} ${service}:`, err);
      // TODO: Show error toast notification
    } finally {
      setControlLoading(null);
    }
  };

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

  const keenPbrStatus = data.services['keen-pbr']?.status || 'unknown';
  const dnsmasqStatus = data.services.dnsmasq?.status || 'unknown';

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
            status={keenPbrStatus}
            actions={
              <>
                <Button
                  size="sm"
                  variant="outline"
                  onClick={() => handleServiceControl('keen-pbr', 'start')}
                  disabled={keenPbrStatus === 'running' || controlLoading === 'keen-pbr-start'}
                >
                  <Play className="h-3 w-3 mr-1" />
                  {t('common.start')}
                </Button>
                <Button
                  size="sm"
                  variant="outline"
                  onClick={() => handleServiceControl('keen-pbr', 'stop')}
                  disabled={keenPbrStatus === 'stopped' || controlLoading === 'keen-pbr-stop'}
                >
                  <Square className="h-3 w-3 mr-1" />
                  {t('common.stop')}
                </Button>
                <Button
                  size="sm"
                  variant="outline"
                  onClick={() => handleServiceControl('keen-pbr', 'restart')}
                  disabled={controlLoading === 'keen-pbr-restart'}
                >
                  <RotateCw className="h-3 w-3 mr-1" />
                  {t('common.restart')}
                </Button>
              </>
            }
          />
          <StatusCard
            title={t('dashboard.dnsmasqService')}
            status={dnsmasqStatus}
            actions={
              <Button
                size="sm"
                variant="outline"
                onClick={() => handleServiceControl('dnsmasq', 'restart')}
                disabled={controlLoading === 'dnsmasq-restart'}
              >
                <RotateCw className="h-3 w-3 mr-1" />
                {t('common.restart')}
              </Button>
            }
          />
        </div>
      </CardContent>
    </Card>
  );
}
