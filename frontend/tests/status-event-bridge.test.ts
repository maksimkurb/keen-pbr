import { describe, expect, test } from "bun:test"
import { QueryClient } from "@tanstack/react-query"

import {
  getGetConfigQueryKey,
  getGetHealthIssuesQueryKey,
  getGetHealthRoutingQueryKey,
  getGetHealthServiceQueryKey,
  getGetRuntimeInterfacesQueryKey,
  getGetRuntimeOutboundsQueryKey,
} from "../src/api/generated/keen-api"
import {
  applyStatusEvent,
  getTerminalConfigLifecycleOperationKey,
  invalidateAfterTerminalConfigOperation,
} from "../src/api/status-event-cache"

const service = (version: string) => ({ version })
const outbounds = (tag: string) => ({ outbounds: [{ tag }] })
const interfaces = (name: string) => ({ interfaces: [{ name }] })

describe("status event cache bridge", () => {
  test("snapshot hydrates and later snapshots resynchronize every dataset", () => {
    const client = new QueryClient()
    expect(
      applyStatusEvent(
        client,
        JSON.stringify({
          type: "snapshot",
          data: {
            service: service("1"),
            outbounds: outbounds("wan"),
            interfaces: interfaces("eth0"),
          },
        })
      )
    ).toBe(true)
    applyStatusEvent(
      client,
      JSON.stringify({
        type: "snapshot",
        data: {
          service: service("2"),
          outbounds: outbounds("vpn"),
          interfaces: interfaces("tun0"),
        },
      })
    )
    expect(client.getQueryData(getGetHealthServiceQueryKey())).toMatchObject({
      data: { version: "2" },
      status: 200,
    })
    expect(client.getQueryData(getGetRuntimeOutboundsQueryKey())).toMatchObject({
      data: { outbounds: [{ tag: "vpn" }] },
    })
    expect(client.getQueryData(getGetRuntimeInterfacesQueryKey())).toMatchObject({
      data: { interfaces: [{ name: "tun0" }] },
    })
  })

  test("named events replace only their own dataset and malformed data is ignored", () => {
    const client = new QueryClient()
    applyStatusEvent(
      client,
      JSON.stringify({
        type: "snapshot",
        data: {
          service: service("1"),
          outbounds: outbounds("wan"),
          interfaces: interfaces("eth0"),
        },
      })
    )
    const interfacesBefore = client.getQueryData(
      getGetRuntimeInterfacesQueryKey()
    )
    expect(
      applyStatusEvent(
        client,
        JSON.stringify({ type: "outbounds", data: outbounds("vpn") })
      )
    ).toBe(true)
    expect(client.getQueryData(getGetRuntimeInterfacesQueryKey())).toBe(
      interfacesBefore
    )
    expect(applyStatusEvent(client, "not json")).toBe(false)
    expect(client.getQueryData(getGetHealthServiceQueryKey())).toMatchObject({
      data: { version: "1" },
    })
  })

  test("only terminal config lifecycle events request a config resync", () => {
    expect(
      getTerminalConfigLifecycleOperationKey(
        JSON.stringify({
          type: "service",
          data: {
            lifecycle_operation: {
              id: "apply-1",
              type: "apply_config",
              status: "running",
            },
          },
        })
      )
    ).toBeNull()

    expect(
      getTerminalConfigLifecycleOperationKey(
        JSON.stringify({
          type: "service",
          data: {
            lifecycle_operation: {
              id: "start-1",
              type: "start",
              status: "succeeded",
            },
          },
        })
      )
    ).toBeNull()

    expect(
      getTerminalConfigLifecycleOperationKey(
        JSON.stringify({
          type: "service",
          data: {
            lifecycle_operation: {
              id: "apply-1",
              type: "apply_config",
              status: "succeeded",
            },
          },
        })
      )
    ).toBe("apply-1:succeeded")

    expect(
      getTerminalConfigLifecycleOperationKey(
        JSON.stringify({
          type: "snapshot",
          data: {
            service: {
              lifecycle_operation: {
                id: "rollback-1",
                type: "rollback_config",
                status: "failed",
              },
            },
            outbounds: {},
            interfaces: {},
          },
        })
      )
    ).toBe("rollback-1:failed")
  })

  test("a terminal apply invalidates config and routing health", () => {
    const client = new QueryClient()
    client.setQueryData(getGetConfigQueryKey(), { cached: true })
    client.setQueryData(getGetHealthRoutingQueryKey(), { cached: true })
    client.setQueryData(getGetHealthServiceQueryKey(), { cached: true })
    invalidateAfterTerminalConfigOperation(client)
    expect(client.getQueryState(getGetConfigQueryKey())?.isInvalidated).toBe(
      true
    )
    expect(
      client.getQueryState(getGetHealthRoutingQueryKey())?.isInvalidated
    ).toBe(true)
    expect(
      client.getQueryState(getGetHealthServiceQueryKey())?.isInvalidated
    ).toBe(false)
  })

  test("snapshot and issues events fill the health issues cache", () => {
    const client = new QueryClient()
    applyStatusEvent(
      client,
      JSON.stringify({
        type: "snapshot",
        data: {
          service: service("1"),
          outbounds: outbounds("wan"),
          interfaces: interfaces("eth0"),
          issues: { issues: [{ key: "a" }], kernel_checks: [] },
        },
      })
    )
    expect(client.getQueryData(getGetHealthIssuesQueryKey())).toMatchObject({
      data: { issues: [{ key: "a" }] },
      status: 200,
    })
    const servicesBefore = client.getQueryData(getGetHealthServiceQueryKey())
    expect(
      applyStatusEvent(
        client,
        JSON.stringify({
          type: "issues",
          data: { issues: [], kernel_checks: [] },
        })
      )
    ).toBe(true)
    expect(client.getQueryData(getGetHealthIssuesQueryKey())).toMatchObject({
      data: { issues: [] },
    })
    expect(client.getQueryData(getGetHealthServiceQueryKey())).toBe(
      servicesBefore
    )
  })
})
