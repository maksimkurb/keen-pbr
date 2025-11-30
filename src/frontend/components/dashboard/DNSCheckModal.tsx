import { AlertCircle, CheckCircle2, Loader2, Terminal } from 'lucide-react';
import { useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { type CheckStatus, useDNSCheck } from '../../src/hooks/useDNSCheck';
import { Alert, AlertDescription } from '../ui/alert';
import { Button } from '../ui/button';
import { Dialog, DialogContent, DialogHeader, DialogTitle } from '../ui/dialog';

interface DNSCheckModalProps {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  browserStatus: CheckStatus;
}

export function DNSCheckModal({
  open,
  onOpenChange,
  browserStatus,
}: DNSCheckModalProps) {
  const { t } = useTranslation();

  const {
    status: pcStatus,
    checkState: pcCheckState,
    startCheck: pcStartCheck,
    reset: pcReset,
  } = useDNSCheck();

  // Start PC check when modal opens
  useEffect(() => {
    if (open) {
      pcStartCheck(false);
    }
  }, [open, pcStartCheck]);

  const handleDialogClose = (isOpen: boolean) => {
    onOpenChange(isOpen);
    if (!isOpen) {
      pcReset();
    }
  };

  const getBrowserStatusText = () => {
    switch (browserStatus) {
      case 'success':
        return t('dnsCheck.browserWorking');
      case 'browser-fail':
      case 'sse-fail':
        return t('dnsCheck.browserNotWorking');
      case 'checking':
        return t('dnsCheck.browserChecking');
      default:
        return t('dnsCheck.browserUnknown');
    }
  };

  const getPcStatusText = () => {
    if (isPcSuccess) {
      return t('dnsCheck.pcWorking');
    }
    if (pcCheckState.waiting) {
      return t('dnsCheck.pcWaiting');
    }
    return t('dnsCheck.pcUnknown');
  };

  const isBrowserSuccess = browserStatus === 'success';
  const isPcSuccess = pcStatus === 'pc-success';

  return (
    <Dialog open={open} onOpenChange={handleDialogClose}>
      <DialogContent>
        <DialogHeader>
          <DialogTitle>{t('dnsCheck.pcCheckTitle')}</DialogTitle>
        </DialogHeader>

        <div className="mt-4 space-y-4">
          {/* Status summary */}
          <div className="flex flex-col gap-2 text-sm">
            <div className="flex items-center gap-2">
              {isBrowserSuccess ? (
                <CheckCircle2 className="h-4 w-4 text-chart-2" />
              ) : browserStatus === 'checking' ? (
                <Loader2 className="h-4 w-4 animate-spin" />
              ) : (
                <AlertCircle className="h-4 w-4 text-destructive" />
              )}
              <span>{getBrowserStatusText()}</span>
            </div>
            <div className="flex items-center gap-2">
              {isPcSuccess ? (
                <CheckCircle2 className="h-4 w-4 text-chart-2" />
              ) : pcCheckState.waiting ? (
                <Loader2 className="h-4 w-4 animate-spin" />
              ) : (
                <AlertCircle className="h-4 w-4 text-muted-foreground" />
              )}
              <span>{getPcStatusText()}</span>
            </div>
          </div>

          {/* Command for PC check */}
          {pcCheckState.waiting && (
            <div>
              <p className="text-sm text-muted-foreground mb-3">
                {t('dnsCheck.pcCheckDescription')}
              </p>
              <code className="block bg-muted p-3 rounded text-sm break-all">
                <Terminal className="inline h-4 w-4 mr-2" />
                nslookup {pcCheckState.randomString}.dns-check.keen-pbr.internal
              </code>
            </div>
          )}

          {/* Warning message */}
          {pcCheckState.showWarning && (
            <Alert className="border-chart-4 bg-chart-4/10">
              <AlertCircle className="h-4 w-4 text-chart-4" />
              <AlertDescription className="text-chart-4">
                {t('dnsCheck.pcCheckWarning')}
              </AlertDescription>
            </Alert>
          )}

          {/* Close button when PC check succeeds */}
          {isPcSuccess && (
            <Button
              variant="outline"
              className="w-full"
              onClick={() => handleDialogClose(false)}
            >
              {t('common.close')}
            </Button>
          )}
        </div>
      </DialogContent>
    </Dialog>
  );
}
