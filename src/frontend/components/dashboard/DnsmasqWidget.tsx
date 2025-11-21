import { useState } from 'react';
import { useQueryClient } from '@tanstack/react-query';
import { useTranslation } from 'react-i18next';
import { Card, CardContent, CardHeader, CardTitle } from '../ui/card';
import { Alert, AlertDescription } from '../ui/alert';
import { Button } from '../ui/button';
import { Badge } from '../ui/badge';
import { RotateCw } from 'lucide-react';
import { apiClient } from '@/src/api/client';
import { useStatus } from '@/src/hooks/useStatus';

export function DnsmasqWidget() {
	const { t } = useTranslation();
	const queryClient = useQueryClient();
	const [controlLoading, setControlLoading] = useState(false);
	const { data, isLoading, error } = useStatus();

	const handleRestart = async () => {
		setControlLoading(true);
		try {
			await apiClient.restartDnsmasq();

			// Wait a bit before refreshing status to allow service to change state
			await new Promise((resolve) => setTimeout(resolve, 500));
			queryClient.invalidateQueries({ queryKey: ['status'] });
		} catch (err) {
			console.error('Failed to restart dnsmasq:', err);
		} finally {
			setControlLoading(false);
		}
	};

	if (error) {
		return (
			<Card>
				<CardHeader>
					<CardTitle>dnsmasq</CardTitle>
				</CardHeader>
				<CardContent>
					<Alert variant="destructive">
						<AlertDescription>
							{error instanceof Error ? error.message : t('common.error')}
						</AlertDescription>
					</Alert>
				</CardContent>
			</Card>
		);
	}

	if (isLoading || !data) {
		return (
			<Card>
				<CardHeader>
					<CardTitle>dnsmasq</CardTitle>
				</CardHeader>
				<CardContent>
					<div className="h-20 bg-muted animate-pulse rounded-lg" />
				</CardContent>
			</Card>
		);
	}

	const dnsmasqStatus = data.services.dnsmasq?.status || 'unknown';

	// Check if dnsmasq config is outdated
	const dnsmasqConfigHash = data.services.dnsmasq?.config_hash;
	const dnsmasqNotConfigured = !dnsmasqConfigHash; // Empty hash means not configured
	const dnsmasqOutdated =
		dnsmasqConfigHash &&
		data.current_config_hash &&
		dnsmasqConfigHash !== data.current_config_hash;

	const cardClassName = dnsmasqNotConfigured
		? 'bg-red-50 dark:bg-red-950'
		: dnsmasqOutdated
		? 'bg-yellow-50 dark:bg-yellow-950'
		: '';

	return (
		<Card className={cardClassName}>
			<CardHeader>
				<CardTitle>dnsmasq</CardTitle>
			</CardHeader>
			<CardContent className="space-y-4">
				<div>
					<div className="text-sm text-muted-foreground mb-1">{t('dashboard.serviceStatus')}</div>
					<div className="flex items-center gap-2">
						<Badge
							variant={dnsmasqStatus === 'running' ? 'default' : 'secondary'}
							className={
								dnsmasqStatus === 'running'
									? 'bg-green-500 hover:bg-green-600'
									: dnsmasqStatus === 'stopped'
									? 'bg-gray-500'
									: 'bg-yellow-500'
							}
						>
							{t(`dashboard.status${dnsmasqStatus.charAt(0).toUpperCase() + dnsmasqStatus.slice(1)}`)}
						</Badge>
						{dnsmasqNotConfigured && (
							<Badge variant="destructive">{t('dashboard.badgeMisconfigured')}</Badge>
						)}
						{!dnsmasqNotConfigured && dnsmasqOutdated && (
							<Badge variant="outline">{t('dashboard.badgeStale')}</Badge>
						)}
					</div>
				</div>

				<Button
					size="sm"
					variant="outline"
					onClick={handleRestart}
					disabled={controlLoading}
					className="w-full"
				>
					<RotateCw className="h-3 w-3 mr-1" />
					{t('common.restart')}
				</Button>
			</CardContent>
		</Card>
	);
}
