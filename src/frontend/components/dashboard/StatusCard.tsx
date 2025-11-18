import { Card, CardContent, CardHeader, CardTitle } from '../ui/card';
import { Badge } from '../ui/badge';

type ServiceStatus = 'running' | 'stopped' | 'unknown';

interface StatusCardProps {
  title: string;
  value: string;
  status?: ServiceStatus;
}

const statusVariants: Record<ServiceStatus, 'default' | 'destructive' | 'secondary'> = {
  running: 'default',
  stopped: 'destructive',
  unknown: 'secondary',
};

export function StatusCard({ title, value, status }: StatusCardProps) {
  return (
    <Card>
      <CardHeader className="pb-3">
        <CardTitle className="text-sm font-medium text-muted-foreground">
          {title}
        </CardTitle>
      </CardHeader>
      <CardContent>
        <div className="flex items-center justify-between">
          <div className="text-2xl font-bold">{value}</div>
          {status && (
            <Badge variant={statusVariants[status]}>
              {status}
            </Badge>
          )}
        </div>
      </CardContent>
    </Card>
  );
}
