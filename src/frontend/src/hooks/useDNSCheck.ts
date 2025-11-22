import { useState, useEffect, useCallback } from 'react';
import { dnsCheckService } from '../services/dnsCheckService';

export type CheckStatus = 'idle' | 'checking' | 'success' | 'browser-fail' | 'sse-fail' | 'pc-success';

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
	const [status, setStatus] = useState<CheckStatus>('idle');
	const [checkState, setCheckState] = useState<CheckState>({
		randomString: '',
		waiting: false,
		showWarning: false,
	});
	const [warningTimeoutId, setWarningTimeoutId] = useState<NodeJS.Timeout | null>(null);

	// Generate random string for DNS check
	const generateRandomString = useCallback(() => {
		return Math.random().toString(36).substring(2, 15);
	}, []);

	// Cleanup on unmount
	useEffect(() => {
		return () => {
			dnsCheckService.cancel();
			if (warningTimeoutId) {
				clearTimeout(warningTimeoutId);
			}
		};
	}, [warningTimeoutId]);

	// Start DNS check
	// performBrowserRequest=true: Browser check (makes fetch request)
	// performBrowserRequest=false: PC check (waits for manual nslookup)
	const startCheck = useCallback(async (performBrowserRequest: boolean) => {
		const randStr = generateRandomString();

		if (performBrowserRequest) {
			// Browser check
			setCheckState({
				randomString: randStr,
				waiting: false,
				showWarning: false,
			});
			setStatus('checking');

			try {
				await dnsCheckService.checkDNS(randStr, true, 5000);
				setStatus('success');
			} catch (error) {
				if (error instanceof Error && error.message.includes('domain not received via SSE')) {
					setStatus('browser-fail');
				} else {
					setStatus('sse-fail');
				}

				console.error('Browser DNS check failed:', error);
			}
		} else {
			// PC check (no browser request, longer timeout, with warning)
			setCheckState({
				randomString: randStr,
				waiting: true,
				showWarning: false,
			});

			// Set up warning timer for 30 seconds
			const timerId = setTimeout(() => {
				setCheckState((prev) => ({
					...prev,
					showWarning: true,
				}));
			}, 30000);
			setWarningTimeoutId(timerId);

			try {
				// Wait indefinitely (5 minutes timeout as a safety measure)
				await dnsCheckService.checkDNS(randStr, false, 300000);

				// Clear warning timer if still active
				if (timerId) {
					clearTimeout(timerId);
				}

				setCheckState((prev) => ({
					...prev,
					waiting: false,
					showWarning: false,
				}));
				setStatus('pc-success');
			} catch (error) {
				console.error('PC DNS check timeout:', error);
				// Keep waiting state but show timeout message
				setCheckState((prev) => ({
					...prev,
					waiting: false,
					showWarning: true,
				}));
			}
		}
	}, [generateRandomString]);

	// Reset to idle state
	const reset = useCallback(() => {
		dnsCheckService.cancel();
		if (warningTimeoutId) {
			clearTimeout(warningTimeoutId);
			setWarningTimeoutId(null);
		}
		setStatus('idle');
		setCheckState({
			randomString: '',
			waiting: false,
			showWarning: false,
		});
	}, [warningTimeoutId]);

	return {
		status,
		checkState,
		startCheck,
		reset,
	};
}
