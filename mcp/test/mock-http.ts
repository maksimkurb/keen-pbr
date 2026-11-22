import type { FetchLike } from "../src/client.js";

export interface RecordedRequest {
  method: string;
  path: string;
  url: string;
  headers: Record<string, string>;
  body?: unknown;
}

export type MockHandler = (request: RecordedRequest) => Response | Promise<Response>;

export interface MockRoute {
  method: string;
  path: string;
  handler: MockHandler;
}

export class MockHttp {
  readonly requests: RecordedRequest[] = [];
  readonly routes: MockRoute[] = [];
  readonly fetch: FetchLike;

  constructor() {
    this.fetch = async (input, init) => {
      const url = typeof input === "string" ? input : input instanceof URL ? input.toString() : input.url;
      const parsed = new URL(url);
      const method = (init?.method ?? "GET").toUpperCase();
      const headers = Object.fromEntries(new Headers(init?.headers).entries());
      let body: unknown;
      if (typeof init?.body === "string") {
        try {
          body = JSON.parse(init.body);
        } catch {
          body = init.body;
        }
      }
      const request: RecordedRequest = { method, path: parsed.pathname, url, headers, ...(body === undefined ? {} : { body }) };
      this.requests.push(request);

      const route = this.routes.find((candidate) => candidate.method === method && candidate.path === parsed.pathname);
      if (!route) {
        return json({ error: `Unexpected request: ${method} ${parsed.pathname}` }, 500);
      }
      return route.handler(request);
    };
  }

  on(method: string, path: string, handler: MockHandler): this {
    this.routes.push({ method: method.toUpperCase(), path, handler });
    return this;
  }

  onJson(method: string, path: string, status: number, body: unknown): this {
    return this.on(method, path, () => json(body, status));
  }

  onText(method: string, path: string, status: number, text: string, contentType = "text/plain"): this {
    return this.on(
      method,
      path,
      () => new Response(text, { status, headers: { "Content-Type": contentType } }),
    );
  }

  requestsFor(method: string, path: string): RecordedRequest[] {
    return this.requests.filter((request) => request.method === method.toUpperCase() && request.path === path);
  }
}

export function json(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}
