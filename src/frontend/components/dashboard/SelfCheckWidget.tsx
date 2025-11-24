import { useState, useRef, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from '../ui/card';
import { Button } from '../ui/button';
import { Alert, AlertDescription } from '../ui/alert';
import { CheckCircle2, XCircle, Play, Square, Loader2, Download, ListChecks } from 'lucide-react';
import { toast } from 'sonner';
import { apiClient } from '../../src/api/client';
import { Empty, EmptyHeader, EmptyMedia, EmptyDescription } from '../ui/empty';
import { useDNSCheck } from '../../src/hooks/useDNSCheck';

interface CheckEvent {
  check: string;
  ok: boolean;
  should_exist?: boolean;
  checking?: boolean;
  ipset_name?: string;
  reason?: string;
}

type CheckStatus = 'idle' | 'running' | 'completed' | 'failed' | 'error';

export function SelfCheckWidget() {
  const { t } = useTranslation();
  const [status, setStatus] = useState<CheckStatus>('idle');
  const [results, setResults] = useState<CheckEvent[]>([]);
  const [error, setError] = useState<string | null>(null);
  const eventSourceRef = useRef<EventSource | null>(null);

  // DNS check hook for client-side split-DNS verification
  const {
    status: dnsStatus,
    startCheck: startDnsCheck,
    reset: resetDnsCheck,
  } = useDNSCheck();

  // Update DNS check result based on hook status
  useEffect(() => {
    if (dnsStatus === 'success') {
      setResults((prev) =>
        prev.map((r) =>
          r.check === 'split_dns_client'
            ? {
                ...r,
                ok: true,
                checking: false,
                reason: 'Split-DNS is working correctly from browser',
              }
            : r
        )
      );
    } else if (dnsStatus === 'browser-fail' || dnsStatus === 'sse-fail') {
      setResults((prev) =>
        prev.map((r) =>
          r.check === 'split_dns_client'
            ? {
                ...r,
                ok: false,
                checking: false,
                reason: 'Split-DNS is NOT working from browser (queries not reaching keen-pbr)',
              }
            : r
        )
      );
    }
  }, [dnsStatus]);

  const startSelfCheck = async () => {
    // Reset state
    setResults([]);
    setError(null);
    setStatus('running');
    resetDnsCheck();

    // Step 1: Start client-side DNS check (non-blocking)
    const dnsCheckEvent: CheckEvent = {
      check: 'split_dns_client',
      ok: true, // Set to true to show as in-progress
      should_exist: true,
      checking: true,
      reason: 'Split-DNS must be configured correctly for domain-based routing to work from browser',
    };

    // Add DNS check to results immediately with testing state
    setResults([dnsCheckEvent]);

    // Start DNS check asynchronously (don't block other checks)
    startDnsCheck(true);

    // Step 2: Start server-side checks immediately (don't wait for DNS check)
    const apiBaseUrl = window.location.protocol + '//' + window.location.host;
    const eventSource = new EventSource(`${apiBaseUrl}/api/v1/check/self`);
    eventSourceRef.current = eventSource;

    eventSource.onmessage = (event) => {
      try {
        const data: CheckEvent = JSON.parse(event.data);
        setResults((prev) => [...prev, data]);

        // Check if completed
        if (data.check === 'complete') {
          // Set status based on whether check passed or failed
          setStatus(data.ok ? 'completed' : 'failed');
          eventSource.close();
        }
      } catch (err) {
        console.error('Failed to parse SSE data:', err);
      }
    };

    eventSource.onerror = (err) => {
      console.error('SSE error:', err);
      setError(t('dashboard.selfCheck.failed'));
      setStatus('error');
      eventSource.close();
    };
  };

  const stopSelfCheck = () => {
    resetDnsCheck();
    if (eventSourceRef.current) {
      eventSourceRef.current.close();
      eventSourceRef.current = null;
    }
    setStatus('idle');
  };

  const getCheckTypeLabel = (checkType: string): string => {
    const key = `dashboard.selfCheck.checkTypes.${checkType}`;
    const translated = t(key);
    // If translation key doesn't exist, return the check type as-is
    return translated === key ? checkType : translated;
  };

  const getExpectedStatus = (result: CheckEvent): string => {
    if (result.should_exist === false) {
      return t('dashboard.selfCheck.status.absent', { defaultValue: 'absent' });
    }
    return t('dashboard.selfCheck.status.present', { defaultValue: 'present' });
  };

  const getActualStatus = (result: CheckEvent): { text: string; icon: 'check' | 'cross' | 'spinner' } => {
    // Special case: testing state
    if (result.checking) {
      return { text: t('common.loading', { defaultValue: 'testing...' }), icon: 'spinner' };
    }

    if (result.ok) {
      return { text: t('dashboard.selfCheck.status.ok', { defaultValue: 'OK' }), icon: 'check' };
    } else {
      return { text: t('dashboard.selfCheck.status.error', { defaultValue: 'error' }), icon: 'cross' };
    }
  };

  const downloadResults = async () => {
    try {
      // Fetch status info including DNS servers
      const statusInfo = await apiClient.getStatus();

      // Format self-check results
      const formattedResults = results
        .filter((result) => result.check !== 'complete')
        .map((result) => ({
          rule: result.ipset_name || 'global',
          check: result.check,
          ok: result.ok,
          reason: result.reason,
        }));

      // Create export object
      const exportData = {
        timestamp: new Date().toISOString(),
        selfCheck: {
          status: status === 'completed' ? 'passed' : status === 'failed' ? 'failed' : status,
          results: formattedResults,
        },
        system: statusInfo,
      };

      // Create blob and download
      const blob = new Blob([JSON.stringify(exportData, null, 2)], {
        type: 'application/json',
      });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `keen-pbr-self-check-${new Date().toISOString().split('T')[0]}.json`;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      URL.revokeObjectURL(url);

      toast.success(t('dashboard.selfCheck.downloaded', { defaultValue: 'Results downloaded' }));
    } catch (error) {
      console.error('Failed to download results:', error);
      toast.error(t('dashboard.selfCheck.downloadFailed', { defaultValue: 'Failed to download results' }));
    }
  };

  return (
    <Card>
      <CardHeader>
        <CardTitle>{t('dashboard.selfCheck.title')}</CardTitle>
        <CardDescription>{t('dashboard.selfCheck.description')}</CardDescription>
      </CardHeader>
      <CardContent>
        <div className="space-y-4">
          {/* Control buttons */}
          <div className="flex flex-wrap gap-2">
            <Button
              onClick={startSelfCheck}
              disabled={status === 'running'}
              size="default"
            >
              {status === 'running' ? (
                <>
                  <Loader2 className="mr-2 h-4 w-4 animate-spin" />
                  {t('dashboard.selfCheck.checking')}
                </>
              ) : (
                <>
                  <Play className="mr-2 h-4 w-4" />
                  {t('dashboard.selfCheck.runCheck')}
                </>
              )}
            </Button>
            {status === 'running' && (
              <Button onClick={stopSelfCheck} variant="outline" size="default">
                <Square className="mr-2 h-4 w-4" />
                {t('dashboard.selfCheck.stopCheck')}
              </Button>
            )}
            {results.length > 0 && (
              <Button onClick={downloadResults} variant="outline" size="default">
                <Download className="mr-2 h-4 w-4" />
                {t('dashboard.selfCheck.download', { defaultValue: 'Download' })}
              </Button>
            )}
          </div>

          {/* Error alert */}
          {error && (
            <Alert variant="destructive">
              <AlertDescription>{error}</AlertDescription>
            </Alert>
          )}

          {/* Results Table */}
          {results.length > 0 ? (
            <div className="w-full rounded-md border overflow-x-auto">
              <table className="w-full min-w-[640px] text-sm">
                <thead className="bg-muted/50 border-b">
                  <tr>
                    <th className="text-left py-3 px-4 font-medium">{t('dashboard.selfCheck.table.check', { defaultValue: 'Check' })}</th>
                    <th className="text-left py-3 px-4 font-medium">{t('dashboard.selfCheck.table.expected', { defaultValue: 'Expected' })}</th>
                    <th className="text-left py-3 px-4 font-medium">{t('dashboard.selfCheck.table.actual', { defaultValue: 'Actual' })}</th>
                  </tr>
                </thead>
                <tbody>
                  {(() => {
                    const filteredResults = results.filter((result) => result.check !== 'complete');
                    let lastRule: string | null = null;

                    return filteredResults.map((result, index) => {
                      const rule = result.ipset_name || 'global';
                      const actualStatus = getActualStatus(result);
                      const isNewGroup = rule !== lastRule;
                      lastRule = rule;

                      return (
                        <>
                          {isNewGroup && (
                            <tr key={`${index}-header`} className="bg-muted/30">
                              <td colSpan={3} className="py-2 px-4 font-medium border-t">
                                {rule}
                              </td>
                            </tr>
                          )}
                          <tr
                            key={`${index}-main`}
                            className={`border-b-0 ${
                              result.reason === 'testing'
                                ? 'bg-muted/10'
                                : result.ok
                                ? 'bg-green-500/5 dark:bg-green-500/10'
                                : 'bg-red-500/5 dark:bg-red-500/10'
                            }`}
                          >
                            <td className="py-3 px-4 pl-8">
                              <div>{getCheckTypeLabel(result.check)}</div>
                              {result.reason && result.reason !== 'testing' && (
                                <div className="text-xs text-muted-foreground mt-1">{result.reason}</div>
                              )}
                            </td>
                            <td className="py-3 px-4 text-muted-foreground">
                              {getExpectedStatus(result)}
                            </td>
                            <td className="py-3 px-4">
                              <div className="flex items-center gap-2">
                                <span>{actualStatus.text}</span>
                                {actualStatus.icon === 'check' ? (
                                  <CheckCircle2 className="h-4 w-4 text-green-600" />
                                ) : actualStatus.icon === 'spinner' ? (
                                  <Loader2 className="h-4 w-4 animate-spin text-blue-600" />
                                ) : (
                                  <XCircle className="h-4 w-4 text-red-600" />
                                )}
                              </div>
                            </td>
                          </tr>
                        </>
                      );
                    });
                  })()}
                </tbody>
              </table>
            </div>
          ) : status === 'idle' ? (
            <Empty className='p-4 md:p-4'>
              <EmptyHeader>
                <EmptyMedia variant="icon">
                  <ListChecks />
                </EmptyMedia>
                <EmptyDescription>
                  {t('dashboard.selfCheck.noResults')}
                </EmptyDescription>
              </EmptyHeader>
            </Empty>
          ) : null}

          {/* Status message */}
          {status === 'completed' && (
            <Alert>
              <CheckCircle2 className="h-4 w-4" />
              <AlertDescription>{t('dashboard.selfCheck.completed')}</AlertDescription>
            </Alert>
          )}
          {status === 'failed' && (
            <Alert variant="destructive">
              <XCircle className="h-4 w-4" />
              <AlertDescription>{t('dashboard.selfCheck.failed')}</AlertDescription>
            </Alert>
          )}
        </div>
      </CardContent>
    </Card>
  );
}
