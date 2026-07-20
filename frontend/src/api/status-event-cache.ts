import { QueryClient } from "@tanstack/react-query"

import {
  getGetHealthServiceQueryKey,
  getGetRuntimeInterfacesQueryKey,
  getGetRuntimeOutboundsQueryKey,
  type getHealthServiceResponseSuccess,
  type getRuntimeInterfacesResponseSuccess,
  type getRuntimeOutboundsResponseSuccess,
} from "@/api/generated/keen-api"
import type {
  HealthResponse,
  RuntimeInterfaceInventoryResponse,
  RuntimeOutboundsResponse,
  StatusEventInterfaces,
  StatusEventOutbounds,
  StatusEventService,
  StatusEventSnapshot,
} from "@/api/generated/model"

type StatusEvent =
  | StatusEventSnapshot
  | StatusEventService
  | StatusEventOutbounds
  | StatusEventInterfaces

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

  switch (event?.type) {
    case "snapshot":
      setService(event.data.service)
      setOutbounds(event.data.outbounds)
      setInterfaces(event.data.interfaces)
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
    default:
      return false
  }
  return true
}
