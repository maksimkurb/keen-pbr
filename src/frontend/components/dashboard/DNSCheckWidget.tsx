import { useState, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { Card, CardContent, CardHeader, CardTitle } from '../ui/card';
import { Button } from '../ui/button';
import { Alert, AlertDescription } from '../ui/alert';
import { Dialog, DialogContent, DialogHeader, DialogTitle, DialogDescription } from '../ui/dialog';
import { Loader2, CheckCircle2, AlertCircle, Terminal } from 'lucide-react';
import { useDNSCheck } from '../../src/hooks/useDNSCheck';

export function DNSCheckWidget() {
	const { t } = useTranslation();
	const [showPCCheckDialog, setShowPCCheckDialog] = useState(false);

	const {
		status,
		checkState,
		startCheck,
		reset,
	} = useDNSCheck();

	// Auto-run browser DNS check on component mount
	useEffect(() => {
		startCheck(true); // performBrowserRequest = true
		// eslint-disable-next-line react-hooks/exhaustive-deps
	}, []); // Only run on mount

	const handleCheckFromPC = () => {
		setShowPCCheckDialog(true);
		startCheck(false); // performBrowserRequest = false
	};

	const handleDialogClose = (open: boolean) => {
		setShowPCCheckDialog(open);
		if (!open && checkState.waiting) {
			// If dialog is closed while waiting, reset everything
			reset();
		}
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
									onClick={handleCheckFromPC}
								>
									{t('dnsCheck.checkFromPC')}
								</Button>
							</div>
						</AlertDescription>
					</Alert>
				);

			case 'sse-fail':
				return (
					<Alert className="border-yellow-200 bg-yellow-50">
						<AlertCircle className="h-4 w-4 text-yellow-600" />
						<AlertDescription className="text-yellow-800">
							<div className="space-y-2">
								<p>{t('dnsCheck.sseFail')}</p>
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
						<Button onClick={() => startCheck(true)}>
							{t('dnsCheck.startCheck')}
						</Button>
					)}

					{renderContent()}

					{status !== 'idle' && status !== 'checking' && (
						<Button variant="outline" size="sm" onClick={reset}>
							{t('dnsCheck.checkAgain')}
						</Button>
					)}
				</CardContent>
			</Card>

			<Dialog open={showPCCheckDialog} onOpenChange={handleDialogClose}>
				<DialogContent>
					<DialogHeader>
						<DialogTitle>{t('dnsCheck.pcCheckTitle')}</DialogTitle>
						<DialogDescription>
							{t('dnsCheck.pcCheckDescription')}
						</DialogDescription>
					</DialogHeader>

					<div className="space-y-4">
						{/* Single command for all operating systems */}
						<div>
							<h4 className="font-semibold mb-2">
								<Terminal className="inline h-4 w-4 mr-1" />
								{t('dnsCheck.pcCheckCommandTitle')}
							</h4>
							<code className="block bg-muted p-3 rounded text-sm break-all">
								nslookup {checkState.randomString}.dns-check.keen-pbr.internal
							</code>
						</div>

						{checkState.waiting && (
							<div className="flex items-center gap-2 text-sm">
								<Loader2 className="h-4 w-4 animate-spin" />
								{t('dnsCheck.pcCheckWaiting')}
							</div>
						)}

						{checkState.showWarning && (
							<Alert className="border-yellow-200 bg-yellow-50">
								<AlertCircle className="h-4 w-4 text-yellow-600" />
								<AlertDescription className="text-yellow-800">
									{t('dnsCheck.pcCheckWarning')}
								</AlertDescription>
							</Alert>
						)}
					</div>
				</DialogContent>
			</Dialog>
		</>
	);
}
