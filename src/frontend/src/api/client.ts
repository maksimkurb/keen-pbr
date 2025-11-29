/**
 * TypeScript client for keen-pbr REST API
 */

// Import all generated types
import type {
	// Response wrappers
	DataResponse,
	ErrorResponse,
	// List types
	ListInfo,
	ListStatistics,
	ListsResponse,
	ListDownloadResponse,
	// IPSet/Config types
	IPSetConfig,
	GeneralConfig,
	AutoUpdateConfig,
	DNSServerConfig,
	RoutingConfig,
	RoutingDNSConfig,
	IPTablesRule,
	// Interface types
	InterfaceInfo,
	InterfacesResponse,
	// Settings types
	SettingsResponse,
	// Status types
	StatusResponse,
	ServiceInfo,
	VersionInfo,
	// Service control types
	ServiceControlRequest,
	ServiceControlResponse,
	// Health check types
	CheckResult,
	HealthCheckResponse,
	// Network check types
	RoutingCheckResponse,
	HostnameRuleMatch,
	IPSetCheckResult,
	RuleCheckResult,
} from "./generated-types";

// Re-export commonly used types
export type {
	DataResponse,
	ErrorResponse,
	ListStatistics,
	ListInfo,
	ListDownloadResponse,
	RoutingConfig,
	IPTablesRule,
	IPSetConfig,
	RoutingDNSConfig,
	InterfaceInfo,
	AutoUpdateConfig,
	DNSServerConfig,
	GeneralConfig,
	ServiceInfo,
	VersionInfo,
	ServiceControlRequest,
	ServiceControlResponse,
	CheckResult,
	HealthCheckResponse,
	HostnameRuleMatch,
	RuleCheckResult,
	IPSetCheckResult,
	RoutingCheckResponse,
};

// Client-only request types (not generated from Go)
export interface CreateListRequest {
	list_name: string;
	url?: string;
	file?: string;
	hosts?: string[];
}

export interface UpdateListRequest {
	url?: string;
	file?: string;
	hosts?: string[];
}

export interface CreateIPSetRequest {
	ipset_name: string;
	lists: string[];
	ip_version: 4 | 6;
	flush_before_applying?: boolean;
	routing?: RoutingConfig;
	iptables_rule?: IPTablesRule[];
}

export interface UpdateIPSetRequest {
	lists?: string[];
	ip_version?: 4 | 6;
	flush_before_applying?: boolean;
	routing?: RoutingConfig;
	iptables_rule?: IPTablesRule[];
}

export interface UpdateSettingsRequest {
	lists_output_dir?: string;
	interface_monitoring_interval_seconds?: number;
	auto_update_lists?: AutoUpdateConfig;
	dns_server?: DNSServerConfig;
}

// Enhanced StatusInfo (StatusResponse from generated types)
export type StatusInfo = StatusResponse;

// API Client class
export class KeenPBRClient {
	private baseURL: string;

	constructor(baseURL = "") {
		this.baseURL = baseURL;
	}

	private async request<T>(
		method: string,
		path: string,
		body?: unknown,
	): Promise<T> {
		const url = `${this.baseURL}/api/v1${path}`;
		const headers: HeadersInit = {};

		if (body) {
			headers["Content-Type"] = "application/json";
		}

		const response = await fetch(url, {
			method,
			headers,
			body: body ? JSON.stringify(body) : undefined,
		});

		if (!response.ok) {
			const error: ErrorResponse = await response.json();
			throw new Error(error.error.message);
		}

		const result: DataResponse = await response.json();
		return result.data as T;
	}

	// Lists API
	async getLists(): Promise<ListInfo[]> {
		const result = await this.request<ListsResponse>("GET", "/lists");
		return result.lists.filter((list): list is ListInfo => list !== null);
	}

	async getList(name: string): Promise<ListInfo> {
		return this.request<ListInfo>("GET", `/lists/${encodeURIComponent(name)}`);
	}

