import { useTranslation } from 'react-i18next';
import { Card, CardContent, CardHeader, CardTitle } from '../ui/card';
import { Alert, AlertDescription } from '../ui/alert';
import { useStatus } from '@/src/hooks/useStatus';

export function KeeneticWidget() {
  const { t } = useTranslation();
  const { data, isLoading, error } = useStatus();

  if (error) {
    return (
      <Card>
        <CardHeader>
          <CardTitle>{t('dashboard.keeneticVersion')}</CardTitle>
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
          <CardTitle>{t('dashboard.keeneticVersion')}</CardTitle>
        </CardHeader>
        <CardContent>
          <div className="h-20 bg-muted animate-pulse rounded-lg" />
        </CardContent>
      </Card>
    );
  }

  return (
    <Card>
      <CardHeader>
        <CardTitle>{t('dashboard.keeneticVersion')}</CardTitle>
      </CardHeader>
      <CardContent className="space-y-4">
        <div className="text-2xl font-semibold">
          {data.keenetic_version || t('common.notAvailable')}
        </div>

        {/* DNS Servers section */}
        {data.dns_servers && data.dns_servers.length > 0 && (
          <div>
            <h3 className="text-sm font-medium text-muted-foreground mb-2">
              {t('dashboard.dnsServers')}
            </h3>
            <div className="space-y-2">
              {data.dns_servers.map((server, index) => (
                <div key={index} className="flex items-center gap-2 text-sm">
                  <span className="font-mono text-muted-foreground text-xs">
                    {server}
                  </span>
                </div>
              ))}
            </div>
          </div>
        )}
      </CardContent>
    </Card>
  );
}
