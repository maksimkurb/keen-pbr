import { useState, useEffect, useRef, useCallback } from 'react';
import { apiClient } from '../api/client';

export type CheckStatus = 'idle' | 'checking' | 'success' | 'browser-fail' | 'pc-success';

interface PCCheckState {
	randomString: string;
	waiting: boolean;
	timeout: boolean;
}

interface UseDNSCheckReturn {
	status: CheckStatus;
	browserRandomString: string;
	pcCheckState: PCCheckState;
	startBrowserCheck: () => void;
	startPCCheck: () => void;
	reset: () => void;
}

export function useDNSCheck(): UseDNSCheckReturn {
	const [status, setStatus] = useState<CheckStatus>('idle');
	const [browserRandomString, setBrowserRandomString] = useState('');
	const [pcCheckState, setPCCheckState] = useState<PCCheckState>({
		randomString: '',
		waiting: false,
		timeout: false,
	});

	const eventSourceRef = useRef<EventSource | null>(null);
	const timeoutRef = useRef<NodeJS.Timeout | null>(null);
	const fetchControllerRef = useRef<AbortController | null>(null);
	const pcTimeoutRef = useRef<NodeJS.Timeout | null>(null);

	// Generate random string for DNS check
	const generateRandomString = useCallback(() => {
		return Math.random().toString(36).substring(2, 15);
	}, []);

	// Cleanup function
	const cleanup = useCallback(() => {
		if (eventSourceRef.current) {
			eventSourceRef.current.close();
			eventSourceRef.current = null;
		}
		if (timeoutRef.current) {
			clearTimeout(timeoutRef.current);
			timeoutRef.current = null;
		}
		if (fetchControllerRef.current) {
			fetchControllerRef.current.abort();
			fetchControllerRef.current = null;
		}
		if (pcTimeoutRef.current) {
			clearTimeout(pcTimeoutRef.current);
			pcTimeoutRef.current = null;
		}
	}, []);

	// Cleanup on unmount
	useEffect(() => {
		return () => {
			cleanup();
		};
	}, [cleanup]);

	// Start browser-level DNS check
	const startBrowserCheck = useCallback(() => {
		cleanup();

		const randStr = generateRandomString();
		setBrowserRandomString(randStr);
		setStatus('checking');

		// Open SSE connection
		const sseUrl = apiClient.getSplitDNSCheckSSEUrl();
		const eventSource = new EventSource(sseUrl);
		eventSourceRef.current = eventSource;

		let sseReceived = false;
		const fetchTimeout = 5000; // 5 seconds

		// Listen for SSE events
		eventSource.onmessage = (event) => {
			const receivedDomain = event.data.trim();

			// Check if this is our random string
			if (receivedDomain === `${randStr}.dns-check.keen-pbr.internal`) {
				sseReceived = true;

				// Cancel fetch if still pending
				if (fetchControllerRef.current) {
					fetchControllerRef.current.abort();
				}

				// Clear timeout
				if (timeoutRef.current) {
					clearTimeout(timeoutRef.current);
				}

				setStatus('success');
				cleanup();
			}
		};

		eventSource.onerror = () => {
			console.error('SSE connection error');
		};

		// Make fetch request to trigger DNS lookup
		const controller = new AbortController();
		fetchControllerRef.current = controller;

		fetch(`http://${randStr}.dns-check.keen-pbr.internal`, {
			signal: controller.signal,
			mode: 'no-cors',
		}).catch((err) => {
			if (err.name !== 'AbortError') {
				console.log('Fetch failed (expected):', err);
			}
		});

		// Set timeout: fetch timeout + 5 seconds for SSE
		timeoutRef.current = setTimeout(() => {
			if (!sseReceived) {
				setStatus('browser-fail');
				cleanup();
			}
		}, fetchTimeout + 5000);
	}, [cleanup, generateRandomString]);

	// Start PC-level DNS check
	const startPCCheck = useCallback(() => {
		const randStr = generateRandomString();
		setPCCheckState({
			randomString: randStr,
			waiting: true,
			timeout: false,
		});

		// Open new SSE connection for PC check
		const sseUrl = apiClient.getSplitDNSCheckSSEUrl();
		const eventSource = new EventSource(sseUrl);
		eventSourceRef.current = eventSource;

		eventSource.onmessage = (event) => {
			const receivedDomain = event.data.trim();
			if (receivedDomain === `${randStr}.dns-check.keen-pbr.internal`) {
				setPCCheckState((prev) => ({
					...prev,
					waiting: false,
					timeout: false,
				}));
				setStatus('pc-success');
				if (pcTimeoutRef.current) {
					clearTimeout(pcTimeoutRef.current);
				}
				cleanup();
			}
		};

		eventSource.onerror = () => {
			console.error('SSE connection error in PC check');
		};

		// Set timeout for PC check (30 seconds)
		pcTimeoutRef.current = setTimeout(() => {
			setPCCheckState((prev) => ({
				...prev,
				waiting: false,
				timeout: true,
			}));
		}, 30000);
	}, [cleanup, generateRandomString]);

	// Reset to idle state
	const reset = useCallback(() => {
		cleanup();
		setStatus('idle');
		setBrowserRandomString('');
		setPCCheckState({
			randomString: '',
			waiting: false,
			timeout: false,
		});
	}, [cleanup]);

	return {
		status,
		browserRandomString,
		pcCheckState,
		startBrowserCheck,
		startPCCheck,
		reset,
	};
}
