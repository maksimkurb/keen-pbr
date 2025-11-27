import { useState, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { Card, CardContent, CardHeader, CardTitle } from '../ui/card';
import { Button } from '../ui/button';
import { Loader2, CheckCircle2, AlertCircle } from 'lucide-react';
import { useDNSCheck } from '../../src/hooks/useDNSCheck';
import { DNSCheckModal } from './DNSCheckModal';

export function DNSCheckWidget() {
	const { t } = useTranslation();
	const [showPCCheckDialog, setShowPCCheckDialog] = useState(false);

	// Main widget DNS check (browser-based)
	const {
		status,
		startCheck,
		reset,
	} = useDNSCheck();

	// Auto-run browser DNS check on component mount
	useEffect(() => {
		startCheck(true); // performBrowserRequest = true
		// eslint-disable-next-line react-hooks/exhaustive-deps
	}, []); // Only run on mount

	const isChecking = status === 'checking';

	const handleRetry = () => {
		reset();
		startCheck(true);
	};

	const getCardClassName = () => {
		switch (status) {
			case 'success':
			case 'pc-success':
				return 'border-green-600 bg-green-600/5';
			case 'browser-fail':
			case 'sse-fail':
				return 'border-destructive bg-destructive/10';
			default:
				return '';
		}
	};

	const renderContent = () => {
		switch (status) {
			case 'idle':
			case 'checking':
				return (
					<div className="flex items-center justify-center py-4">
						<Loader2 className="h-8 w-8 animate-spin" />
					</div>
				);

			case 'success':
				return (
					<div className="flex items-center gap-2 text-chart-2">
						<CheckCircle2 className="h-5 w-5" />
						<span>{t('dnsCheck.success')}</span>
					</div>
				);

			case 'browser-fail':
				return (
					<div className="flex items-center gap-2 text-destructive">
						<AlertCircle className="h-5 w-5" />
						<span>{t('dnsCheck.browserFail')}</span>
					</div>
				);

			case 'sse-fail':
				return (
					<div className="flex items-center gap-2 text-destructive">
						<AlertCircle className="h-5 w-5" />
						<span>{t('dnsCheck.sseFail')}</span>
					</div>
				);

			case 'pc-success':
				return (
					<div className="flex items-center gap-2 text-chart-2">
						<CheckCircle2 className="h-5 w-5" />
						<span>{t('dnsCheck.pcSuccess')}</span>
					</div>
				);

			default:
				return null;
		}
	};

	return (
		<>
			<Card className={`flex flex-col ${getCardClassName()}`}>
				<CardHeader>
					<CardTitle>{t('dnsCheck.title')}</CardTitle>
				</CardHeader>
				<CardContent className="flex flex-col flex-1 justify-between gap-4">
					{renderContent()}

					<div className="flex flex-col gap-2 mt-auto">
						<Button
							variant="outline"
							className="w-full"
							onClick={handleRetry}
							disabled={isChecking}
						>
							{isChecking ? t('dnsCheck.checking') : t('dnsCheck.checkAgain')}
						</Button>
						<Button
							variant="outline"
							className="w-full"
							onClick={() => setShowPCCheckDialog(true)}
						>
							{t('dnsCheck.checkFromPC')}
						</Button>
					</div>
				</CardContent>
			</Card>

			<DNSCheckModal
				open={showPCCheckDialog}
				onOpenChange={setShowPCCheckDialog}
				browserStatus={status}
			/>
		</>
	);
}
