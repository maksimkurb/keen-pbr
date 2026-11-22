import type { ServerConfig } from "./config.js";
import { KeenPbrApiError } from "./errors.js";
import type {
  ConfigStateResponse,
  ConfigUpdateResponse,
  DnsCheckRequest,
  DnsCheckResponse,
  DnsStreamEvent,
  HealthIssuesResponse,
  HealthResponse,
  LifecycleOperationAcceptedResponse,
  ListRefreshResponse,
  RoutingHealthResponse,
  RoutingTestRequest,
  RoutingTestResponse,
  RuntimeInterfaceInventoryResponse,
  RuntimeOutboundsResponse,
  DraftConfig,
} from "./types.js";

export type FetchLike = (input: string | URL | Request, init?: RequestInit) => Promise<Response>;

interface RequestOptions {
  body?: unknown;
  timeoutMs?: number;
  accept?: string;
}

/**
 * Minimal typed client for the keen-pbr REST API.
 *
 * Authentication is always a bearer token. The token is kept in memory only and
 * is never included in errors, logs or tool output.
 */
export class KeenPbrClient {
  private readonly baseUrl: string;
  private readonly token: string;
  private readonly timeoutMs: number;
  private readonly fetchImpl: FetchLike;

  constructor(config: Pick<ServerConfig, "baseUrl" | "token" | "timeoutMs">, fetchImpl: FetchLike = fetch) {
    this.baseUrl = config.baseUrl;
    this.token = config.token;
    this.timeoutMs = config.timeoutMs;
    this.fetchImpl = fetchImpl;
  }

  async getServiceHealth(): Promise<HealthResponse> {
    return this.getJson<HealthResponse>("/api/health/service");
  }

  async getRoutingHealth(): Promise<RoutingHealthResponse> {
    return this.getJson<RoutingHealthResponse>("/api/health/routing");
  }

  async getHealthIssues(): Promise<HealthIssuesResponse> {
    return this.getJson<HealthIssuesResponse>("/api/health/issues");
  }

  async getRuntimeOutbounds(): Promise<RuntimeOutboundsResponse> {
    return this.getJson<RuntimeOutboundsResponse>("/api/runtime/outbounds");
  }

  async getRuntimeInterfaces(): Promise<RuntimeInterfaceInventoryResponse> {
    return this.getJson<RuntimeInterfaceInventoryResponse>("/api/runtime/interfaces");
  }

  async getConfig(): Promise<ConfigStateResponse> {
    return this.getJson<ConfigStateResponse>("/api/config");
  }

  async stageConfig(config: DraftConfig): Promise<ConfigUpdateResponse> {
    return this.postJson<ConfigUpdateResponse>("/api/config", config);
  }

  async saveConfig(): Promise<LifecycleOperationAcceptedResponse> {
    return this.postJson<LifecycleOperationAcceptedResponse>("/api/config/save");
  }

  async discardConfig(): Promise<ConfigUpdateResponse> {
    return this.postJson<ConfigUpdateResponse>("/api/config/discard");
  }

  async rollbackConfig(): Promise<LifecycleOperationAcceptedResponse> {
    return this.postJson<LifecycleOperationAcceptedResponse>("/api/config/rollback");
  }

  async refreshLists(name?: string): Promise<ListRefreshResponse> {
    return this.postJson<ListRefreshResponse>(
      "/api/lists/refresh",
      name === undefined ? {} : { name },
    );
  }

  async routingTest(request: RoutingTestRequest): Promise<RoutingTestResponse> {
    return this.postJson<RoutingTestResponse>("/api/routing/test", request);
  }

  async dnsCheckServers(request: DnsCheckRequest = {}): Promise<DnsCheckResponse> {
    return this.postJson<DnsCheckResponse>("/api/dns/servers/check", request);
  }

  async getMetrics(): Promise<string> {
    return this.getText("/metrics", 5_000);
  }

  /** Returns null when the daemon has no command-failure log (HTTP 204). */
  async getCommandFailure(): Promise<string | null> {
    const response = await this.fetchChecked("/api/diagnostics/command-failure", {
      method: "GET",
      timeoutMs: this.timeoutMs,
      accept: "text/plain",
    });
    if (response.status === 204) return null;
    return response.text();
  }

