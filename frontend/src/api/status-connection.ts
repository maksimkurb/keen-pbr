import type { QueryClient, QueryFunctionContext } from "@tanstack/react-query"

// connecting: the stream is opening, no snapshot received yet
// live: the snapshot of the current connection has been applied to the cache
// down: no connection (closed, failed, or waiting to reconnect)
export type StatusConnectionState = "connecting" | "live" | "down"

export const SNAPSHOT_WAIT_TIMEOUT_MS = 2_000

let state: StatusConnectionState = "connecting"
const listeners = new Set<() => void>()

export function getStatusConnectionState(): StatusConnectionState {
  return state
}

export function subscribeStatusConnection(listener: () => void) {
  listeners.add(listener)
  return () => {
    listeners.delete(listener)
  }
}

export function setStatusConnectionState(next: StatusConnectionState) {
  if (state === next) return
  state = next
  for (const listener of [...listeners]) listener()
}

/**
 * Resolves true once the current connection has delivered its snapshot, false
 * when the stream is down or the snapshot does not arrive within `timeoutMs`.
 * Never rejects and never hangs.
 */
export function waitForStatusSnapshot(
  timeoutMs = SNAPSHOT_WAIT_TIMEOUT_MS
): Promise<boolean> {
  if (state === "live") return Promise.resolve(true)
  if (state === "down") return Promise.resolve(false)
  return new Promise((resolve) => {
    let unsubscribe = () => {}
    const finish = (value: boolean) => {
      clearTimeout(timer)
      unsubscribe()
      resolve(value)
    }
    const timer = setTimeout(() => finish(false), timeoutMs)
    unsubscribe = subscribeStatusConnection(() => {
      if (state === "live") finish(true)
      else if (state === "down") finish(false)
    })
  })
}

type StatusQueryFnDeps = {
  waitForSnapshot?: (timeoutMs?: number) => Promise<boolean>
}

/**
 * Query function for data owned by the status stream: serves the cache the
 * stream fills, and only falls back to the REST fetcher when the stream is
 * down or does not deliver in time.
 */
export function createStatusStreamQueryFn<T>(
  fetcher: (signal: AbortSignal) => Promise<T>,
  deps: StatusQueryFnDeps = {}
) {
  const wait = deps.waitForSnapshot ?? waitForStatusSnapshot
  return async (context: QueryFunctionContext): Promise<T> => {
    if (await wait()) {
      const cached = context.client.getQueryData<T>(context.queryKey)
      if (cached !== undefined) return cached
    }
    return fetcher(context.signal)
  }
}

/** Marks stream-owned queries stale without refetching them. */
export function markStatusQueriesStale(
  queryClient: QueryClient,
  queryKeys: readonly (readonly unknown[])[]
) {
  for (const queryKey of queryKeys) {
    void queryClient.invalidateQueries({ queryKey, refetchType: "none" })
  }
}
