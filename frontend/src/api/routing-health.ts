import { useEffect, useRef } from "react"

import type { UseQueryOptions, UseQueryResult } from "@tanstack/react-query"

import { useGetHealthRouting } from "@/api/queries"
import type { getHealthRoutingResponse } from "@/api/generated/keen-api"

export const ROUTING_PENDING_RETRY_MS = 1_500
export const ROUTING_PENDING_MAX_RETRIES = 10

/**
 * The daemon reports `unavailable` while the routing runtime is being applied
 * or the canonical report is being refreshed; the answer settles in seconds.
 */
export function isRoutingHealthPending(
  response: getHealthRoutingResponse | undefined
): boolean {
  return (
    response?.status === 200 &&
    response.data.firewall.verification_state === "unavailable"
  )
}

/** Delay before the next quick refetch, or null when none is due. */
export function getRoutingHealthRetryDelay(
  pending: boolean,
  attempts: number
): number | null {
  return pending && attempts < ROUTING_PENDING_MAX_RETRIES
    ? ROUTING_PENDING_RETRY_MS
    : null
}

/** Routing health query that re-reads quickly while the answer is pending. */
export function useRoutingHealthQuery(options?: {
  query?: Partial<
    UseQueryOptions<getHealthRoutingResponse, unknown, getHealthRoutingResponse>
  >
}): UseQueryResult<getHealthRoutingResponse, unknown> {
  const query = useGetHealthRouting(options)
  const { refetch, data, dataUpdatedAt } = query
  const attemptsRef = useRef(0)

  useEffect(() => {
    const pending = isRoutingHealthPending(data)
    if (!pending) attemptsRef.current = 0
    const delay = getRoutingHealthRetryDelay(pending, attemptsRef.current)
    if (delay === null) return
    const timer = setTimeout(() => {
      attemptsRef.current += 1
      void refetch()
    }, delay)
    return () => clearTimeout(timer)
    // dataUpdatedAt re-arms the timer after every completed response.
  }, [data, dataUpdatedAt, refetch])

  return query
}
