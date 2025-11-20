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
	 * Performs a DNS check by generating a random domain, making a fetch request,
	 * and waiting for the domain to be received via SSE.
	 * @param randomString Random string to use for the domain (e.g., "abc123")
	 * @param fetchTimeout Timeout for the fetch request in milliseconds (default: 5000)
	 * @param sseTimeout Additional timeout for SSE after fetch in milliseconds (default: 5000)
	 * @returns Promise that resolves with DNSCheckResult if successful, rejects if timeout
	 */
	async checkDNS(
		randomString: string,
		fetchTimeout: number = 5000,
		sseTimeout: number = 5000
	): Promise<DNSCheckResult> {
		return new Promise((resolve, reject) => {
			const domain = `${randomString}.dns-check.keen-pbr.internal`;
			let sseReceived = false;

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
				const receivedDomain = event.data.trim();

				// Check if this is our domain
				if (receivedDomain === domain) {
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

			// Make fetch request to trigger DNS lookup
			this.fetchController = new AbortController();
			fetch(`http://${domain}`, {
				signal: this.fetchController.signal,
				mode: 'no-cors',
			}).catch((err) => {
				if (err.name !== 'AbortError') {
					console.log('Fetch failed (expected):', err);
				}
			});

			// Set timeout: fetchTimeout + sseTimeout
			this.timeoutId = setTimeout(() => {
				if (!sseReceived) {
					cleanup();
					reject(new Error(`DNS check timeout: domain ${domain} not received via SSE`));
				}
			}, fetchTimeout + sseTimeout);
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
