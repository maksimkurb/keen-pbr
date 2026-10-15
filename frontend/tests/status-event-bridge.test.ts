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
  getStreamAuthState,
  subscribeStreamAuthState,
} from "../src/api/status-auth"
import {
  applyStatusEvent,
  observeConfigRevision,
  statusStreamQueryKeys,
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

  test("config revision changes invalidate the config query, the first value only seeds", () => {
    const client = new QueryClient()
    const tracker = { last: null as number | null }
    client.setQueryData(getGetConfigQueryKey(), { cached: true })
    const invalidated = () =>
      client.getQueryState(getGetConfigQueryKey())?.isInvalidated
    const snapshot = (revision: number) =>
      JSON.stringify({ type: "snapshot", data: { config: { revision } } })
    const event = (revision: number) =>
      JSON.stringify({ type: "config", data: { revision } })

    // First value after the first connect: remembered, not invalidated.
    expect(observeConfigRevision(client, tracker, snapshot(5))).toBe(false)
    expect(invalidated()).toBe(false)
    expect(tracker.last).toBe(5)

    // Same value again (e.g. a reconnect with no change): nothing to do.
    expect(observeConfigRevision(client, tracker, snapshot(5))).toBe(false)
    expect(observeConfigRevision(client, tracker, event(5))).toBe(false)
    expect(invalidated()).toBe(false)

    // A pushed change invalidates.
    expect(observeConfigRevision(client, tracker, event(6))).toBe(true)
    expect(invalidated()).toBe(true)
    expect(tracker.last).toBe(6)
  })

  test("a snapshot that differs from the last seen revision invalidates", () => {
    const client = new QueryClient()
    const tracker = { last: 3 as number | null }
    client.setQueryData(getGetConfigQueryKey(), { cached: true })
    expect(
      observeConfigRevision(
        client,
        tracker,
        JSON.stringify({ type: "snapshot", data: { config: { revision: 4 } } })
      )
    ).toBe(true)
    expect(client.getQueryState(getGetConfigQueryKey())?.isInvalidated).toBe(
      true
    )
  })

  test("events without a revision and other datasets are ignored", () => {
    const client = new QueryClient()
    const tracker = { last: 1 as number | null }
    expect(observeConfigRevision(client, tracker, "not json")).toBe(false)
    expect(
      observeConfigRevision(
        client,
        tracker,
        JSON.stringify({ type: "service", data: { version: "1" } })
      )
    ).toBe(false)
    expect(
      observeConfigRevision(
        client,
        tracker,
        JSON.stringify({ type: "snapshot", data: { service: {} } })
      )
    ).toBe(false)
    expect(tracker.last).toBe(1)
  })

  test("snapshot and auth events update the pushed auth state", () => {
    const client = new QueryClient()
    let notified = 0
    const unsubscribe = subscribeStreamAuthState(() => {
      notified += 1
    })
    applyStatusEvent(
      client,
      JSON.stringify({
        type: "snapshot",
        data: {
          service: service("1"),
          outbounds: outbounds("wan"),
          interfaces: interfaces("eth0"),
          auth: { enabled: true, device_name: "Home" },
        },
      })
    )
    expect(getStreamAuthState()).toEqual({ enabled: true, deviceName: "Home" })
    expect(notified).toBe(1)

    // An identical value does not notify.
    applyStatusEvent(
      client,
      JSON.stringify({
        type: "auth",
        data: { enabled: true, device_name: "Home" },
      })
    )
    expect(notified).toBe(1)

    expect(
      applyStatusEvent(
        client,
        JSON.stringify({
          type: "auth",
          data: { enabled: true, device_name: "Office" },
        })
      )
    ).toBe(true)
    expect(getStreamAuthState()).toEqual({
      enabled: true,
      deviceName: "Office",
    })
    expect(notified).toBe(2)
    unsubscribe()
  })

  test("routing health is a stream-owned query", () => {
    expect(statusStreamQueryKeys()).toContainEqual(
      getGetHealthRoutingQueryKey()
    )
  })

  test("snapshot and routing events fill the routing health cache", () => {
    const client = new QueryClient()
    applyStatusEvent(
      client,
      JSON.stringify({
        type: "snapshot",
        data: {
          service: service("1"),
          outbounds: outbounds("wan"),
          interfaces: interfaces("eth0"),
          issues: { issues: [], kernel_checks: [] },
          routing: { overall: "ok", firewall_backend: "nftables" },
        },
      })
    )
    expect(client.getQueryData(getGetHealthRoutingQueryKey())).toMatchObject({
      data: { overall: "ok" },
      status: 200,
    })
    const servicesBefore = client.getQueryData(getGetHealthServiceQueryKey())
    expect(
      applyStatusEvent(
        client,
        JSON.stringify({ type: "routing", data: { overall: "degraded" } })
      )
    ).toBe(true)
    expect(client.getQueryData(getGetHealthRoutingQueryKey())).toMatchObject({
      data: { overall: "degraded" },
    })
    expect(client.getQueryData(getGetHealthServiceQueryKey())).toBe(
      servicesBefore
    )
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
