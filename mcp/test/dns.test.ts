import { describe, expect, it } from "vitest";
import { KeenPbrClient } from "../src/client.js";
import { MockHttp } from "./mock-http.js";

function clientFor(mock: MockHttp): KeenPbrClient {
  return new KeenPbrClient(
    { baseUrl: "http://router.test:12121", token: "test-token", timeoutMs: 5_000 },
    mock.fetch,
  );
}

function sseResponse(chunks: string[]): Response {
  const encoder = new TextEncoder();
  return new Response(
    new ReadableStream<Uint8Array>({
      start(controller) {
        for (const chunk of chunks) controller.enqueue(encoder.encode(chunk));
        controller.close();
      },
    }),
    { status: 200, headers: { "Content-Type": "text/event-stream" } },
  );
}

describe("DNS test stream", () => {
  it("parses SSE events with LF and CRLF separators", async () => {
    const mock = new MockHttp().on("GET", "/api/dns/test", () =>
      sseResponse([
        ": heartbeat\n\n",
        'data: {"type":"HELLO"}\n\n',
        'event: INTERCEPT\r\ndata: {"type":"INTERCEPT","seq":1,"domain":"example.com"}\r\n\r\n',
        'data: {"type":"GAP"}\n\n',
      ]),
    );

    const result = await clientFor(mock).collectDnsEvents({ durationMs: 1_000, maxEvents: 10 });
    expect(result.truncated).toBe(false);
    expect(result.events).toEqual([
      { data: { type: "HELLO" } },
      { event: "INTERCEPT", data: { type: "INTERCEPT", seq: 1, domain: "example.com" } },
      { data: { type: "GAP" } },
    ]);
    expect(mock.requests[0]?.url).toBe("http://router.test:12121/api/dns/test");
  });

  it("stops at max_events and forwards query parameters", async () => {
    const mock = new MockHttp().on("GET", "/api/dns/test", () =>
      sseResponse([
        'data: {"type":"A"}\n\n',
        'data: {"type":"B"}\n\n',
        'data: {"type":"C"}\n\n',
      ]),
    );

    const result = await clientFor(mock).collectDnsEvents({
      durationMs: 1_000,
      maxEvents: 2,
      show: "all",
      domain: "check.example",
    });
    expect(result.truncated).toBe(true);
    expect(result.events).toHaveLength(2);
    expect(mock.requests[0]?.url).toBe(
      "http://router.test:12121/api/dns/test?show=all&domain=check.example",
    );
  });
});
