import { useState, useRef, Fragment } from 'react';
import { useTranslation } from 'react-i18next';
import { Card, CardContent, CardHeader, CardTitle, CardDescription } from '../ui/card';
import { Button } from '../ui/button';
import { Alert, AlertDescription } from '../ui/alert';
import { CheckCircle2, XCircle, Play, Square, Loader2, Copy, Check } from 'lucide-react';
import { toast } from 'sonner';

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
      return 'running';
    }
    return 'present';
  };

  const getActualStatus = (result: CheckEvent): { text: string; icon: 'check' | 'cross' } => {
    if (result.ok) {
      // Success cases
      if (result.check === 'dnsmasq' || result.check === 'service') {
        return { text: 'running', icon: 'check' };
      }
      if (result.log.includes('not present') || result.log.includes('disabled')) {
        return { text: 'absent', icon: 'check' };
      }
      return { text: 'present', icon: 'check' };
    } else {
      // Failure cases
      if (result.check === 'dnsmasq' || result.check === 'service') {
        return { text: 'dead', icon: 'cross' };
      }
      if (result.log.includes('missing') || result.log.includes('does NOT exist')) {
        return { text: 'missing', icon: 'cross' };
      }
      if (result.log.includes('stale') || result.log.includes('unexpected')) {
        return { text: 'stale', icon: 'cross' };
      }
      return { text: 'error', icon: 'cross' };
    }
  };

  const copyAllChecks = () => {
    const text = results
      .map((result) => {
        const rule = result.ipset_name || 'global';
        const check = getCheckTypeLabel(result.check);
        const expected = getExpectedStatus(result.check);
        const actual = getActualStatus(result);
        return `${rule} | ${check} | ${expected} | ${actual.text} ${actual.icon === 'check' ? '✓' : '✗'}`;
      })
      .join('\n');

    navigator.clipboard.writeText(text).then(
      () => {
        toast.success(t('dashboard.selfCheck.copied'));
      },
      () => {
        toast.error(t('dashboard.selfCheck.copyFailed'));
      }
    );
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
          <div className="flex gap-2">
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
              <Button onClick={copyAllChecks} variant="outline" size="default">
                <Copy className="mr-2 h-4 w-4" />
                {t('dashboard.selfCheck.copyAll')}
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
            <div className="w-full rounded-md border overflow-hidden">
              <table className="w-full text-sm">
                <thead className="bg-muted/50 border-b">
                  <tr>
                    <th className="text-left py-3 px-4 font-medium">Rule</th>
                    <th className="text-left py-3 px-4 font-medium">Check</th>
                    <th className="text-left py-3 px-4 font-medium">Expected</th>
                    <th className="text-left py-3 px-4 font-medium">Actual</th>
                  </tr>
                </thead>
                <tbody>
                  {results
                    .filter((result) => result.check !== 'complete')
                    .map((result, index) => {
                      const actualStatus = getActualStatus(result);
                      return (
                        <>
                          <tr
                            key={`${index}-main`}
                            className={`border-b-0 hover:bg-muted/30 ${
                              !result.ok ? 'bg-destructive/5' : ''
                            }`}
                          >
                            <td className="py-3 px-4">
                              <span className="font-mono text-xs px-2 py-1 rounded bg-muted">
                                {result.ipset_name || 'global'}
                              </span>
                            </td>
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
                              <td colSpan={4} className="py-2 px-4 text-xs">
                                <code className="text-muted-foreground font-mono bg-muted/50 px-2 py-1 rounded">
                                  {result.command}
                                </code>
                              </td>
                            </tr>
                          )}
                        </>
                      );
                    })}
                </tbody>
              </table>
            </div>
          ) : status === 'idle' ? (
            <div className="flex items-center justify-center h-32 text-muted-foreground text-sm border rounded-md">
              {t('dashboard.selfCheck.noResults')}
            </div>
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
