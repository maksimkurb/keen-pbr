import { ReactNode } from 'react';
import { useTranslation } from 'react-i18next';
import { Card, CardContent, CardHeader, CardTitle } from '../ui/card';
import { Badge } from '../ui/badge';

type ServiceStatus = 'running' | 'stopped' | 'unknown';

interface StatusBadge {
  label: string;
  variant: 'default' | 'destructive' | 'secondary' | 'outline';
}

interface StatusCardProps {
  title: string;
  value?: string;
  status?: ServiceStatus;
  badges?: StatusBadge[];
  actions?: ReactNode;
  className?: string;
}

const statusVariants: Record<
  ServiceStatus,
  'default' | 'destructive' | 'secondary'
> = {
  running: 'default',
  stopped: 'destructive',
  unknown: 'secondary',
};

const statusI18nKeys: Record<ServiceStatus, string> = {
  running: 'dashboard.statusRunning',
  stopped: 'dashboard.statusStopped',
  unknown: 'dashboard.statusUnknown',
};

export function StatusCard({
  title,
  value,
  status,
  badges,
  actions,
  className,
}: StatusCardProps) {
  const { t } = useTranslation();

  return (
    <Card className={className}>
      <CardHeader className="pb-3">
        <CardTitle className="text-sm font-medium text-muted-foreground">
          {title}
        </CardTitle>
      </CardHeader>
      <CardContent>
        <div className="space-y-3">
          {value && <div className="text-2xl font-bold">{value}</div>}
          {(status || badges) && (
            <div className="flex flex-wrap gap-2">
              {status && (
                <Badge variant={statusVariants[status]} className="text-xs">
                  {t(statusI18nKeys[status])}
                </Badge>
              )}
              {badges &&
                badges.map((badge, index) => (
                  <Badge
                    key={index}
                    variant={badge.variant}
                    className="text-xs"
                  >
                    {badge.label}
                  </Badge>
                ))}
            </div>
          )}
          {actions && <div className="flex flex-wrap gap-2">{actions}</div>}
        </div>
      </CardContent>
    </Card>
  );
}