  /**
   * Collects events from the `/api/dns/test` SSE stream for at most
   * `durationMs`. The stream is intentionally finite: MCP tools must return.
   */
  async collectDnsEvents(options: {
    durationMs: number;
    maxEvents: number;
    show?: "keen-pbr" | "all" | "full";
    domain?: string;
  }): Promise<{ events: DnsStreamEvent[]; truncated: boolean }> {
    const query = new URLSearchParams();
    if (options.show) query.set("show", options.show);
    if (options.domain) query.set("domain", options.domain);
    const path = query.size === 0 ? "/api/dns/test" : `/api/dns/test?${query.toString()}`;

    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), options.durationMs);
    timer.unref?.();

    const events: DnsStreamEvent[] = [];
    let truncated = false;

    try {
      const response = await this.fetchImpl(this.url(path), {
        method: "GET",
        headers: this.headers("text/event-stream"),
        signal: controller.signal,
      });
      if (!response.ok) await this.throwApiError(response);
      if (!response.body) throw new KeenPbrApiError("The DNS test endpoint returned no stream");

      const reader = response.body.getReader();
      const decoder = new TextDecoder();
      let buffer = "";

      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        buffer += decoder.decode(value, { stream: true });

        for (;;) {
          const next = takeSseBlock(buffer);
          if (!next) break;
          buffer = next.rest;
          const parsed = parseSseBlock(next.block);
          if (parsed) {
            events.push(parsed);
            if (events.length >= options.maxEvents) {
              truncated = true;
              await reader.cancel().catch(() => undefined);
              break;
            }
          }
        }
        if (truncated) break;
      }
    } catch (error) {
      if (!controller.signal.aborted) {
        throw this.wrapNetworkError(error, path);
      }
    } finally {
      clearTimeout(timer);
    }

    return { events, truncated };
  }

  /** Wait for an accepted lifecycle operation to finish. */
  async waitForLifecycle(
    operationId: string,
    timeoutMs: number,
  ): Promise<{ done: boolean; health?: HealthResponse }> {
    const deadline = Date.now() + timeoutMs;
    let delay = 250;
    while (true) {
      let health: HealthResponse | undefined;
      try {
        health = await this.getServiceHealth();
      } catch {
        health = undefined;
      }
      if (health) {
        const operation = health.lifecycle_operation;
        if (!operation || operation.id !== operationId || operation.status !== "running") {
          return { done: true, health };
        }
      }
      if (Date.now() + delay > deadline) {
        return { done: false, ...(health ? { health } : {}) };
      }
      await sleep(delay);
      delay = Math.min(delay * 2, 1_000);
    }
  }

  private async getJson<T>(path: string): Promise<T> {
    const response = await this.fetchChecked(path, {
      method: "GET",
      timeoutMs: this.timeoutMs,
      accept: "application/json",
    });
    return (await response.json()) as T;
  }

  private async postJson<T>(path: string, body?: unknown): Promise<T> {
    const response = await this.fetchChecked(path, {
      method: "POST",
      timeoutMs: this.timeoutMs,
      accept: "application/json",
      ...(body === undefined ? {} : { body }),
    });
    if (response.status === 204) return {} as T;
    return (await response.json()) as T;
  }

  private async getText(path: string, timeoutMs: number): Promise<string> {
    const response = await this.fetchChecked(path, {
      method: "GET",
      timeoutMs,
      accept: "text/plain",
    });
    return response.text();
  }

  private async fetchChecked(path: string, options: RequestOptions & { method: string }): Promise<Response> {
    let response: Response;
    try {
      response = await this.fetchImpl(this.url(path), {
        method: options.method,
        headers: this.headers(options.accept, options.body !== undefined),
        ...(options.body === undefined ? {} : { body: JSON.stringify(options.body) }),
        signal: AbortSignal.timeout(options.timeoutMs ?? this.timeoutMs),
      });
    } catch (error) {
      throw this.wrapNetworkError(error, path);
    }
    if (!response.ok) await this.throwApiError(response);
    return response;
  }

  private url(path: string): string {
    return `${this.baseUrl}${path}`;
  }

  private headers(accept?: string, hasBody = false): Record<string, string> {
    return {
      Authorization: `Bearer ${this.token}`,
      ...(accept ? { Accept: accept } : {}),
      ...(hasBody ? { "Content-Type": "application/json" } : {}),
    };
  }

  private wrapNetworkError(error: unknown, path: string): KeenPbrApiError {
    if (error instanceof KeenPbrApiError) return error;
    const cause = error instanceof Error ? error.message : String(error);
    return new KeenPbrApiError(
      `Cannot reach keen-pbr at ${this.baseUrl} (${path}): ${cause}. Check KEEN_PBR_URL, that the API is enabled and that KEEN_PBR_TOKEN is valid.`,
      { cause: error },
    );
  }

  private async throwApiError(response: Response): Promise<never> {
    let message = "";
    let validationErrors: Array<{ path?: string; message: string }> = [];
    let raw = "";
    try {
      raw = await response.text();
    } catch {
      raw = "";
    }
    if (raw) {
      try {
        const parsed = JSON.parse(raw) as {
          error?: unknown;
          validation_errors?: Array<{ path?: unknown; message?: unknown }>;
        };
        if (typeof parsed.error === "string") message = parsed.error;
        if (Array.isArray(parsed.validation_errors)) {
          validationErrors = parsed.validation_errors
            .filter((issue) => issue !== null && typeof issue === "object")
            .map((issue) => ({
              ...(typeof issue.path === "string" ? { path: issue.path } : {}),
              message: typeof issue.message === "string" ? issue.message : String(issue.message),
            }));
        }
      } catch {
        message = raw.slice(0, 500);
      }
    }
    if (!message) message = `HTTP ${response.status} ${response.statusText}`.trim();
    if (response.status === 401 || response.status === 403) {
      message += ". Check that KEEN_PBR_TOKEN is valid and has the required scope.";
    }
    if (response.status === 409) {
      message +=
        ". A config draft or another lifecycle operation is in progress; inspect get_config and get_health.";
    }
    throw new KeenPbrApiError(message, { status: response.status, validationErrors });
  }
}

function takeSseBlock(buffer: string): { block: string; rest: string } | null {
  const match = /\r?\n\r?\n/.exec(buffer);
  if (!match) return null;
  return {
    block: buffer.slice(0, match.index),
    rest: buffer.slice(match.index + match[0].length),
  };
}

function parseSseBlock(block: string): DnsStreamEvent | null {
  const dataLines: string[] = [];
  let event: string | undefined;
  for (const line of block.split("\n")) {
    if (line.startsWith(":")) continue;
    if (line.startsWith("event:")) {
      event = line.slice("event:".length).trim();
      continue;
    }
    if (line.startsWith("data:")) {
      dataLines.push(line.slice("data:".length).trimStart());
    }
  }
  if (dataLines.length === 0) return null;
  const raw = dataLines.join("\n");
  let data: unknown = raw;
  try {
    data = JSON.parse(raw);
  } catch {
    // Keep non-JSON payloads as text.
  }
  return { ...(event ? { event } : {}), data };
}

function sleep(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}
