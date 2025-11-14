import type { Config, NetworkInterface, Outbound, Rule, ServiceStatusResponse } from '../types';

// API base URL - empty string means same origin (embedded UI case)
const API_BASE_URL = import.meta.env?.VITE_API_BASE_URL || '';

class APIError extends Error {
  constructor(public status: number, message: string) {
    super(message);
    this.name = 'APIError';
  }
}

async function fetchJSON<T>(url: string, options?: RequestInit): Promise<T> {
  const response = await fetch(`${API_BASE_URL}${url}`, {
    ...options,
    headers: {
      'Content-Type': 'application/json',
      ...options?.headers,
    },
  });

  if (!response.ok) {
    const error = await response.json().catch(() => ({ error: 'Unknown error' }));
    throw new APIError(response.status, error.error || 'Request failed');
  }

  return response.json();
}

// Service API
export const serviceAPI = {
  async start() {
    return fetchJSON<{ status: string }>('/v1/service/start', { method: 'POST' });
  },

  async stop() {
    return fetchJSON<{ status: string }>('/v1/service/stop', { method: 'POST' });
  },

  async restart() {
    return fetchJSON<{ status: string }>('/v1/service/restart', { method: 'POST' });
  },

  async enable() {
    return fetchJSON<{ status: string }>('/v1/service/enable', { method: 'POST' });
  },

  async disable() {
    return fetchJSON<{ status: string }>('/v1/service/disable', { method: 'POST' });
  },

  async getStatus() {
    return fetchJSON<ServiceStatusResponse>('/v1/service/status');
  },
};

// Rules API
export const rulesAPI = {
  async getAll() {
    return fetchJSON<Record<string, Rule>>('/v1/rules');
  },

  async getOne(id: string) {
    return fetchJSON<Rule>(`/v1/rules/${id}`);
  },

  async bulkUpdate(rules: Rule[]) {
    return fetchJSON<{ status: string }>('/v1/rules', {
      method: 'PUT',
      body: JSON.stringify(rules),
    });
  },
};

// Outbounds API
export const outboundsAPI = {
  async getAll() {
    return fetchJSON<Record<string, Outbound>>('/v1/outbounds');
  },

  async getOne(tag: string) {
    return fetchJSON<Outbound>(`/v1/outbounds/${tag}`);
  },

  async bulkUpdate(outbounds: Outbound[]) {
    return fetchJSON<{ status: string }>('/v1/outbounds', {
      method: 'PUT',
      body: JSON.stringify(outbounds),
    });
  },
};

// Info API
export const infoAPI = {
  async getInterfaces() {
    return fetchJSON<NetworkInterface[]>('/v1/info/interfaces');
  },
};

// Config API
export const configAPI = {
  async get() {
    return fetchJSON<Config>('/v1/config');
  },

  async update(config: Config) {
    return fetchJSON<Config>('/v1/config', {
      method: 'PUT',
      body: JSON.stringify(config),
    });
  },
};
