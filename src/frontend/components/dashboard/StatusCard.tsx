import { ReactNode } from 'react';
import { Card, CardContent, CardHeader, CardTitle } from '../ui/card';
import { Badge } from '../ui/badge';

type ServiceStatus = 'running' | 'stopped' | 'unknown';

interface StatusCardProps {
  title: string;
  value?: string;
  status?: ServiceStatus;
  actions?: ReactNode;
  className?: string;
}

const statusVariants: Record<ServiceStatus, 'default' | 'destructive' | 'secondary'> = {
  running: 'default',
  stopped: 'destructive',
  unknown: 'secondary',
};

export function StatusCard({ title, value, status, actions, className }: StatusCardProps) {
  return (
    <Card className={className}>
      <CardHeader className="pb-3">
        <CardTitle className="text-sm font-medium text-muted-foreground">
          {title}
        </CardTitle>
      </CardHeader>
      <CardContent>
        <div className="space-y-3">
          {value && (
            <div className="text-2xl font-bold">{value}</div>
          )}
          {status && (
            <Badge variant={statusVariants[status]} className="text-xs">
              {status}
            </Badge>
          )}
          {actions && (
            <div className="flex flex-wrap gap-2">
              {actions}
            </div>
          )}
        </div>
      </CardContent>
    </Card>
  );
}
