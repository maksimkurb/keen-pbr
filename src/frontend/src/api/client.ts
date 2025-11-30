/**
 * TypeScript client for keen-pbr REST API
 * All types are generated from Go code - see generated-types.ts
 */

// Import types used internally in this file
import type {
  // Error types
  APIError,
  // Response wrappers
  DataResponse,
  ErrorResponse,
  GeneralConfig,
  // Interface types
  InterfaceInfo,
  InterfacesResponse,
  // IPSet/Config types
  IPSetConfig,
  IPSetsResponse,
  ListDownloadResponse,
  // List types
  ListInfo,
  ListSource,
  ListsResponse,
  // Network check types
  RoutingCheckResponse,
  ServiceControlResponse,
  // Settings types
  SettingsResponse,
  // Status types
  StatusResponse,
  ValidationErrorDetail,
} from './generated-types';

// Re-export all commonly used types
// Re-export additional useful types
export type {
  APIError,
  AutoUpdateConfig,
  CheckResult,
  DataResponse,
  DNSServerConfig,
  ErrorCode,
  ErrorResponse,
  GeneralConfig,
  HealthCheckResponse,
  HostnameRuleMatch,
  InterfaceInfo,
  IPSetCheckResult,
  IPSetConfig,
  IPTablesRule,
  ListDownloadResponse,
  ListInfo,
  ListSource,
  ListStatistics,
  RoutingCheckResponse,
  RoutingConfig,
  RoutingDNSConfig,
  RuleCheckResult,
  ServiceControlResponse,
  ServiceInfo,
  StatusResponse as StatusInfo,
  ValidationErrorDetail,
  VersionInfo,
} from './generated-types';

// Re-export enums
export { IPFamily } from './generated-types';

// Custom error class that preserves API error details
export class KeenPBRAPIError extends Error {
  public readonly apiError: APIError;
  public readonly statusCode: number;

  constructor(apiError: APIError, statusCode: number) {
    super(apiError.message);
    this.name = 'KeenPBRAPIError';
    this.apiError = apiError;
    this.statusCode = statusCode;
  }

  // Helper to get validation errors if they exist
  getValidationErrors(): ValidationErrorDetail[] | null {
    if (
      this.apiError.code === 'validation_failed' &&
      this.apiError.fieldErrors
    ) {
      return this.apiError.fieldErrors;
    }
    return null;
  }
}

// API Client class
export class KeenPBRClient {
  private baseURL: string;

  constructor(baseURL = '') {
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
      headers['Content-Type'] = 'application/json';
    }

    const response = await fetch(url, {
      method,
      headers,
      body: body ? JSON.stringify(body) : undefined,
    });

    if (!response.ok) {
      const error: ErrorResponse = await response.json();
      throw new KeenPBRAPIError(error.error, response.status);
    }

    const result: DataResponse = await response.json();
    return result.data as T;
  }

  // Lists API
  async getLists(): Promise<ListInfo[]> {
    const result = await this.request<ListsResponse>('GET', '/lists');
    return result.lists.filter((list): list is ListInfo => list !== null);
  }

  async getList(name: string): Promise<ListInfo> {
    return this.request<ListInfo>('GET', `/lists/${encodeURIComponent(name)}`);
  }

  async createList(data: ListSource): Promise<ListInfo> {
    return this.request<ListInfo>('POST', '/lists', data);
  }

  async updateList(
    name: string,
    data: Partial<Omit<ListSource, 'list_name'>>,
  ): Promise<ListInfo> {
    return this.request<ListInfo>(
      'PUT',
      `/lists/${encodeURIComponent(name)}`,
      data,
    );
  }

  async deleteList(name: string): Promise<void> {
    await this.request<void>('DELETE', `/lists/${encodeURIComponent(name)}`);
  }

  async downloadList(name: string): Promise<ListDownloadResponse> {
    return this.request<ListDownloadResponse>(
      'POST',
      `/lists-download/${encodeURIComponent(name)}`,
    );
  }

  async downloadAllLists(): Promise<{ message: string }> {
    return this.request<{ message: string }>('POST', '/lists-download');
  }

  // IPSets API
  async getIPSets(): Promise<IPSetConfig[]> {
    const result = await this.request<IPSetsResponse>('GET', '/ipsets');
    return result.ipsets.filter(
      (ipset): ipset is IPSetConfig => ipset !== null,
    );
  }

  async getIPSet(name: string): Promise<IPSetConfig> {
    return this.request<IPSetConfig>(
      'GET',
      `/ipsets/${encodeURIComponent(name)}`,
    );
  }

  async createIPSet(data: IPSetConfig): Promise<IPSetConfig> {
    return this.request<IPSetConfig>('POST', '/ipsets', data);
  }

  async updateIPSet(
    name: string,
    data: Partial<Omit<IPSetConfig, 'ipset_name'>>,
  ): Promise<IPSetConfig> {
    return this.request<IPSetConfig>(
      'PUT',
      `/ipsets/${encodeURIComponent(name)}`,
      data,
    );
  }

  async deleteIPSet(name: string): Promise<void> {
    await this.request<void>('DELETE', `/ipsets/${encodeURIComponent(name)}`);
  }

  // Interfaces API
  async getInterfaces(): Promise<InterfaceInfo[]> {
    const result = await this.request<InterfacesResponse>('GET', '/interfaces');
    return result.interfaces;
  }

  // Settings API
  async getSettings(): Promise<GeneralConfig> {
    const result = await this.request<SettingsResponse>('GET', '/settings');
    if (!result.general) {
      throw new Error('Settings not found in response');
    }
    return result.general;
  }

  async updateSettings(data: Partial<GeneralConfig>): Promise<GeneralConfig> {
    const result = await this.request<SettingsResponse>(
      'PATCH',
      '/settings',
      data,
    );
    if (!result.general) {
      throw new Error('Settings not found in response');
    }
    return result.general;
  }

  // Status API
  async getStatus(): Promise<StatusResponse> {
    return this.request<StatusResponse>('GET', '/status');
  }

  // Service Control API
  async controlService(
    state: 'started' | 'stopped' | 'restarted',
  ): Promise<ServiceControlResponse> {
    return this.request<ServiceControlResponse>('POST', '/service', { state });
  }

  // Network Check API
  async checkRouting(host: string): Promise<RoutingCheckResponse> {
    return this.request<RoutingCheckResponse>('POST', '/check/routing', {
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
