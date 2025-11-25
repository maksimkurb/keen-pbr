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
	hosts?: string[]; // Only populated in GET single list endpoint for inline hosts
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

export interface ListDownloadResponse extends ListInfo {
	changed: boolean; // true if the list was updated, false if unchanged
}

// IPSet types
export interface RoutingConfig {
	interfaces: string[];
	default_gateway?: string;
	kill_switch?: boolean;
	fwmark: number;
	table: number;
	priority: number;
	override_dns?: string;
}

export interface IPTablesRule {
	chain: string;
	table: string;
	rule: string[];
}

export interface IPSetConfig {
	ipset_name: string;
	lists: string[];
	ip_version: 4 | 6;
	flush_before_applying: boolean;
	routing?: RoutingConfig;
	iptables_rule?: IPTablesRule[];
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

// Interface types
export interface InterfaceInfo {
	name: string;
	is_up: boolean;
}

// Settings types
export interface AutoUpdateConfig {
	enabled: boolean;
	interval_hours: number;
}

export interface DNSServerConfig {
	enable: boolean;
	listen_addr: string;
	listen_port: number;
	upstreams: string[];
	cache_max_domains: number;
	drop_aaaa: boolean;
	ttl_override: number;
	remap_53_interfaces: string[];
}

export interface GeneralSettings {
	lists_output_dir: string;
	interface_monitoring_interval_seconds: number; // 0 = disabled
	auto_update_lists: AutoUpdateConfig;
	dns_server: DNSServerConfig;
}

export interface UpdateSettingsRequest {
	lists_output_dir?: string;
	interface_monitoring_interval_seconds?: number;
	auto_update_lists?: AutoUpdateConfig;
	dns_server?: DNSServerConfig;
}

// Status types
export interface ServiceInfo {
	status: "running" | "stopped" | "unknown";
	message?: string;
	config_hash?: string; // Hash of applied config (keen-pbr service only)
}

export interface VersionInfo {
	version: string;
	date: string;
	commit: string;
}

export interface DNSServerInfo {
	type: string; // "IP4", "IP6", "DoT", "DoH"
	endpoint: string; // IP for plain DNS, SNI for DoT, URI for DoH
	port?: string; // Port for DoT/DoH
	domain?: string; // Domain scope (if any)
}

export interface StatusInfo {
	version: VersionInfo;
	keenetic_version?: string;
	services: Record<string, ServiceInfo>;
	current_config_hash: string;
	configuration_outdated: boolean;
	dns_servers?: string[]; // Upstream DNS servers
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

// Network check types
export interface HostnameRuleMatch {
	rule_name: string;
	pattern: string;
}

export interface RuleCheckResult {
	rule_name: string;
	present_in_ipset: boolean;
	should_be_present: boolean;
	match_reason?: string;
}

export interface IPSetCheckResult {
	ip: string;
	rule_results: RuleCheckResult[];
}

export interface RoutingCheckResponse {
	host: string;
	resolved_ips?: string[];
	matched_by_hostname?: HostnameRuleMatch[];
	ipset_checks: IPSetCheckResult[];
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

	// Interfaces API
	async getInterfaces(): Promise<InterfaceInfo[]> {
		const result = await this.request<{ interfaces: InterfaceInfo[] }>(
			"GET",
			"/interfaces",
		);
		return result.interfaces;
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