	async createList(data: CreateListRequest): Promise<ListInfo> {
		return this.request<ListInfo>("POST", "/lists", data);
	}

	async updateList(
		name: string,
		data: UpdateListRequest,
	): Promise<ListInfo> {
		return this.request<ListInfo>(
			"PUT",
			`/lists/${encodeURIComponent(name)}`,
			data,
		);
	}

	async deleteList(name: string): Promise<void> {
		await this.request<void>("DELETE", `/lists/${encodeURIComponent(name)}`);
	}

	async downloadList(name: string): Promise<ListDownloadResponse> {
		return this.request<ListDownloadResponse>(
			"POST",
			`/lists-download/${encodeURIComponent(name)}`,
		);
	}

	async downloadAllLists(): Promise<{ message: string }> {
		return this.request<{ message: string }>("POST", "/lists-download");
	}

	// IPSets API
	async getIPSets(): Promise<IPSetConfig[]> {
		const result = await this.request<{ ipsets: (IPSetConfig | null)[] }>(
			"GET",
			"/ipsets",
		);
		return result.ipsets.filter((ipset): ipset is IPSetConfig => ipset !== null);
	}

	async getIPSet(name: string): Promise<IPSetConfig> {
		return this.request<IPSetConfig>(
			"GET",
			`/ipsets/${encodeURIComponent(name)}`,
		);
	}

	async createIPSet(data: CreateIPSetRequest): Promise<IPSetConfig> {
		return this.request<IPSetConfig>("POST", "/ipsets", data);
	}

	async updateIPSet(
		name: string,
		data: UpdateIPSetRequest,
	): Promise<IPSetConfig> {
		return this.request<IPSetConfig>(
			"PUT",
			`/ipsets/${encodeURIComponent(name)}`,
			data,
		);
	}

	async deleteIPSet(name: string): Promise<void> {
		await this.request<void>("DELETE", `/ipsets/${encodeURIComponent(name)}`);
	}

	// Interfaces API
	async getInterfaces(): Promise<InterfaceInfo[]> {
		const result = await this.request<InterfacesResponse>(
			"GET",
			"/interfaces",
		);
		return result.interfaces;
	}

	// Settings API
	async getSettings(): Promise<GeneralConfig> {
		const result = await this.request<SettingsResponse>(
			"GET",
			"/settings",
		);
		if (!result.general) {
			throw new Error("Settings not found in response");
		}
		return result.general;
	}

	async updateSettings(data: UpdateSettingsRequest): Promise<GeneralConfig> {
		const result = await this.request<SettingsResponse>(
			"PATCH",
			"/settings",
			data,
		);
		if (!result.general) {
			throw new Error("Settings not found in response");
		}
		return result.general;
	}

	// Status API
	async getStatus(): Promise<StatusInfo> {
		return this.request<StatusInfo>("GET", "/status");
	}

	// Service Control API
	async controlService(
		state: "started" | "stopped" | "restarted",
	): Promise<ServiceControlResponse> {
		return this.request<ServiceControlResponse>("POST", "/service", { state });
	}

	// Network Check API
	async checkRouting(host: string): Promise<RoutingCheckResponse> {
		return this.request<RoutingCheckResponse>("POST", "/check/routing", {
			host,
		});
	}

	// Get SSE URL for ping (client will handle EventSource)
	getPingSSEUrl(host: string): string {
		return `${this.baseURL}/api/v1/check/ping?host=${encodeURIComponent(host)}`;
	}

	// Get SSE URL for traceroute (client will handle EventSource)
	getTracerouteSSEUrl(host: string): string {
		return `${this.baseURL}/api/v1/check/traceroute?host=${encodeURIComponent(host)}`;
	}

	// Get SSE URL for split-DNS check (client will handle EventSource)
	getSplitDNSCheckSSEUrl(): string {
		return `${this.baseURL}/api/v1/check/split-dns`;
	}
}

// Export a default instance
export const apiClient = new KeenPBRClient();
