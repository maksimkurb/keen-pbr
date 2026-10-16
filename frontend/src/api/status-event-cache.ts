import { QueryClient } from "@tanstack/react-query"

import { setStreamAuthState } from "@/api/status-auth"
import {
  getGetConfigQueryKey,
  getGetHealthIssuesQueryKey,
  getGetHealthRoutingQueryKey,
  getGetHealthServiceQueryKey,
  getGetRuntimeInterfacesQueryKey,
  getGetRuntimeOutboundsQueryKey,
  type getHealthIssuesResponseSuccess,
  type getHealthRoutingResponseSuccess,
  type getHealthServiceResponseSuccess,
  type getRuntimeInterfacesResponseSuccess,
  type getRuntimeOutboundsResponseSuccess,
} from "@/api/generated/keen-api"
import type {
  HealthIssuesResponse,
  HealthResponse,
  RoutingHealthResponse,
  RuntimeInterfaceInventoryResponse,
  RuntimeOutboundsResponse,
  StatusAuthState,
  StatusEventAuth,
  StatusEventConfig,
  StatusEventInterfaces,
  StatusEventIssues,
  StatusEventOutbounds,
  StatusEventRouting,
  StatusEventService,
  StatusEventSnapshot,
} from "@/api/generated/model"

type StatusEvent =
  | StatusEventSnapshot
  | StatusEventService
  | StatusEventOutbounds
  | StatusEventInterfaces
  | StatusEventIssues
  | StatusEventRouting
  | StatusEventAuth
  | StatusEventConfig

function response<T>(data: T) {
  return { data, status: 200 as const, headers: new Headers() }
}

export function applyStatusEvent(
  queryClient: QueryClient,
  rawData: string
): boolean {
  let event: StatusEvent | null
  try {
    event = JSON.parse(rawData) as StatusEvent
  } catch {
    return false
  }

  const setService = (data: HealthResponse) =>
    queryClient.setQueryData<getHealthServiceResponseSuccess>(
      getGetHealthServiceQueryKey(),
      response(data)
    )
  const setOutbounds = (data: RuntimeOutboundsResponse) =>
    queryClient.setQueryData<getRuntimeOutboundsResponseSuccess>(
      getGetRuntimeOutboundsQueryKey(),
      response(data)
    )
  const setInterfaces = (data: RuntimeInterfaceInventoryResponse) =>
    queryClient.setQueryData<getRuntimeInterfacesResponseSuccess>(
      getGetRuntimeInterfacesQueryKey(),
      response(data)
    )

  const setIssues = (data: HealthIssuesResponse) =>
    queryClient.setQueryData<getHealthIssuesResponseSuccess>(
      getGetHealthIssuesQueryKey(),
      response(data)
    )

  const setRouting = (data: RoutingHealthResponse) =>
    queryClient.setQueryData<getHealthRoutingResponseSuccess>(
      getGetHealthRoutingQueryKey(),
      response(data)
    )

  const setAuth = (data: StatusAuthState | undefined) => {
    if (data) {
      setStreamAuthState({
        enabled: data.enabled,
        deviceName: data.device_name,
      })
    }
  }

  switch (event?.type) {
    case "snapshot":
      setService(event.data.service)
      setOutbounds(event.data.outbounds)
      setInterfaces(event.data.interfaces)
      setIssues(event.data.issues)
      setRouting(event.data.routing)
      setAuth(event.data.auth)
      break
    case "service":
      setService(event.data)
      break
    case "outbounds":
      setOutbounds(event.data)
      break
    case "interfaces":
      setInterfaces(event.data)
      break
    case "issues":
      setIssues(event.data)
      break
    case "routing":
      setRouting(event.data)
      break
    case "auth":
      setAuth(event.data)
      break
    default:
      return false
  }
  return true
}

/** The last config revision the stream reported, across reconnects. */
export type ConfigRevisionTracker = { last: number | null }

/**
 * Invalidates the config query when the stream reports a revision other than
 * the last one seen. The first value only seeds the tracker; a snapshot after
 * a reconnect that differs from the last revision means the config changed
 * while the stream was down.
 */
export function observeConfigRevision(
  queryClient: QueryClient,
  tracker: ConfigRevisionTracker,
  rawData: string
): boolean {
  let event: StatusEvent | null
  try {
    event = JSON.parse(rawData) as StatusEvent
  } catch {
    return false
  }
  const revision =
    event?.type === "snapshot"
      ? event.data.config?.revision
      : event?.type === "config"
        ? event.data.revision
        : undefined
  if (typeof revision !== "number") return false

  const changed = tracker.last !== null && tracker.last !== revision
  tracker.last = revision
  if (changed) {
    void queryClient.invalidateQueries({ queryKey: getGetConfigQueryKey() })
  }
  return changed
}

/** Query keys whose data is owned by the status stream. */
export function statusStreamQueryKeys() {
  return [
    getGetHealthServiceQueryKey(),
    getGetRuntimeOutboundsQueryKey(),
    getGetRuntimeInterfacesQueryKey(),
    getGetHealthIssuesQueryKey(),
    getGetHealthRoutingQueryKey(),
  ] as const
}
