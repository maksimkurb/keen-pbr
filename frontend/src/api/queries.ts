import {
  getHealthService,
  getRuntimeInterfaces,
  getRuntimeOutbounds,
  useGetHealthService as useGeneratedHealthService,
  useGetRuntimeInterfaces as useGeneratedRuntimeInterfaces,
  useGetRuntimeOutbounds as useGeneratedRuntimeOutbounds,
} from "@/api/generated/keen-api"

export {
  getConfig,
  getDnsTest,
  getHealthRouting,
  getHealthService,
  getRuntimeInterfaces,
  getRuntimeOutbounds,
  getGetConfigQueryOptions,
  getGetDnsTestQueryOptions,
  getGetHealthRoutingQueryOptions,
  getGetHealthServiceQueryOptions,
  getGetRuntimeInterfacesQueryOptions,
  getGetRuntimeOutboundsQueryOptions,
  useGetConfig,
  useGetDnsTest,
  useGetHealthRouting,
} from "@/api/generated/keen-api"

export function useGetHealthService() {
  return useGeneratedHealthService<Awaited<ReturnType<typeof getHealthService>>>({
    query: { enabled: false },
  })
}

export function useGetRuntimeOutbounds() {
  return useGeneratedRuntimeOutbounds<
    Awaited<ReturnType<typeof getRuntimeOutbounds>>
  >({
    query: { enabled: false },
  })
}

export function useGetRuntimeInterfaces() {
  return useGeneratedRuntimeInterfaces<
    Awaited<ReturnType<typeof getRuntimeInterfaces>>
  >({
    query: { enabled: false },
  })
}
