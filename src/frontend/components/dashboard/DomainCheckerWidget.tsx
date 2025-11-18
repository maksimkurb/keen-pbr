import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Card, CardContent, CardHeader, CardTitle } from '../ui/card';
import { Input } from '../ui/input';
import { Button } from '../ui/button';
import { Badge } from '../ui/badge';
import { Alert, AlertDescription } from '../ui/alert';
import { Search } from 'lucide-react';

export function DomainCheckerWidget() {
  const { t } = useTranslation();
  const [domain, setDomain] = useState('');
  const [results, setResults] = useState<{
    ipsets: string[];
    routing?: {
      table: number;
      interface: string;
      fwmark?: string;
    };
  } | null>(null);

  const handleCheckRouting = () => {
    // TODO: Implement API call to check domain routing
    // For now, show placeholder message
    setResults({
      ipsets: ['vpn-domains', 'streaming-sites'],
      routing: {
        table: 100,
        interface: 'wg0',
        fwmark: '0x64',
      },
    });
  };

  const handlePing = () => {
    // TODO: Implement ping functionality
    console.log('Ping:', domain);
  };

  const handleTraceroute = () => {
    // TODO: Implement traceroute functionality
    console.log('Traceroute:', domain);
  };

  return (
    <Card>
      <CardHeader>
        <CardTitle>{t('dashboard.domainChecker.title')}</CardTitle>
      </CardHeader>
      <CardContent className="space-y-4">
        <div className="flex gap-2">
          <div className="relative flex-1">
            <Search className="absolute left-3 top-1/2 h-4 w-4 -translate-y-1/2 text-muted-foreground" />
            <Input
              placeholder={t('dashboard.domainChecker.placeholder')}
              value={domain}
              onChange={(e) => setDomain(e.target.value)}
              className="pl-9"
            />
          </div>
        </div>

        <div className="flex gap-2">
          <Button
            onClick={handleCheckRouting}
            disabled={!domain}
            variant="default"
          >
            {t('dashboard.domainChecker.checkRouting')}
          </Button>
          <Button
            onClick={handlePing}
            disabled={!domain}
            variant="outline"
          >
            {t('dashboard.domainChecker.ping')}
          </Button>
          <Button
            onClick={handleTraceroute}
            disabled={!domain}
            variant="outline"
          >
            {t('dashboard.domainChecker.traceroute')}
          </Button>
        </div>

        {results && (
          <div className="space-y-3 rounded-lg border p-4">
            <div>
              <div className="text-sm font-medium mb-2">
                {t('dashboard.domainChecker.foundInIPSets')}
              </div>
              <div className="flex gap-2 flex-wrap">
                {results.ipsets.map((ipset) => (
                  <Badge key={ipset} variant="secondary">
                    {ipset}
                  </Badge>
                ))}
              </div>
            </div>

            {results.routing && (
              <div>
                <div className="text-sm font-medium mb-2">
                  {t('dashboard.domainChecker.routing')}
                </div>
                <div className="text-sm text-muted-foreground">
                  {t('dashboard.domainChecker.routeInfo', {
                    table: results.routing.table,
                    interface: results.routing.interface,
                  })}
                  {results.routing.fwmark && (
                    <span className="ml-2">
                      (fwmark: {results.routing.fwmark})
                    </span>
                  )}
                </div>
              </div>
            )}

            <Alert>
              <AlertDescription className="text-xs">
                {t('dashboard.domainChecker.placeholderNote')}
              </AlertDescription>
            </Alert>
          </div>
        )}
      </CardContent>
    </Card>
  );
}
