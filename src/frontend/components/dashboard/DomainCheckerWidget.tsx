import { useState, useRef, useEffect } from 'react';
import { useTranslation } from 'react-i18next';
import { Card, CardContent, CardHeader, CardTitle } from '../ui/card';
import { Input } from '../ui/input';
import { Button } from '../ui/button';
import { Badge } from '../ui/badge';
import { Alert, AlertDescription } from '../ui/alert';
import { Search, Loader2, AlertCircle, CheckCircle2, X } from 'lucide-react';
import { apiClient } from '../../src/api/client';
import type {
	RoutingCheckResponse,
} from '../../src/api/client';

type CheckType = 'routing' | 'ping' | 'traceroute';

interface CheckState {
	type: CheckType | null;
	loading: boolean;
	error: string | null;
	routingResult: RoutingCheckResponse | null;
	consoleOutput: string[];
}

export function DomainCheckerWidget() {
	const { t } = useTranslation();
	const [host, setHost] = useState('');
	const [state, setState] = useState<CheckState>({
		type: null,
		loading: false,
		error: null,
		routingResult: null,
		consoleOutput: [],
	});
	const eventSourceRef = useRef<EventSource | null>(null);
	const consoleEndRef = useRef<HTMLDivElement | null>(null);

	// Auto-scroll console to bottom when new output arrives
	useEffect(() => {
		if (consoleEndRef.current) {
			consoleEndRef.current.scrollIntoView({ behavior: 'smooth' });
		}
	}, [state.consoleOutput]);

	// Cleanup EventSource on unmount
	useEffect(() => {
		return () => {
			if (eventSourceRef.current) {
				eventSourceRef.current.close();
			}
		};
	}, []);

	const handleCheckRouting = async () => {
		setState({
			type: 'routing',
			loading: true,
			error: null,
			routingResult: null,
			consoleOutput: [],
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

	const handlePing = () => {
		// Close any existing EventSource
		if (eventSourceRef.current) {
			eventSourceRef.current.close();
		}

		setState({
			type: 'ping',
			loading: true,
			error: null,
			routingResult: null,
			consoleOutput: [],
		});

		const url = apiClient.getPingSSEUrl(host);
		const eventSource = new EventSource(url);
		eventSourceRef.current = eventSource;

		eventSource.onmessage = (event) => {
			setState((prev) => ({
				...prev,
				consoleOutput: [...prev.consoleOutput, event.data],
			}));
		};

		eventSource.onerror = (error) => {
			console.error('SSE Error:', error);
			eventSource.close();
			setState((prev) => ({
				...prev,
				loading: false,
				error: 'Connection to server lost',
			}));
		};

		// Detect completion
		eventSource.addEventListener('message', (event) => {
			if (
				event.data.includes('[Process completed') ||
				event.data.includes('[Process exited')
			) {
				setTimeout(() => {
					eventSource.close();
					setState((prev) => ({ ...prev, loading: false }));
				}, 100);
			}
		});
	};

	const handleTraceroute = () => {
		// Close any existing EventSource
		if (eventSourceRef.current) {
			eventSourceRef.current.close();
		}

		setState({
			type: 'traceroute',
			loading: true,
			error: null,
			routingResult: null,
			consoleOutput: [],
		});

		const url = apiClient.getTracerouteSSEUrl(host);
		const eventSource = new EventSource(url);
		eventSourceRef.current = eventSource;

		eventSource.onmessage = (event) => {
			setState((prev) => ({
				...prev,
				consoleOutput: [...prev.consoleOutput, event.data],
			}));
		};

		eventSource.onerror = (error) => {
			console.error('SSE Error:', error);
			eventSource.close();
			setState((prev) => ({
				...prev,
				loading: false,
				error: 'Connection to server lost',
			}));
		};

		// Detect completion
		eventSource.addEventListener('message', (event) => {
			if (
				event.data.includes('[Process completed') ||
				event.data.includes('[Process exited')
			) {
				setTimeout(() => {
					eventSource.close();
					setState((prev) => ({ ...prev, loading: false }));
				}, 100);
			}
		});
	};

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
					<Button onClick={handlePing} disabled={!host || state.loading} variant="outline">
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
						<AlertDescription>{state.error}</AlertDescription>
					</Alert>
				)}

				{/* Routing Check Results */}
				{state.routingResult && (
					<div className="space-y-4 rounded-lg border p-4">
						<div className="flex items-center gap-2">
							<CheckCircle2 className="h-5 w-5 text-green-600" />
							<h3 className="font-semibold">
								Host: {state.routingResult.host}
							</h3>
						</div>

						{/* Hostname Matches */}
						{state.routingResult.matched_by_hostname &&
							state.routingResult.matched_by_hostname.length > 0 && (
								<div>
									<div className="text-sm font-medium mb-2">
										Present in Rules:
									</div>
									<ul className="list-disc list-inside text-sm space-y-1">
										{state.routingResult.matched_by_hostname.map((match) => (
											<li key={match.rule_name}>
												<strong>{match.rule_name}</strong> (hostname "
												{match.pattern}")
											</li>
										))}
									</ul>
								</div>
							)}

						{/* IP -> Rule -> Present Table */}
						{state.routingResult.ipset_checks &&
							state.routingResult.ipset_checks.length > 0 && (
								<div className="overflow-x-auto">
									<table className="w-full text-sm border-collapse">
										<thead>
											<tr className="border-b">
												<th className="text-left p-2 font-medium">
													IP Address
												</th>
												<th className="text-left p-2 font-medium">Rule</th>
												<th className="text-left p-2 font-medium">
													Present in IPset
												</th>
											</tr>
										</thead>
										<tbody>
											{state.routingResult.ipset_checks.map((ipCheck) => (
												<>
													{ipCheck.rule_results.map((ruleResult, index) => (
														<tr
															key={`${ipCheck.ip}-${ruleResult.rule_name}`}
															className="border-b"
														>
															{index === 0 && (
																<td
																	rowSpan={ipCheck.rule_results.length}
																	className="p-2 font-mono border-r align-top"
																>
																	{ipCheck.ip}
																</td>
															)}
															<td className="p-2">{ruleResult.rule_name}</td>
															<td className="p-2">
																<div className="flex items-center gap-2">
																	<span>
																		{ruleResult.present_in_ipset
																			? 'Yes'
																			: 'No'}
																	</span>
																	{ruleResult.present_in_ipset ===
																	ruleResult.should_be_present ? (
																		<CheckCircle2 className="h-4 w-4 text-green-600" />
																	) : (
																		<X className="h-4 w-4 text-red-600" />
																	)}
																	{ruleResult.match_reason && (
																		<span className="text-xs text-muted-foreground">
																			({ruleResult.match_reason})
																		</span>
																	)}
																</div>
															</td>
														</tr>
													))}
												</>
											))}
										</tbody>
									</table>
								</div>
							)}
					</div>
				)}

				{/* Console Output for Ping/Traceroute */}
				{(state.type === 'ping' || state.type === 'traceroute') &&
					state.consoleOutput.length > 0 && (
						<div className="rounded-lg border bg-black p-4">
							<div className="space-y-1 font-mono text-sm text-green-400">
								{state.consoleOutput.map((line, index) => (
									<div key={index}>{line}</div>
								))}
								<div ref={consoleEndRef} />
							</div>
						</div>
					)}
			</CardContent>
		</Card>
	);
}
