import { apiClient } from '../api/client';

export interface DNSCheckResult {
	success: boolean;
	domain: string;
	receivedViaSse: boolean;
}

export class DNSCheckService {
	private eventSource: EventSource | null = null;
	private fetchController: AbortController | null = null;
	private timeoutId: NodeJS.Timeout | null = null;

	/**
	 * Performs a DNS check by generating a random domain, optionally making a fetch request,
	 * and waiting for the domain to be received via SSE.
	 * @param randomString Random string to use for the domain (e.g., "abc123")
	 * @param performBrowserRequest Whether to make a browser fetch request (default: true). Set to false for PC-only checks.
	 * @param timeout Timeout for the fetch request in milliseconds (default: 5000)
	 * @returns Promise that resolves with DNSCheckResult if successful, rejects if timeout
	 */
	async checkDNS(
		randomString: string,
		performBrowserRequest: boolean = true,
		timeout: number = 5000
	): Promise<DNSCheckResult> {
		return new Promise((resolve, reject) => {
			if (this.eventSource) {
				throw new Error("Could not run dns check: reset event source first");
			}

			const domain = `${randomString}.dns-check.keen-pbr.internal`;
			let sseReceived = false;
			let sseConnected = false;

			// Cleanup function
			const cleanup = () => {
				if (this.eventSource) {
					this.eventSource.close();
					this.eventSource = null;
				}
				if (this.fetchController) {
					this.fetchController.abort();
					this.fetchController = null;
				}
				if (this.timeoutId) {
					clearTimeout(this.timeoutId);
					this.timeoutId = null;
				}
			};

			// Open SSE connection
			const sseUrl = apiClient.getSplitDNSCheckSSEUrl();
			this.eventSource = new EventSource(sseUrl);

			// Listen for SSE events
			this.eventSource.onmessage = (event) => {
				const message = event.data.trim();

				// Check if this is the "connected" confirmation message
				if (message === 'connected') {
					sseConnected = true;
					console.log('SSE connected');

					// Now that SSE is connected, make the fetch request if requested
					if (performBrowserRequest) {
						console.log('Making browser fetch request...');
						this.fetchController = new AbortController();
						fetch(`http://${domain}`, {
							signal: this.fetchController.signal,
							mode: 'no-cors',
						}).catch((err) => {
							if (err.name !== 'AbortError') {
								console.log('Fetch failed (expected):', err);
							}
						});
					} else {
						console.log('Skipping browser fetch (PC check mode)');
					}
					return;
				}

				// Check if this is our domain
				if (message === domain) {
					sseReceived = true;
					cleanup();
					resolve({
						success: true,
						domain,
						receivedViaSse: true,
					});
				}
			};

			this.eventSource.onerror = (error) => {
				console.error('SSE connection error:', error);
				// Don't reject immediately, let the timeout handle it
			};

			this.timeoutId = setTimeout(() => {
				if (!sseConnected) {
					cleanup();
					reject(new Error('DNS check timeout: could not connect to SSE endpoint'));
				}
				if (!sseReceived) {
					cleanup();
					reject(new Error('DNS check timeout: domain not received via SSE'));
				}
			}, timeout);
		});
	}

	/**
	 * Cancels any ongoing DNS check
	 */
	cancel(): void {
		if (this.eventSource) {
			this.eventSource.close();
			this.eventSource = null;
		}
		if (this.fetchController) {
			this.fetchController.abort();
			this.fetchController = null;
		}
		if (this.timeoutId) {
			clearTimeout(this.timeoutId);
			this.timeoutId = null;
		}
	}
}

// Export a singleton instance
export const dnsCheckService = new DNSCheckService();
