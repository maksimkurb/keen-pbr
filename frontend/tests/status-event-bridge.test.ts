import { describe, expect, test } from "bun:test"
import { QueryClient } from "@tanstack/react-query"

import {
  getGetHealthServiceQueryKey,
  getGetRuntimeInterfacesQueryKey,
  getGetRuntimeOutboundsQueryKey,
} from "../src/api/generated/keen-api"
import { applyStatusEvent } from "../src/api/status-event-cache"

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
})
