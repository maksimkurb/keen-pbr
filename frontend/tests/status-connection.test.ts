import { beforeEach, describe, expect, test } from "bun:test"
import { QueryClient } from "@tanstack/react-query"

import {
  getGetHealthIssuesQueryKey,
  getGetHealthServiceQueryKey,
} from "../src/api/generated/keen-api"
import {
  createStatusStreamQueryFn,
  markStatusQueriesStale,
  setStatusConnectionState,
  waitForStatusSnapshot,
} from "../src/api/status-connection"

function context(client: QueryClient) {
  return {
    client,
    queryKey: getGetHealthServiceQueryKey(),
    signal: new AbortController().signal,
    meta: undefined,
  } as never
}

describe("status stream query function", () => {
  beforeEach(() => setStatusConnectionState("connecting"))

  test("waits for the snapshot and returns the cached value", async () => {
    const client = new QueryClient()
    let fetched = 0
    const queryFn = createStatusStreamQueryFn(async () => {
      fetched += 1
      return { rest: true }
    })
    const pending = queryFn(context(client))
    client.setQueryData(getGetHealthServiceQueryKey(), { cached: true })
    setStatusConnectionState("live")
    expect(await pending).toEqual({ cached: true })
    expect(fetched).toBe(0)
  })

  test("the issues query is served from the stream cache as well", async () => {
    const client = new QueryClient()
    client.setQueryData(getGetHealthIssuesQueryKey(), { issues: true })
    setStatusConnectionState("live")
    let fetched = 0
    const queryFn = createStatusStreamQueryFn(async () => {
      fetched += 1
      return { rest: true }
    })
    expect(
      await queryFn({
        client,
        queryKey: getGetHealthIssuesQueryKey(),
        signal: new AbortController().signal,
        meta: undefined,
      } as never)
    ).toEqual({ issues: true })
    expect(fetched).toBe(0)
  })

  test("falls back to REST when the stream is down", async () => {
    const client = new QueryClient()
    setStatusConnectionState("down")
    const queryFn = createStatusStreamQueryFn(async () => ({ rest: true }))
    expect(await queryFn(context(client))).toEqual({ rest: true })
  })

  test("falls back to REST when the snapshot does not arrive in time", async () => {
    const client = new QueryClient()
    const queryFn = createStatusStreamQueryFn(async () => ({ rest: true }), {
      waitForSnapshot: () => waitForStatusSnapshot(10),
    })
    expect(await queryFn(context(client))).toEqual({ rest: true })
  })

  test("falls back to REST when live but nothing is cached", async () => {
    const client = new QueryClient()
    setStatusConnectionState("live")
    const queryFn = createStatusStreamQueryFn(async () => ({ rest: true }))
    expect(await queryFn(context(client))).toEqual({ rest: true })
  })

  test("a disconnect during the wait resolves immediately", async () => {
    const pending = waitForStatusSnapshot(5_000)
    setStatusConnectionState("down")
    expect(await pending).toBe(false)
  })

  test("marking stale keeps the data and does not refetch", async () => {
    const client = new QueryClient()
    client.setQueryData(getGetHealthServiceQueryKey(), { cached: true })
    markStatusQueriesStale(client, [getGetHealthServiceQueryKey()])
    const state = client.getQueryState(getGetHealthServiceQueryKey())
    expect(state?.isInvalidated).toBe(true)
    expect(client.getQueryData(getGetHealthServiceQueryKey())).toEqual({
      cached: true,
    })
  })
})
