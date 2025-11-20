import { useState, useRef, Fragment } from 'react';
import { useTranslation } from 'react-i18next';
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from '../ui/card';
import { Button } from '../ui/button';
import { Alert, AlertDescription } from '../ui/alert';
import { CheckCircle2, XCircle, Play, Square, Loader2, Download, ListChecks } from 'lucide-react';
import { toast } from 'sonner';
import { apiClient } from '../../src/api/client';
import { Empty, EmptyHeader, EmptyMedia, EmptyDescription } from '../ui/empty';

interface CheckEvent {
  check: string;
  ok: boolean;
  log: string;
  ipset_name?: string;
  reason?: string;
  command?: string;
}

type CheckStatus = 'idle' | 'running' | 'completed' | 'failed' | 'error';

export function SelfCheckWidget() {
  const { t } = useTranslation();
  const [status, setStatus] = useState<CheckStatus>('idle');
  const [results, setResults] = useState<CheckEvent[]>([]);
  const [error, setError] = useState<string | null>(null);
  const eventSourceRef = useRef<EventSource | null>(null);

  const startSelfCheck = () => {
    // Reset state
    setResults([]);
    setError(null);
    setStatus('running');

    // Create EventSource to connect to SSE endpoint
    const apiBaseUrl = window.location.protocol + '//' + window.location.host;
    const eventSource = new EventSource(`${apiBaseUrl}/api/v1/check/self?sse=true`);
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

  const getExpectedStatus = (checkType: string): string => {
    // Most checks expect things to be "present" or "running"
    if (checkType === 'dnsmasq' || checkType === 'service') {
      return t('dashboard.selfCheck.status.running', { defaultValue: 'running' });
    }
    return t('dashboard.selfCheck.status.present', { defaultValue: 'present' });
  };

  const getActualStatus = (result: CheckEvent): { text: string; icon: 'check' | 'cross' } => {
    if (result.ok) {
      // Success cases
      if (result.check === 'dnsmasq' || result.check === 'service') {
        return { text: t('dashboard.selfCheck.status.running', { defaultValue: 'running' }), icon: 'check' };
      }
      if (result.log.includes('not present') || result.log.includes('disabled')) {
        return { text: t('dashboard.selfCheck.status.absent', { defaultValue: 'absent' }), icon: 'check' };
      }
      return { text: t('dashboard.selfCheck.status.present', { defaultValue: 'present' }), icon: 'check' };
    } else {
      // Failure cases
      if (result.check === 'dnsmasq' || result.check === 'service') {
        return { text: t('dashboard.selfCheck.status.dead', { defaultValue: 'dead' }), icon: 'cross' };
      }
      if (result.log.includes('missing') || result.log.includes('does NOT exist')) {
        return { text: t('dashboard.selfCheck.status.missing', { defaultValue: 'missing' }), icon: 'cross' };
      }
      if (result.log.includes('stale') || result.log.includes('unexpected')) {
        return { text: t('dashboard.selfCheck.status.stale', { defaultValue: 'stale' }), icon: 'cross' };
      }
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
          log: result.log,
          reason: result.reason,
          command: result.command,
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
                    <th className="text-left py-3 px-4 font-medium">{t('dashboard.selfCheck.table.rule', { defaultValue: 'Rule' })}</th>
                    <th className="text-left py-3 px-4 font-medium">{t('dashboard.selfCheck.table.check', { defaultValue: 'Check' })}</th>
                    <th className="text-left py-3 px-4 font-medium">{t('dashboard.selfCheck.table.expected', { defaultValue: 'Expected' })}</th>
                    <th className="text-left py-3 px-4 font-medium">{t('dashboard.selfCheck.table.actual', { defaultValue: 'Actual' })}</th>
                  </tr>
                </thead>
                <tbody>
                  {(() => {
                    const filteredResults = results.filter((result) => result.check !== 'complete');

                    // Calculate rowspan for each rule
                    const ruleRowSpans = new Map<string, number>();
                    filteredResults.forEach((result) => {
                      const rule = result.ipset_name || 'global';
                      // Each check takes 1 row, plus 1 for command if present
                      const rows = result.command ? 2 : 1;
                      ruleRowSpans.set(rule, (ruleRowSpans.get(rule) || 0) + rows);
                    });

                    // Track which rule was last rendered
                    let lastRule: string | null = null;

                    return filteredResults.map((result, index) => {
                      const rule = result.ipset_name || 'global';
                      const actualStatus = getActualStatus(result);
                      const isFirstInGroup = rule !== lastRule;
                      const rowSpan = isFirstInGroup ? ruleRowSpans.get(rule) || 1 : 0;

                      lastRule = rule;

                      return (
                        <>
                          <tr
                            key={`${index}-main`}
                            className={`border-b-0 hover:bg-muted/30 ${
                              !result.ok ? 'bg-destructive/5' : ''
                            }`}
                          >
                            {isFirstInGroup && (
                              <td rowSpan={rowSpan} className="py-3 px-4 align-top">
                                <span className="font-mono text-xs px-2 py-1 rounded bg-muted">
                                  {rule}
                                </span>
                              </td>
                            )}
                            <td className="py-3 px-4">{getCheckTypeLabel(result.check)}</td>
                            <td className="py-3 px-4 text-muted-foreground">
                              {getExpectedStatus(result.check)}
                            </td>
                            <td className="py-3 px-4">
                              <div className="flex items-center gap-2">
                                <span>{actualStatus.text}</span>
                                {actualStatus.icon === 'check' ? (
                                  <CheckCircle2 className="h-4 w-4 text-green-600" />
                                ) : (
                                  <XCircle className="h-4 w-4 text-red-600" />
                                )}
                              </div>
                            </td>
                          </tr>
                          {result.command && (
                            <tr
                              key={`${index}-cmd`}
                              className={`border-b last:border-b-0 ${
                                !result.ok ? 'bg-destructive/5' : ''
                              }`}
                            >
                              <td colSpan={3} className="py-2 px-4 text-xs">
                                <code className="text-muted-foreground font-mono bg-muted/50 px-2 py-1 rounded">
                                  {result.command}
                                </code>
                              </td>
                            </tr>
                          )}
                        </>
                      );
                    });
                  })()}
                </tbody>
              </table>
            </div>
          ) : status === 'idle' ? (
            <Empty>
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
