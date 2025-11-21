import { useState, useEffect, useCallback } from 'react';
import { dnsCheckService } from '../services/dnsCheckService';

export type CheckStatus = 'idle' | 'checking' | 'success' | 'browser-fail' | 'pc-success';

interface PCCheckState {
	randomString: string;
	waiting: boolean;
	showWarning: boolean;
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
		};
	}, []);

	// Start browser-level DNS check
	const startBrowserCheck = useCallback(async () => {
		const randStr = generateRandomString();
		setBrowserRandomString(randStr);
		setStatus('checking');

		try {
			await dnsCheckService.checkDNS(randStr);
			setStatus('success');
		} catch (error) {
			console.error('Browser DNS check failed:', error);
			setStatus('browser-fail');
		}
	}, [generateRandomString]);

	// Start PC-level DNS check
	const startPCCheck = useCallback(async () => {
		const randStr = generateRandomString();
		setPCCheckState({
			randomString: randStr,
			waiting: true,
			showWarning: false,
		});

		// Set up warning timer for 30 seconds
		const timerId = setTimeout(() => {
			setPCCheckState((prev) => ({
				...prev,
				showWarning: true,
			}));
		}, 30000);
		setWarningTimeoutId(timerId);

		try {
			// Wait indefinitely (5 minutes timeout as a safety measure)
			await dnsCheckService.checkDNS(randStr, 5000, 295000); // 5s fetch + 295s SSE = 5 min total

			// Clear warning timer if still active
			if (timerId) {
				clearTimeout(timerId);
			}

			setPCCheckState((prev) => ({
				...prev,
				waiting: false,
				showWarning: false,
			}));
			setStatus('pc-success');
		} catch (error) {
			console.error('PC DNS check timeout:', error);
			// Keep waiting state but show timeout message
			setPCCheckState((prev) => ({
				...prev,
				waiting: false,
				showWarning: true,
			}));
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
		setBrowserRandomString('');
		setPCCheckState({
			randomString: '',
			waiting: false,
			showWarning: false,
		});
	}, [warningTimeoutId]);

	return {
		status,
		browserRandomString,
		pcCheckState,
		startBrowserCheck,
		startPCCheck,
		reset,
	};
}
