/**
 * TypeScript client for keen-pbr REST API
 * Based on .claude/API_PLAN.md
 */

// API Response types
export interface DataResponse<T> {
	data: T;
}

export interface ErrorResponse {
	error: {
		code: string;
		message: string;
		details?: Record<string, unknown>;
	};
}

// List types
export interface ListStatistics {
	total_hosts: number | null;
	ipv4_subnets: number | null;
	ipv6_subnets: number | null;
	downloaded?: boolean;
	last_modified?: string;
}

export interface ListInfo {
	list_name: string;
	type: "url" | "file" | "hosts";
	url?: string;
	file?: string;
	stats: ListStatistics | null;
}

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

// IPSet types
export interface RoutingConfig {
	interface: string;
	table: string;
	priority: number;
	fwmark?: string;
	dns_override?: string;
	kill_switch?: boolean;
}

export interface IPSetConfig {
	ipset_name: string;
	lists: string[];
	ip_version: 4 | 6;
	flush_before_applying: boolean;
	routing?: RoutingConfig;
}

export interface CreateIPSetRequest {
	ipset_name: string;
	lists: string[];
	ip_version: 4 | 6;
	flush_before_applying?: boolean;
	routing?: RoutingConfig;
}

export interface UpdateIPSetRequest {
	lists?: string[];
	ip_version?: 4 | 6;
	flush_before_applying?: boolean;
	routing?: RoutingConfig;
}

// Settings types
export interface GeneralSettings {
	lists_output_dir: string;
	use_keenetic_dns: boolean;
	fallback_dns?: string;
	api_bind_address?: string;
}

export interface UpdateSettingsRequest {
	lists_output_dir?: string;
	use_keenetic_dns?: boolean;
	fallback_dns?: string;
	api_bind_address?: string;
}

// Status types
export interface ServiceInfo {
	status: "running" | "stopped" | "unknown";
	message?: string;
}

export interface VersionInfo {
	version: string;
	date: string;
	commit: string;
}

export interface StatusInfo {
	version: VersionInfo;
	keenetic_version?: string;
	services: Record<string, ServiceInfo>;
}

// Service control types
export interface ServiceControlRequest {
	up: boolean;
}

export interface ServiceControlResponse {
	status: string;
	message?: string;
}

// Health check types
export interface CheckResult {
	passed: boolean;
	message?: string;
}

export interface HealthCheckResponse {
	healthy: boolean;
	checks: Record<string, CheckResult>;
}

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

		const result: DataResponse<T> = await response.json();
		return result.data;
	}

	// Lists API
	async getLists(): Promise<ListInfo[]> {
		const result = await this.request<{ lists: ListInfo[] }>("GET", "/lists");
		return result.lists;
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

	// IPSets API
	async getIPSets(): Promise<IPSetConfig[]> {
		const result = await this.request<{ ipsets: IPSetConfig[] }>(
			"GET",
			"/ipsets",
		);
		return result.ipsets;
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

	// Settings API
	async getSettings(): Promise<GeneralSettings> {
		const result = await this.request<{ general: GeneralSettings }>(
			"GET",
			"/settings",
		);
		return result.general;
	}

	async updateSettings(data: UpdateSettingsRequest): Promise<GeneralSettings> {
		const result = await this.request<{ general: GeneralSettings }>(
			"PATCH",
			"/settings",
			data,
		);
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

	// Dnsmasq Control API
	async restartDnsmasq(): Promise<ServiceControlResponse> {
		return this.request<ServiceControlResponse>("POST", "/dnsmasq", {});
	}

	// Health Check API
	async checkHealth(): Promise<HealthCheckResponse> {
		return this.request<HealthCheckResponse>("GET", "/health");
	}
}

// Export a default instance
export const apiClient = new KeenPBRClient();
