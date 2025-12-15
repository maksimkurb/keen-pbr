import {
  AlertCircle,
  CircleCheckBig,
  CircleOff,
  Loader2,
  Route,
  RouteOff,
  Search,
  X,
} from 'lucide-react';
import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { RoutingCheckResponse } from '../../src/api/client';
import { apiClient } from '../../src/api/client';
import { Alert, AlertDescription } from '../ui/alert';
import { Button } from '../ui/button';
import { Card, CardContent, CardHeader, CardTitle } from '../ui/card';
import { Input } from '../ui/input';

type CheckType = 'routing' | 'ping' | 'traceroute';

interface CheckState {
  type: CheckType | null;
  loading: boolean;
  error: string | null;
  routingResult: RoutingCheckResponse | null;
  consoleOutput: string[];
}

export function DomainCheckerWidget() {
  const { t } = useTranslation();
  const [host, setHost] = useState('');
  const [state, setState] = useState<CheckState>({
    type: null,
    loading: false,
    error: null,
    routingResult: null,
    consoleOutput: [],
  });
  const eventSourceRef = useRef<EventSource | null>(null);
  const abortControllerRef = useRef<AbortController | null>(null);

  // Cleanup EventSource on unmount
  useEffect(() => {
    return () => {
      if (eventSourceRef.current) {
        eventSourceRef.current.close();
      }
      if (abortControllerRef.current) {
        abortControllerRef.current.abort();
      }
    };
  }, []);

  const sanitizeHostInput = (input: string): string | null => {
    const trimmed = input.trim();

    // Try to parse as URL
    try {
      const url = new URL(trimmed);
      return url.hostname;
    } catch {
      // Not a valid URL, check if it's a valid domain/IPv4/IPv6
      if (
        /^([a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?\.)*[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?$/.test(
          trimmed,
        )
      ) {
        return trimmed; // Valid domain
      }
      if (/^(\d{1,3}\.){3}\d{1,3}$/.test(trimmed)) {
        return trimmed; // Valid IPv4
      }
      if (
        /^(([0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}|::1|::)$/.test(trimmed) ||
        /^[0-9a-fA-F:]+$/.test(trimmed)
      ) {
        return trimmed; // Valid IPv6
      }
      return null; // Invalid input
    }
  };

  const handleCancel = () => {
    if (eventSourceRef.current) {
      eventSourceRef.current.close();
      eventSourceRef.current = null;
    }
    if (abortControllerRef.current) {
      abortControllerRef.current.abort();
      abortControllerRef.current = null;
    }
    setState((prev) => ({ ...prev, loading: false }));
  };

  const handleCheckRouting = async () => {
    const sanitized = sanitizeHostInput(host);

    if (!sanitized) {
      setState({
        type: 'routing',
        loading: false,
        error: t('dashboard.domainChecker.invalidHostInput'),
        routingResult: null,
        consoleOutput: [],
      });
      return;
    }

    // Update the input field with sanitized value
    if (sanitized !== host) {
      setHost(sanitized);
    }

    setState({
      type: 'routing',
      loading: true,
      error: null,
      routingResult: null,
      consoleOutput: [],
    });

    try {
      const result = await apiClient.checkRouting(sanitized);
      setState((prev) => ({
        ...prev,
        loading: false,
        routingResult: result,
      }));
    } catch (err) {
      setState((prev) => ({
        ...prev,
        loading: false,
        error:
          err instanceof Error
            ? err.message
            : t('dashboard.domainChecker.checkError'),
      }));
    }
  };

  const handlePing = () => {
    // Close any existing EventSource and abort controller
    if (eventSourceRef.current) {
      eventSourceRef.current.close();
    }
    if (abortControllerRef.current) {
      abortControllerRef.current.abort();
    }

    const sanitized = sanitizeHostInput(host);

    if (!sanitized) {
      setState({
        type: 'routing',
        loading: false,
        error: t('dashboard.domainChecker.invalidHostInput'),
        routingResult: null,
        consoleOutput: [],
      });
      return;
    }

    // Update the input field with sanitized value
    if (sanitized !== host) {
      setHost(sanitized);
    }

    setState({
      type: 'ping',
      loading: true,
      error: null,
      routingResult: null,
      consoleOutput: [],
    });

    const abortController = new AbortController();
    abortControllerRef.current = abortController;

    const url = apiClient.getPingSSEUrl(sanitized);
    const eventSource = new EventSource(url);
    eventSourceRef.current = eventSource;
    let completed = false;

    eventSource.onmessage = (event) => {
      setState((prev) => ({
        ...prev,
        consoleOutput: [...prev.consoleOutput, event.data],
      }));

      // Detect completion
      if (
        event.data.includes('[Process completed') ||
        event.data.includes('[Process exited')
      ) {
        completed = true;
        setTimeout(() => {
          eventSource.close();
          setState((prev) => ({ ...prev, loading: false }));
        }, 100);
      }
    };

    eventSource.onerror = (error) => {
      eventSource.close();
      // Only show error if process didn't complete successfully
      if (!completed && !abortController.signal.aborted) {
        console.error('SSE Error:', error);
        setState((prev) => ({
          ...prev,
          loading: false,
          error: t('dashboard.domainChecker.pingError'),
        }));
      }
    };
  };

  const handleTraceroute = () => {
    // Close any existing EventSource and abort controller
    if (eventSourceRef.current) {
      eventSourceRef.current.close();
    }
    if (abortControllerRef.current) {
      abortControllerRef.current.abort();
    }

    const sanitized = sanitizeHostInput(host);

    if (!sanitized) {
      setState({
        type: 'routing',
        loading: false,
        error: t('dashboard.domainChecker.invalidHostInput'),
        routingResult: null,
        consoleOutput: [],
      });
      return;
    }

    // Update the input field with sanitized value
    if (sanitized !== host) {
      setHost(sanitized);
    }

    setState({
      type: 'traceroute',
      loading: true,
      error: null,
      routingResult: null,
      consoleOutput: [],
    });

    const abortController = new AbortController();
    abortControllerRef.current = abortController;

    const url = apiClient.getTracerouteSSEUrl(sanitized);
    const eventSource = new EventSource(url);
    eventSourceRef.current = eventSource;
    let completed = false;

    eventSource.onmessage = (event) => {
      setState((prev) => ({
        ...prev,
        consoleOutput: [...prev.consoleOutput, event.data],
      }));

      // Detect completion
      if (
        event.data.includes('[Process completed') ||
        event.data.includes('[Process exited')
      ) {
        completed = true;
        setTimeout(() => {
          eventSource.close();
          setState((prev) => ({ ...prev, loading: false }));
        }, 100);
      }
    };

    eventSource.onerror = (error) => {
      eventSource.close();
      // Only show error if process didn't complete successfully
      if (!completed && !abortController.signal.aborted) {
        console.error('SSE Error:', error);
        setState((prev) => ({
          ...prev,
          loading: false,
          error: t('dashboard.domainChecker.tracerouteError'),
        }));
      }
    };
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
              value={host}
              onChange={(e) => setHost(e.target.value)}
              onKeyDown={(e) => {
                if (e.key === 'Enter' && host) {
                  handleCheckRouting();
                }
              }}
              className="pl-9"
              disabled={state.loading}
            />
          </div>
        </div>

        <div className="flex gap-2 flex-wrap">
          <Button
            onClick={handleCheckRouting}
            disabled={!host || state.loading}
            variant="default"
          >
            {state.loading && state.type === 'routing' && (
              <Loader2 className="mr-2 h-4 w-4 animate-spin" />
            )}
            {t('dashboard.domainChecker.checkRouting')}
          </Button>
          <Button
            onClick={handlePing}
            disabled={!host || state.loading}
            variant="outline"
          >
            {state.loading && state.type === 'ping' && (
              <Loader2 className="mr-2 h-4 w-4 animate-spin" />
            )}
            {t('dashboard.domainChecker.ping')}
          </Button>
          <Button
            onClick={handleTraceroute}
            disabled={!host || state.loading}
            variant="outline"
          >
            {state.loading && state.type === 'traceroute' && (
              <Loader2 className="mr-2 h-4 w-4 animate-spin" />
            )}
            {t('dashboard.domainChecker.traceroute')}
          </Button>
          {state.loading && (state.type === 'ping' || state.type === 'traceroute') && (
            <Button
              onClick={handleCancel}
              variant="destructive"
              size="sm"
            >
              <X className="mr-2 h-4 w-4" />
              {t("common.cancel")}
            </Button>
          )}
        </div>

        {state.error && (
          <Alert variant="destructive">
            <AlertCircle className="h-4 w-4" />
            <AlertDescription>{state.error}</AlertDescription>
          </Alert>
        )}

        {/* Routing Check Results */}
        {state.routingResult && (
          <div className="space-y-4 rounded-lg border p-4">
            {/* Unified Results Table */}
            <div className="overflow-x-auto">
              {state.routingResult &&
                (() => {
                  // Get all rule names from the rules field
                  const ruleNames = (state.routingResult.rules || []).map(
                    (rule) => rule.rule_name,
                  );

                  return (
                    <table className="w-full text-sm border-collapse">
                      <thead>
                        <tr className="border-b bg-accent">
                          <th className="text-left p-2 font-medium">
                            {t('dashboard.domainChecker.host', {
                              host: state.routingResult.host,
                            })}
                          </th>
                          {ruleNames.map((ruleName) => (
                            <th
                              key={ruleName}
                              className="text-center p-2 font-medium text-xs"
                            >
                              {ruleName}
                            </th>
                          ))}
                        </tr>
                      </thead>
                      <tbody>
                        {/* Host row - domain lists */}
                        {state.routingResult && (
                          <tr className="border-b bg-secondary">
                            <td className="p-2">
                              {t('dashboard.domainChecker.hostPresentInRules')}
                            </td>
                            {ruleNames.map((ruleName) => {
                              const rule = state.routingResult?.rules?.find(
                                (r) => r.rule_name === ruleName,
                              );
                              const host = state.routingResult?.host || '';
                              return (
                                <td
                                  key={`${host}-${ruleName}`}
                                  className="text-center p-2"
                                >
                                  <div className="flex items-center justify-center">
                                    {rule?.matched_by_hostname ? (
                                      <CircleCheckBig className="h-5 w-5 text-green-600" />
                                    ) : (
                                      <CircleOff className="h-5 w-5 text-gray-400" />
                                    )}
                                  </div>
                                </td>
                              );
                            })}
                          </tr>
                        )}

                        {/* IP rows - IPSets */}
                        {state.routingResult?.ipset_checks?.map((ipCheck) => (
                          <tr key={ipCheck.ip} className="border-b">
                            <td className="p-2 font-mono">{ipCheck.ip}</td>
                            {ruleNames.map((ruleName) => {
                              const ruleResult = ipCheck.rule_results.find(
                                (r) => r.rule_name === ruleName,
                              );
                              if (!ruleResult) return null;

                              const isInIPSet = ruleResult.present_in_ipset;
                              const shouldBeInIPSet =
                                ruleResult.should_be_present;

                              const isProblematic =
                                (isInIPSet && !shouldBeInIPSet) ||
                                (!isInIPSet && shouldBeInIPSet);

                              return (
                                <td
                                  key={`${ipCheck.ip}-${ruleName}`}
                                  className={`text-center p-2 ${isProblematic ? 'bg-destructive/10' : ''}`}
                                >
                                  <div className="flex items-center justify-center">
                                    {isInIPSet && shouldBeInIPSet ? (
                                      <Route className="h-5 w-5 text-green-600" />
                                    ) : isInIPSet && !shouldBeInIPSet ? (
                                      <Route className="h-5 w-5 text-yellow-500" />
                                    ) : !isInIPSet && shouldBeInIPSet ? (
                                      <RouteOff className="h-5 w-5 text-red-600" />
                                    ) : (
                                      <RouteOff className="h-5 w-5 text-gray-400" />
                                    )}
                                  </div>
                                </td>
                              );
                            })}
                          </tr>
                        ))}
                      </tbody>
                    </table>
                  );
                })()}
            </div>

            {/* Legend */}
            <div className="mt-4 space-y-2 text-sm">
              <div className="font-medium text-muted-foreground">
                {t('dashboard.domainChecker.legend')}
              </div>
              <div className="space-y-1">
                <div className="flex items-center gap-2">
                  <CircleCheckBig className="h-4 w-4 text-green-600" />
                  <span>{t('dashboard.domainChecker.inLists')}</span>
                </div>
                <div className="flex items-center gap-2">
                  <CircleOff className="h-4 w-4 text-gray-400" />
                  <span>{t('dashboard.domainChecker.notInLists')}</span>
                </div>
                <div className="flex items-center gap-2">
                  <Route className="h-4 w-4 text-green-600" />
                  <span>{t('dashboard.domainChecker.inIPSetCorrect')}</span>
                </div>
                <div className="flex items-center gap-2">
                  <RouteOff className="h-4 w-4 text-gray-400" />
                  <span>{t('dashboard.domainChecker.notInIPSetCorrect')}</span>
                </div>
                <div className="flex items-center gap-2">
                  <Route className="h-4 w-4 text-yellow-500" />
                  <span>{t('dashboard.domainChecker.inIPSetUnexpected')}</span>
                </div>
                <div className="flex items-center gap-2">
                  <RouteOff className="h-4 w-4 text-red-600" />
                  <span>{t('dashboard.domainChecker.notInIPSetExpected')}</span>
                </div>
              </div>
            </div>
          </div>
        )}

        {/* Console Output for Ping/Traceroute */}
        {(state.type === 'ping' || state.type === 'traceroute') &&
          state.consoleOutput.length > 0 && (
            <div className="rounded-lg border bg-black p-4">
              <div className="space-y-1 font-mono text-sm text-green-400">
                {state.consoleOutput.map((line, index) => (
                  <div key={index}>{line}</div>
                ))}
              </div>
            </div>
          )}
      </CardContent>
    </Card>
  );
}
