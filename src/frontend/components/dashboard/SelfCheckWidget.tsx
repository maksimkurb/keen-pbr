import { useState, useRef } from 'react';
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

  const copyAllChecks = () => {
    const text = results
      .map((result) => {
        let line = `[${result.ok ? '✓' : '✗'}] ${getCheckTypeLabel(result.check)}`;
        if (result.ipset_name) {
          line += ` (${result.ipset_name})`;
        }
        line += `\n  ${result.log}`;
        if (result.reason) {
          line += `\n  Reason: ${result.reason}`;
        }
        if (result.command) {
          line += `\n  Command: ${result.command}`;
        }
        return line;
      })
      .join('\n\n');

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

          {/* Results */}
          {results.length > 0 ? (
            <div className="w-full rounded-md border">
              <div className="p-4 space-y-3">
                {results.map((result, index) => (
                  <div
                    key={index}
                    className="flex items-start gap-3 py-3 px-3 rounded-md border bg-card hover:bg-muted/50"
                  >
                    <div className="flex-shrink-0 mt-0.5">
                      {result.ok ? (
                        <CheckCircle2 className="h-5 w-5 text-green-600" />
                      ) : (
                        <XCircle className="h-5 w-5 text-red-600" />
                      )}
                    </div>
                    <div className="flex-1 min-w-0 space-y-1">
                      <div className="flex items-center gap-2">
                        <p className="text-sm font-medium text-foreground">
                          {getCheckTypeLabel(result.check)}
                        </p>
                        {result.ipset_name && (
                          <span className="text-xs px-2 py-0.5 rounded-md bg-muted font-mono">
                            {result.ipset_name}
                          </span>
                        )}
                      </div>
                      <p className="text-sm text-muted-foreground break-words">
                        {result.log}
                      </p>
                      {result.reason && (
                        <p className="text-xs text-muted-foreground italic">
                          {result.reason}
                        </p>
                      )}
                      {result.command && (
                        <div className="mt-2">
                          <code className="text-xs bg-muted px-2 py-1 rounded block font-mono break-all">
                            {result.command}
                          </code>
                        </div>
                      )}
                    </div>
                  </div>
                ))}
              </div>
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
