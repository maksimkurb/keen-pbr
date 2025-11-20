import { useState, useEffect, useRef } from 'react';
import { useTranslation } from 'react-i18next';
import { Card, CardContent, CardHeader, CardTitle } from '../ui/card';
import { Button } from '../ui/button';
import { Alert, AlertDescription } from '../ui/alert';
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogDescription } from '../ui/dialog';
import { Loader2, CheckCircle2, AlertCircle, Terminal } from 'lucide-react';
import { apiClient } from '../../src/api/client';

type CheckStatus = 'idle' | 'checking' | 'success' | 'browser-fail' | 'pc-success';

export function DNSCheckWidget() {
	const { t } = useTranslation();
	const [status, setStatus] = useState<CheckStatus>('idle');
	const [showPCCheckDialog, setShowPCCheckDialog] = useState(false);
	const [randomString, setRandomString] = useState('');
	const [pcRandomString, setPCRandomString] = useState('');
	const [pcCheckWaiting, setPCCheckWaiting] = useState(false);
	const [pcCheckTimeout, setPCCheckTimeout] = useState(false);
	const eventSourceRef = useRef<EventSource | null>(null);
	const timeoutRef = useRef<NodeJS.Timeout | null>(null);
	const fetchControllerRef = useRef<AbortController | null>(null);
	const pcTimeoutRef = useRef<NodeJS.Timeout | null>(null);

	// Generate random string
	const generateRandomString = () => {
		return Math.random().toString(36).substring(2, 15);
	};

	// Cleanup on unmount
	useEffect(() => {
		return () => {
			cleanup();
		};
	}, []);

	const cleanup = () => {
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
	};

	const startCheck = () => {
		cleanup();

		const randStr = generateRandomString();
		setRandomString(randStr);
		setStatus('checking');

		// Open SSE connection
		const sseUrl = apiClient.getSplitDNSCheckSSEUrl();
		const eventSource = new EventSource(sseUrl);
		eventSourceRef.current = eventSource;

		let sseReceived = false;
		const fetchTimeout = 5000; // 5 seconds default fetch timeout

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

		// Make fetch request
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

		// Set timeout: fetch timeout + 5 seconds
		timeoutRef.current = setTimeout(() => {
			if (!sseReceived) {
				setStatus('browser-fail');
				cleanup();
			}
		}, fetchTimeout + 5000);
	};

	const startPCCheck = () => {
		const randStr = generateRandomString();
		setPCRandomString(randStr);
		setPCCheckWaiting(true);
		setPCCheckTimeout(false);

		// Wait for SSE event (listening for this domain on existing SSE connection)
		// We need to open a new SSE connection since the previous one was closed
		const sseUrl = apiClient.getSplitDNSCheckSSEUrl();
		const eventSource = new EventSource(sseUrl);
		eventSourceRef.current = eventSource;

		eventSource.onmessage = (event) => {
			const receivedDomain = event.data.trim();
			if (receivedDomain === `${randStr}.dns-check.keen-pbr.internal`) {
				setPCCheckWaiting(false);
				setPCCheckTimeout(false);
				setStatus('pc-success');
				setShowPCCheckDialog(false);
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
			setPCCheckWaiting(false);
			setPCCheckTimeout(true);
		}, 30000);
	};

	const renderContent = () => {
		switch (status) {
			case 'idle':
				return null;

			case 'checking':
				return (
					<div className="flex items-center gap-2 text-sm">
						<Loader2 className="h-4 w-4 animate-spin" />
						{t('dnsCheck.checking')}
					</div>
				);

			case 'success':
				return (
					<Alert className="border-green-200 bg-green-50">
						<CheckCircle2 className="h-4 w-4 text-green-600" />
						<AlertDescription className="text-green-800">
							{t('dnsCheck.success')}
						</AlertDescription>
					</Alert>
				);

			case 'browser-fail':
				return (
					<Alert className="border-yellow-200 bg-yellow-50">
						<AlertCircle className="h-4 w-4 text-yellow-600" />
						<AlertDescription className="text-yellow-800">
							<div className="space-y-2">
								<p>{t('dnsCheck.browserFail')}</p>
								<Button
									variant="outline"
									size="sm"
									onClick={() => {
										setShowPCCheckDialog(true);
										startPCCheck();
									}}
								>
									{t('dnsCheck.checkFromPC')}
								</Button>
							</div>
						</AlertDescription>
					</Alert>
				);

			case 'pc-success':
				return (
					<Alert className="border-blue-200 bg-blue-50">
						<AlertCircle className="h-4 w-4 text-blue-600" />
						<AlertDescription className="text-blue-800">
							{t('dnsCheck.pcSuccess')}
						</AlertDescription>
					</Alert>
				);

			default:
				return null;
		}
	};

	return (
		<>
			<Card>
				<CardHeader>
					<CardTitle>{t('dnsCheck.title')}</CardTitle>
				</CardHeader>
				<CardContent className="space-y-4">
					<p className="text-sm text-muted-foreground">
						{t('dnsCheck.description')}
					</p>

					{status === 'idle' && (
						<Button onClick={startCheck}>
							{t('dnsCheck.startCheck')}
						</Button>
					)}

					{renderContent()}

					{status !== 'idle' && status !== 'checking' && (
						<Button
							variant="outline"
							size="sm"
							onClick={() => {
								setStatus('idle');
								cleanup();
							}}
						>
							{t('dnsCheck.checkAgain')}
						</Button>
					)}
				</CardContent>
			</Card>

			<Dialog open={showPCCheckDialog} onOpenChange={setShowPCCheckDialog}>
				<DialogContent>
					<DialogHeader>
						<DialogTitle>{t('dnsCheck.pcCheckTitle')}</DialogTitle>
						<DialogDescription>
							{t('dnsCheck.pcCheckDescription')}
						</DialogDescription>
					</DialogHeader>

					<div className="space-y-4">
						{/* Windows instructions */}
						<div>
							<h4 className="font-semibold mb-2">
								<Terminal className="inline h-4 w-4 mr-1" />
								Windows
							</h4>
							<code className="block bg-muted p-3 rounded text-sm break-all">
								nslookup {pcRandomString}.dns-check.keen-pbr.internal
							</code>
						</div>

						{/* Linux instructions */}
						<div>
							<h4 className="font-semibold mb-2">
								<Terminal className="inline h-4 w-4 mr-1" />
								Linux
							</h4>
							<code className="block bg-muted p-3 rounded text-sm break-all">
								nslookup {pcRandomString}.dns-check.keen-pbr.internal
							</code>
						</div>

						{/* macOS instructions */}
						<div>
							<h4 className="font-semibold mb-2">
								<Terminal className="inline h-4 w-4 mr-1" />
								macOS
							</h4>
							<code className="block bg-muted p-3 rounded text-sm break-all">
								nslookup {pcRandomString}.dns-check.keen-pbr.internal
							</code>
						</div>

						{pcCheckWaiting && (
							<div className="flex items-center gap-2 text-sm">
								<Loader2 className="h-4 w-4 animate-spin" />
								{t('dnsCheck.pcCheckWaiting')}
							</div>
						)}

						{pcCheckTimeout && (
							<Alert className="border-yellow-200 bg-yellow-50">
								<AlertCircle className="h-4 w-4 text-yellow-600" />
								<AlertDescription className="text-yellow-800">
									{t('dnsCheck.pcCheckTimeout')}
								</AlertDescription>
							</Alert>
						)}
					</div>
				</DialogContent>
			</Dialog>
		</>
	);
}
