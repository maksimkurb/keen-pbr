import { afterEach, describe, expect, test } from "bun:test"

import { authenticatedFetch } from "../src/api/client"

const originalFetch = globalThis.fetch
const originalSessionStorage = globalThis.sessionStorage

afterEach(() => {
  Object.defineProperty(globalThis, "fetch", {
    configurable: true,
    value: originalFetch,
  })
  Object.defineProperty(globalThis, "sessionStorage", {
    configurable: true,
    value: originalSessionStorage,
  })
})

describe("authenticatedFetch", () => {
  test("adds the active UI Bearer token while preserving caller headers", async () => {
    const values = new Map([["keen-pbr-auth-token", "test-token"]])
    Object.defineProperty(globalThis, "sessionStorage", {
      configurable: true,
      value: {
        getItem: (key: string) => values.get(key) ?? null,
        setItem: (key: string, value: string) => values.set(key, value),
        removeItem: (key: string) => values.delete(key),
      },
    })

    let receivedHeaders: Headers | undefined
    Object.defineProperty(globalThis, "fetch", {
      configurable: true,
      value: (_url: string, options?: RequestInit) => {
        receivedHeaders = new Headers(options?.headers)
        return Promise.resolve(new Response(null, { status: 204 }))
      },
    })

    const response = await authenticatedFetch(
      "/api/diagnostics/command-failure",
      {
        headers: { Accept: "text/plain" },
      }
    )

    expect(response.status).toBe(204)
    expect(receivedHeaders?.get("Authorization")).toBe("Bearer test-token")
    expect(receivedHeaders?.get("Accept")).toBe("text/plain")
  })
})
