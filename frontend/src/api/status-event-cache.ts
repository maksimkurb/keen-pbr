import { QueryClient } from "@tanstack/react-query"

import {
  getGetConfigQueryKey,
  getGetHealthIssuesQueryKey,
  getGetHealthRoutingQueryKey,
  getGetHealthServiceQueryKey,
  getGetRuntimeInterfacesQueryKey,
  getGetRuntimeOutboundsQueryKey,
  type getHealthIssuesResponseSuccess,
  type getHealthServiceResponseSuccess,
  type getRuntimeInterfacesResponseSuccess,
  type getRuntimeOutboundsResponseSuccess,
} from "@/api/generated/keen-api"
import type {
  HealthIssuesResponse,
  HealthResponse,
  RuntimeInterfaceInventoryResponse,
  RuntimeOutboundsResponse,
  StatusEventInterfaces,
  StatusEventIssues,
  StatusEventOutbounds,
  StatusEventService,
  StatusEventSnapshot,
} from "@/api/generated/model"

type StatusEvent =
  | StatusEventSnapshot
  | StatusEventService
  | StatusEventOutbounds
  | StatusEventInterfaces
  | StatusEventIssues

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

  switch (event?.type) {
    case "snapshot":
      setService(event.data.service)
      setOutbounds(event.data.outbounds)
      setInterfaces(event.data.interfaces)
      setIssues(event.data.issues)
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
    default:
      return false
  }
  return true
}

export function getTerminalConfigLifecycleOperationKey(
  rawData: string
): string | null {
  let event: StatusEvent | null
  try {
    event = JSON.parse(rawData) as StatusEvent
  } catch {
    return null
  }

  const service =
    event?.type === "snapshot"
      ? event.data.service
      : event?.type === "service"
        ? event.data
        : null
  const operation = service?.lifecycle_operation
  if (
    !operation ||
    (operation.type !== "apply_config" &&
      operation.type !== "rollback_config") ||
    (operation.status !== "succeeded" && operation.status !== "failed")
  ) {
    return null
  }

  return `${operation.id}:${operation.status}`
}

/** Query keys whose data is owned by the status stream. */
export function statusStreamQueryKeys() {
  return [
    getGetHealthServiceQueryKey(),
    getGetRuntimeOutboundsQueryKey(),
    getGetRuntimeInterfacesQueryKey(),
    getGetHealthIssuesQueryKey(),
  ] as const
}

/**
 * An apply/rollback finished: resync the config and re-read what the new
 * runtime changed that the stream does not carry (routing health).
 */
export function invalidateAfterTerminalConfigOperation(
  queryClient: QueryClient
) {
  for (const queryKey of [
    getGetConfigQueryKey(),
    getGetHealthRoutingQueryKey(),
  ]) {
    void queryClient.invalidateQueries({ queryKey })
  }
}
