import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Card, CardContent, CardHeader, CardTitle } from '../ui/card';
import { Input } from '../ui/input';
import { Button } from '../ui/button';
import { Badge } from '../ui/badge';
import { Alert, AlertDescription, AlertTitle } from '../ui/alert';
import { Search, Loader2, AlertCircle, CheckCircle2 } from 'lucide-react';
import { apiClient } from '../../src/api/client';
import type {
	RoutingCheckResponse,
	PingCheckResponse,
	TracerouteCheckResponse,
} from '../../src/api/client';

type CheckType = 'routing' | 'ping' | 'traceroute';

interface CheckState {
	type: CheckType | null;
	loading: boolean;
	error: string | null;
	routingResult: RoutingCheckResponse | null;
	pingResult: PingCheckResponse | null;
	tracerouteResult: TracerouteCheckResponse | null;
}

export function DomainCheckerWidget() {
	const { t } = useTranslation();
	const [host, setHost] = useState('');
	const [state, setState] = useState<CheckState>({
		type: null,
		loading: false,
		error: null,
		routingResult: null,
		pingResult: null,
		tracerouteResult: null,
	});

	const handleCheckRouting = async () => {
		setState({
			type: 'routing',
			loading: true,
			error: null,
			routingResult: null,
			pingResult: null,
			tracerouteResult: null,
		});

		try {
			const result = await apiClient.checkRouting(host);
			setState((prev) => ({
				...prev,
				loading: false,
				routingResult: result,
			}));
		} catch (err) {
			setState((prev) => ({
				...prev,
				loading: false,
				error: err instanceof Error ? err.message : 'Failed to check routing',
			}));
		}
	};

	const handlePing = async () => {
		setState({
			type: 'ping',
			loading: true,
			error: null,
			routingResult: null,
			pingResult: null,
			tracerouteResult: null,
		});

		try {
			const result = await apiClient.checkPing(host);
			setState((prev) => ({
				...prev,
				loading: false,
				pingResult: result,
			}));
		} catch (err) {
			setState((prev) => ({
				...prev,
				loading: false,
				error: err instanceof Error ? err.message : 'Failed to ping',
			}));
		}
	};

	const handleTraceroute = async () => {
		setState({
			type: 'traceroute',
			loading: true,
			error: null,
			routingResult: null,
			pingResult: null,
			tracerouteResult: null,
		});

		try {
			const result = await apiClient.checkTraceroute(host);
			setState((prev) => ({
				...prev,
				loading: false,
				tracerouteResult: result,
			}));
		} catch (err) {
			setState((prev) => ({
				...prev,
				loading: false,
				error: err instanceof Error ? err.message : 'Failed to traceroute',
			}));
		}
	};

	const hasResults =
		state.routingResult || state.pingResult || state.tracerouteResult;

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
				</div>

				{state.error && (
					<Alert variant="destructive">
						<AlertCircle className="h-4 w-4" />
						<AlertTitle>Error</AlertTitle>
						<AlertDescription>{state.error}</AlertDescription>
					</Alert>
				)}

				{state.routingResult && (
					<div className="space-y-3 rounded-lg border p-4">
						<div className="flex items-center gap-2">
							<CheckCircle2 className="h-5 w-5 text-green-600" />
							<h3 className="font-semibold">Routing Check Results</h3>
						</div>

						{state.routingResult.resolved_ips &&
							state.routingResult.resolved_ips.length > 0 && (
								<div>
									<div className="text-sm font-medium mb-2">Resolved IPs</div>
									<div className="flex gap-2 flex-wrap">
										{state.routingResult.resolved_ips.map((ip) => (
											<Badge key={ip} variant="outline">
												{ip}
											</Badge>
										))}
									</div>
								</div>
							)}

						{state.routingResult.matched_ipsets &&
							state.routingResult.matched_ipsets.length > 0 && (
								<div>
									<div className="text-sm font-medium mb-2">
										Found in IPSets
									</div>
									<div className="flex gap-2 flex-wrap">
										{state.routingResult.matched_ipsets.map((match) => (
											<Badge key={match.ipset_name} variant="secondary">
												{match.ipset_name}
												<span className="ml-1 text-xs opacity-70">
													({match.match_type})
												</span>
											</Badge>
										))}
									</div>
								</div>
							)}

						{state.routingResult.routing && (
							<div>
								<div className="text-sm font-medium mb-2">
									Routing Configuration
								</div>
								<div className="text-sm text-muted-foreground space-y-1">
									{state.routingResult.routing.table && (
										<div>
											<strong>Table:</strong> {state.routingResult.routing.table}
										</div>
									)}
									{state.routingResult.routing.priority !== undefined && (
										<div>
											<strong>Priority:</strong>{' '}
											{state.routingResult.routing.priority}
										</div>
									)}
									{state.routingResult.routing.fwmark && (
										<div>
											<strong>FwMark:</strong>{' '}
											{state.routingResult.routing.fwmark}
										</div>
									)}
									{state.routingResult.routing.interface && (
										<div>
											<strong>Interface:</strong>{' '}
											{state.routingResult.routing.interface}
										</div>
									)}
									{state.routingResult.routing.dns_override && (
										<div>
											<strong>DNS Override:</strong>{' '}
											{state.routingResult.routing.dns_override}
										</div>
									)}
								</div>
							</div>
						)}

						{(!state.routingResult.matched_ipsets ||
							state.routingResult.matched_ipsets.length === 0) && (
							<Alert>
								<AlertDescription>
									This host is not found in any IPSets. It will use default
									routing.
								</AlertDescription>
							</Alert>
						)}
					</div>
				)}

				{state.pingResult && (
					<div className="space-y-3 rounded-lg border p-4">
						<div className="flex items-center gap-2">
							{state.pingResult.success ? (
								<CheckCircle2 className="h-5 w-5 text-green-600" />
							) : (
								<AlertCircle className="h-5 w-5 text-red-600" />
							)}
							<h3 className="font-semibold">Ping Results</h3>
						</div>

						{state.pingResult.resolved_ip && (
							<div className="text-sm">
								<strong>Resolved IP:</strong>{' '}
								<Badge variant="outline">{state.pingResult.resolved_ip}</Badge>
							</div>
						)}

						{state.pingResult.success ? (
							<div className="space-y-2">
								<div className="grid grid-cols-2 gap-2 text-sm">
									<div>
										<strong>Packets:</strong>{' '}
										{state.pingResult.packets_received || 0}/
										{state.pingResult.packets_sent || 0} received
									</div>
									<div>
										<strong>Packet Loss:</strong>{' '}
										{state.pingResult.packet_loss?.toFixed(1) || 0}%
									</div>
								</div>
								{(state.pingResult.min_rtt !== undefined ||
									state.pingResult.avg_rtt !== undefined ||
									state.pingResult.max_rtt !== undefined) && (
									<div className="text-sm">
										<strong>RTT:</strong> min={state.pingResult.min_rtt?.toFixed(2) || 'N/A'}ms
										/ avg={state.pingResult.avg_rtt?.toFixed(2) || 'N/A'}ms / max=
										{state.pingResult.max_rtt?.toFixed(2) || 'N/A'}ms
									</div>
								)}
								{state.pingResult.output && (
									<details className="text-xs">
										<summary className="cursor-pointer font-medium">
											Show full output
										</summary>
										<pre className="mt-2 rounded bg-muted p-2 overflow-x-auto whitespace-pre-wrap">
											{state.pingResult.output}
										</pre>
									</details>
								)}
							</div>
						) : (
							<Alert variant="destructive">
								<AlertDescription>
									{state.pingResult.error || 'Ping failed'}
								</AlertDescription>
							</Alert>
						)}
					</div>
				)}

				{state.tracerouteResult && (
					<div className="space-y-3 rounded-lg border p-4">
						<div className="flex items-center gap-2">
							{state.tracerouteResult.success ? (
								<CheckCircle2 className="h-5 w-5 text-green-600" />
							) : (
								<AlertCircle className="h-5 w-5 text-red-600" />
							)}
							<h3 className="font-semibold">Traceroute Results</h3>
						</div>

						{state.tracerouteResult.resolved_ip && (
							<div className="text-sm">
								<strong>Resolved IP:</strong>{' '}
								<Badge variant="outline">
									{state.tracerouteResult.resolved_ip}
								</Badge>
							</div>
						)}

						{state.tracerouteResult.success ? (
							<div className="space-y-2">
								{state.tracerouteResult.hops &&
									state.tracerouteResult.hops.length > 0 && (
										<div>
											<div className="text-sm font-medium mb-2">Hops</div>
											<div className="space-y-1 text-sm">
												{state.tracerouteResult.hops.map((hop) => (
													<div
														key={hop.hop}
														className="flex gap-2 items-baseline font-mono"
													>
														<span className="text-muted-foreground w-6">
															{hop.hop}.
														</span>
														<span className="flex-1">
															{hop.hostname && hop.hostname !== hop.ip ? (
																<>
																	{hop.hostname}{' '}
																	<span className="text-muted-foreground">
																		({hop.ip})
																	</span>
																</>
															) : (
																hop.ip || '* * *'
															)}
														</span>
														{hop.rtt !== undefined && (
															<span className="text-muted-foreground">
																{hop.rtt.toFixed(2)} ms
															</span>
														)}
													</div>
												))}
											</div>
										</div>
									)}
								{state.tracerouteResult.output && (
									<details className="text-xs">
										<summary className="cursor-pointer font-medium">
											Show full output
										</summary>
										<pre className="mt-2 rounded bg-muted p-2 overflow-x-auto whitespace-pre-wrap">
											{state.tracerouteResult.output}
										</pre>
									</details>
								)}
							</div>
						) : (
							<Alert variant="destructive">
								<AlertDescription>
									{state.tracerouteResult.error || 'Traceroute failed'}
								</AlertDescription>
							</Alert>
						)}
					</div>
				)}
			</CardContent>
		</Card>
	);
}
