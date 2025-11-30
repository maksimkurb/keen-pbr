import { useState, useEffect, useCallback, useRef } from 'react';
import { apiClient } from '../api/client';

export type CheckStatus =
  | 'idle'
  | 'checking'
  | 'success'
  | 'browser-fail'
  | 'sse-fail'
  | 'pc-success';

interface CheckState {
  randomString: string;
  waiting: boolean;
  showWarning: boolean;
}

interface UseDNSCheckReturn {
  status: CheckStatus;
  checkState: CheckState;
  startCheck: (performBrowserRequest: boolean) => void;
  reset: () => void;
}

export function useDNSCheck(): UseDNSCheckReturn {
  // Each hook instance gets its own stateful checker
  const eventSourceRef = useRef<EventSource | null>(null);
  const fetchControllerRef = useRef<AbortController | null>(null);
  const checkTimeoutRef = useRef<NodeJS.Timeout | null>(null);
  const warningTimeoutRef = useRef<NodeJS.Timeout | null>(null);

  const [status, setStatus] = useState<CheckStatus>('idle');
  const [checkState, setCheckState] = useState<CheckState>({
    randomString: '',
    waiting: false,
    showWarning: false,
  });

  // Generate random string for DNS check
  const generateRandomString = useCallback(() => {
    return Math.random().toString(36).substring(2, 15);
  }, []);

  // Cleanup function for SSE and timers
  const cleanup = useCallback(() => {
    if (eventSourceRef.current) {
      eventSourceRef.current.close();
      eventSourceRef.current = null;
    }
    if (fetchControllerRef.current) {
      fetchControllerRef.current.abort();
      fetchControllerRef.current = null;
    }
    if (checkTimeoutRef.current) {
      clearTimeout(checkTimeoutRef.current);
      checkTimeoutRef.current = null;
    }
    if (warningTimeoutRef.current) {
      clearTimeout(warningTimeoutRef.current);
      warningTimeoutRef.current = null;
    }
  }, []);

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      cleanup();
    };
  }, [cleanup]);

  // Start DNS check
  // performBrowserRequest=true: Browser check (makes fetch request, short timeout)
  // performBrowserRequest=false: PC check (no fetch, waits for manual nslookup, long timeout)
  const startCheck = useCallback(
    (performBrowserRequest: boolean) => {
      // Cancel any existing check first
      cleanup();

      const randStr = generateRandomString();
      const timeout = performBrowserRequest ? 5000 : 300000;
      const domain = `${randStr}.dns-check.keen-pbr.internal`;

      setCheckState({
        randomString: randStr,
        waiting: !performBrowserRequest,
        showWarning: false,
      });
      setStatus('checking');

      // Set up warning timer for PC check (30 seconds)
      if (!performBrowserRequest) {
        warningTimeoutRef.current = setTimeout(() => {
          setCheckState((prev) => ({
            ...prev,
            showWarning: true,
          }));
        }, 30000);
      }

      // Open SSE connection
      const sseUrl = apiClient.getSplitDNSCheckSSEUrl();
      const eventSource = new EventSource(sseUrl);
      eventSourceRef.current = eventSource;

      let sseConnected = false;

      // Listen for SSE events
      eventSource.onmessage = (event) => {
        const message = event.data.trim();

        // Check if this is the "connected" confirmation message
        if (message === 'connected') {
          sseConnected = true;
          console.log('SSE connected');

          // Now that SSE is connected, make the fetch request if requested
          if (performBrowserRequest) {
            console.log('Making browser fetch request...');
            fetchControllerRef.current = new AbortController();
            fetch(`https://${domain}`, {
              signal: fetchControllerRef.current.signal,
              mode: 'no-cors',
            }).catch((err) => {
              if (err.name !== 'AbortError') {
                console.log('Fetch failed (expected):', err);
              }
            });
          } else {
            console.log('Skipping browser fetch (PC check mode)');
          }
          return;
        }

        // Check if this is our domain
        if (message === domain) {
          cleanup();
          setCheckState((prev) => ({
            ...prev,
            waiting: false,
            showWarning: false,
          }));
          setStatus(performBrowserRequest ? 'success' : 'pc-success');
        }
      };

      eventSource.onerror = (error) => {
        console.error('SSE connection error:', error);
        // Don't reject immediately, let the timeout handle it
      };

      // Set up timeout
      checkTimeoutRef.current = setTimeout(() => {
        cleanup();

        if (!sseConnected) {
          setStatus('sse-fail');
          console.error('DNS check timeout: could not connect to SSE endpoint');
          return;
        }

        if (performBrowserRequest) {
          setStatus('browser-fail');
          console.error('DNS check timeout: domain not received via SSE');
        } else {
          console.error('PC DNS check timeout');
          setCheckState((prev) => ({
            ...prev,
            waiting: false,
            showWarning: true,
          }));
        }
      }, timeout);
    },
    [generateRandomString, cleanup],
  );

  // Reset to idle state
  const reset = useCallback(() => {
    cleanup();
    setStatus('idle');
    setCheckState({
      randomString: '',
      waiting: false,
      showWarning: false,
    });
  }, [cleanup]);

  return {
    status,
    checkState,
    startCheck,
    reset,
  };
}
