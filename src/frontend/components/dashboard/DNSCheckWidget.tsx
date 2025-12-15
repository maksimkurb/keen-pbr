import { AlertCircle, CheckCircle2, Loader2 } from 'lucide-react';
import { useEffect, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useDNSCheck } from '../../src/hooks/useDNSCheck';
import { useStatus } from '../../src/hooks/useStatus';
import { Button } from '../ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '../ui/card';
import { Separator } from '../ui/separator';
import { DNSCheckModal } from './DNSCheckModal';

export function DNSCheckWidget() {
  const { t } = useTranslation();
  const [showPCCheckDialog, setShowPCCheckDialog] = useState(false);
  const { data, isLoading, error } = useStatus();

  // Main widget DNS check (browser-based)
  const { status, startCheck, reset } = useDNSCheck();

  // Auto-run browser DNS check on component mount
  useEffect(() => {
    startCheck(true); // performBrowserRequest = true
  }, [startCheck]);

  const isChecking = status === 'checking';

  const handleRetry = () => {
    reset();
    startCheck(true);
  };

  const getCardClassName = () => {
    switch (status) {
      case 'success':
      case 'pc-success':
        return '';
      case 'browser-fail':
      case 'sse-fail':
        return 'border-destructive bg-destructive/10';
      default:
        return '';
    }
  };

  const renderDNSCheckContent = () => {
    switch (status) {
      case 'idle':
      case 'checking':
        return (
          <div className="flex items-center justify-center py-4">
            <Loader2 className="h-8 w-8 animate-spin" />
          </div>
        );

      case 'success':
        return (
          <div className="flex items-center gap-2 text-success">
            <CheckCircle2 className="h-5 w-5" />
            <span>{t('dnsCheck.success')}</span>
          </div>
        );

      case 'browser-fail':
        return (
          <div className="flex items-center gap-2 text-destructive">
            <AlertCircle className="h-5 w-5" />
            <span>{t('dnsCheck.browserFail')}</span>
          </div>
        );

      case 'sse-fail':
        return (
          <div className="flex items-center gap-2 text-destructive">
            <AlertCircle className="h-5 w-5" />
            <span>{t('dnsCheck.sseFail')}</span>
          </div>
        );

      case 'pc-success':
        return (
          <div className="flex items-center gap-2 text-success">
            <CheckCircle2 className="h-5 w-5" />
            <span>{t('dnsCheck.pcSuccess')}</span>
          </div>
        );

      default:
        return null;
    }
  };

  return (
    <>
      <Card
        className={`col-span-2 lg:col-span-1 flex flex-col ${getCardClassName()}`}
      >
        <CardHeader>
          <CardTitle>{t('dnsCheck.title')}</CardTitle>
        </CardHeader>
        <CardContent>
          <div className="flex flex-row space-x-4">
            <div>
              {/* DNS Servers section */}
              {isLoading || !data ? (
                <div className="h-20 bg-muted animate-pulse rounded-lg" />
              ) : (
                data.dns_servers &&
                data.dns_servers.length > 0 && (
                  <div>
                    <h3 className="text-sm font-medium text-muted-foreground mb-2">
                      {t('dashboard.dnsServers')}
                    </h3>
                    <div className="space-y-2">
                      {data.dns_servers.map((server) => (
                        <div
                          key={server}
                          className="flex items-center gap-2 text-sm"
                        >
                          <span className="font-mono text-muted-foreground text-xs">
                            {server}
                          </span>
                        </div>
                      ))}
                    </div>
                  </div>
                )
              )}
            </div>
            <Separator orientation="vertical" className="h-[150px]! self-center" />
            <div className="flex flex-col flex-1 justify-between gap-4 min-h-[200px]">
              {renderDNSCheckContent()}

              <div className="flex flex-col gap-2 mt-auto">
                <Button
                  variant="outline"
                  className="w-full"
                  onClick={handleRetry}
                  disabled={isChecking}
                >
                  {isChecking
                    ? t('dnsCheck.checking')
                    : t('dnsCheck.checkAgain')}
                </Button>
                <Button
                  variant="outline"
                  className="w-full"
                  onClick={() => setShowPCCheckDialog(true)}
                >
                  {t('dnsCheck.checkFromPC')}
                </Button>
              </div>
            </div>
          </div>
        </CardContent>
      </Card>

      <DNSCheckModal
        open={showPCCheckDialog}
        onOpenChange={setShowPCCheckDialog}
        browserStatus={status}
      />
    </>
  );
}
