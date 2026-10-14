import { describe, expect, test } from "bun:test"

import {
  getRoutingHealthRetryDelay,
  isRoutingHealthPending,
  ROUTING_PENDING_MAX_RETRIES,
  ROUTING_PENDING_RETRY_MS,
} from "../src/api/routing-health"

const response = (state: string, status = 200) =>
  ({ status, data: { firewall: { verification_state: state } } }) as never

describe("routing health pending retry", () => {
  test("only an unavailable firewall chain is pending", () => {
    expect(isRoutingHealthPending(response("unavailable"))).toBe(true)
    expect(isRoutingHealthPending(response("verified"))).toBe(false)
    expect(isRoutingHealthPending(response("failed"))).toBe(false)
    expect(isRoutingHealthPending(response("unavailable", 503))).toBe(false)
    expect(isRoutingHealthPending(undefined)).toBe(false)
  })

  test("retries quickly and gives up after the cap", () => {
    expect(getRoutingHealthRetryDelay(true, 0)).toBe(ROUTING_PENDING_RETRY_MS)
    expect(
      getRoutingHealthRetryDelay(true, ROUTING_PENDING_MAX_RETRIES - 1)
    ).toBe(ROUTING_PENDING_RETRY_MS)
    expect(
      getRoutingHealthRetryDelay(true, ROUTING_PENDING_MAX_RETRIES)
    ).toBeNull()
    expect(getRoutingHealthRetryDelay(false, 0)).toBeNull()
  })
})
