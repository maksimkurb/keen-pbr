import {
  AlertCircle,
  CheckCircle2,
  Copy,
  Loader2,
  Terminal,
} from 'lucide-react';
import { useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { toast } from 'sonner';
import { type CheckStatus, useDNSCheck } from '../../src/hooks/useDNSCheck';
import { Alert, AlertDescription } from '../ui/alert';
import { Button } from '../ui/button';
import { Dialog, DialogContent, DialogDescription, DialogHeader, DialogTitle } from '../ui/dialog';

import {
  InputGroup,
  InputGroupAddon,
  InputGroupButton,
  InputGroupInput,
} from '../ui/input-group';
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from '../ui/tooltip';

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
      <DialogContent aria-describedby={undefined}>
        <DialogHeader>
          <DialogTitle>{t('dnsCheck.pcCheckTitle')}</DialogTitle>
        </DialogHeader>

        <div className="mt-4 space-y-4">
          {/* Status summary */}
          <div className="flex flex-col gap-2 text-sm">
            <div className="flex items-center gap-2">
              {isBrowserSuccess ? (
                <CheckCircle2 className="h-4 w-4 text-success" />
              ) : browserStatus === 'checking' ? (
                <Loader2 className="h-4 w-4 animate-spin" />
              ) : (
                <AlertCircle className="h-4 w-4 text-destructive" />
              )}
              <span>{getBrowserStatusText()}</span>
            </div>
            <div className="flex items-center gap-2">
              {isPcSuccess ? (
                <CheckCircle2 className="h-4 w-4 text-success" />
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
              <InputGroup>
                <InputGroupAddon className="pointer-events-none">
                  <Terminal className="h-4 w-4" />
                </InputGroupAddon>
                <InputGroupInput
                  readOnly
                  value={`nslookup ${pcCheckState.randomString}.dns-check.keen-pbr.internal`}
                  onClick={(e) => {
                    e.currentTarget.select();
                    navigator.clipboard.writeText(e.currentTarget.value);
                    toast.success(t('dnsCheck.commandCopied'));
                  }}
                  className="font-mono text-sm cursor-pointer"
                />
                <InputGroupAddon align="inline-end">
                  <Tooltip>
                    <TooltipTrigger asChild>
                      <InputGroupButton
                        onClick={() => {
                          const text = `nslookup ${pcCheckState.randomString}.dns-check.keen-pbr.internal`;
                          navigator.clipboard.writeText(text);
                          toast.success(t('dnsCheck.commandCopied'));
                        }}
                        size="icon-xs"
                      >
                        <Copy />
                      </InputGroupButton>
                    </TooltipTrigger>
                    <TooltipContent>{t('common.copy')}</TooltipContent>
                  </Tooltip>
                </InputGroupAddon>
              </InputGroup>
            </div>
          )}

          {/* Warning message */}
          {pcCheckState.showWarning && (
            <Alert variant="warning">
              <AlertCircle />
              <AlertDescription>
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
