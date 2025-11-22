import { useState } from 'react';
import { useQueryClient } from '@tanstack/react-query';
import { useTranslation } from 'react-i18next';
import { Card, CardContent, CardHeader, CardTitle } from '../ui/card';
import { Alert, AlertDescription } from '../ui/alert';
import { Button } from '../ui/button';
import { Badge } from '../ui/badge';
import { Play, Square, RotateCw } from 'lucide-react';
import { apiClient } from '@/src/api/client';
import { useStatus } from '@/src/hooks/useStatus';

export function KeenPbrWidget() {
	const { t } = useTranslation();
	const queryClient = useQueryClient();
	const [controlLoading, setControlLoading] = useState<string | null>(null);
	const { data, isLoading, error } = useStatus();

	const handleServiceControl = async (action: 'start' | 'stop' | 'restart') => {
		setControlLoading(action);
		try {
			if (action === 'start') {
				await apiClient.controlService('started');
			} else if (action === 'stop') {
				await apiClient.controlService('stopped');
			} else if (action === 'restart') {
				await apiClient.controlService('restarted');
			}

			// Wait a bit before refreshing status to allow service to change state
			await new Promise((resolve) => setTimeout(resolve, 500));
			queryClient.invalidateQueries({ queryKey: ['status'] });
		} catch (err) {
			console.error(`Failed to ${action} keen-pbr:`, err);
		} finally {
			setControlLoading(null);
		}
	};

	if (error) {
		return (
			<Card>
				<CardHeader>
					<CardTitle>keen-pbr</CardTitle>
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
					<CardTitle>keen-pbr</CardTitle>
				</CardHeader>
				<CardContent>
					<div className="h-20 bg-muted animate-pulse rounded-lg" />
				</CardContent>
			</Card>
		);
	}

	const keenPbrStatus = data.services['keen-pbr']?.status || 'unknown';
	const configOutdated = data.configuration_outdated || false;

	return (
		<Card className={configOutdated ? 'bg-yellow-50 dark:bg-yellow-950' : ''}>
			<CardHeader>
				<CardTitle>keen-pbr</CardTitle>
			</CardHeader>
			<CardContent className="space-y-4">
				<div>
					<div className="text-sm text-muted-foreground mb-1">{t('dashboard.version')}</div>
					<div className="text-lg font-semibold">
						{data.version.version}
						<span className="text-xs text-muted-foreground ml-2">({data.version.commit})</span>
					</div>
				</div>

				<div>
					<div className="text-sm text-muted-foreground mb-1">{t('dashboard.serviceStatus')}</div>
					<div className="flex items-center gap-2">
						<Badge
							variant={keenPbrStatus === 'running' ? 'default' : 'secondary'}
							className={
								keenPbrStatus === 'running'
									? 'bg-green-500 hover:bg-green-600'
									: keenPbrStatus === 'stopped'
									? 'bg-gray-500'
									: 'bg-yellow-500'
							}
						>
							{t(`dashboard.status${keenPbrStatus.charAt(0).toUpperCase() + keenPbrStatus.slice(1)}`)}
						</Badge>
						{configOutdated && (
							<Badge variant="outline">{t('dashboard.badgeStale')}</Badge>
						)}
					</div>
				</div>

				<div className="flex flex-wrap gap-2">
					<div className="flex gap-1">
						<Button
							size="sm"
							variant="outline"
							onClick={() => handleServiceControl('start')}
							disabled={keenPbrStatus === 'running' || controlLoading === 'start'}
						>
							<Play className="h-3 w-3 mr-1" />
							{t('common.start')}
						</Button>
						<Button
							size="sm"
							variant="outline"
							onClick={() => handleServiceControl('stop')}
							disabled={keenPbrStatus === 'stopped' || controlLoading === 'stop'}
						>
							<Square className="h-3 w-3 mr-1" />
							{t('common.stop')}
						</Button>
					</div>
					<Button
						size="sm"
						variant="outline"
						onClick={() => handleServiceControl('restart')}
						disabled={controlLoading === 'restart'}
					>
						<RotateCw className="h-3 w-3 mr-1" />
						{t('common.restart')}
					</Button>
				</div>
			</CardContent>
		</Card>
	);
}
