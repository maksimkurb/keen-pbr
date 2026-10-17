import {
  getHealthIssues,
  getHealthRouting,
  getHealthService,
  getRuntimeInterfaces,
  getRuntimeOutbounds,
  useGetHealthIssues as useGeneratedHealthIssues,
  useGetHealthRouting as useGeneratedHealthRouting,
  useGetHealthService as useGeneratedHealthService,
  useGetRuntimeInterfaces as useGeneratedRuntimeInterfaces,
  useGetRuntimeOutbounds as useGeneratedRuntimeOutbounds,
} from "@/api/generated/keen-api"
import type { UseQueryOptions, UseQueryResult } from "@tanstack/react-query"

import { createStatusStreamQueryFn } from "@/api/status-connection"

export {
  getConfig,
  getConfigDefaults,
  getAuthPasswordStatus,
  getDnsTest,
  getHealthRouting,
  getHealthService,
  getRuntimeInterfaces,
  getRuntimeOutbounds,
  getGetConfigQueryOptions,
  getGetConfigDefaultsQueryOptions,
  getGetAuthPasswordStatusQueryOptions,
  getGetDnsTestQueryOptions,
  getGetHealthRoutingQueryOptions,
  getGetHealthServiceQueryOptions,
  getGetRuntimeInterfacesQueryOptions,
  getGetRuntimeOutboundsQueryOptions,
  useGetKeeneticDnsProxies,
  useGetConfig,
  useGetConfigDefaults,
  useGetAuthPasswordStatus,
  useGetDnsTest,
} from "@/api/generated/keen-api"

// Service, outbounds, interfaces, issues and routing health are pushed by the status stream. Their
// queries read what the stream wrote and only hit REST while it is down.
const statusStreamQueryOptions = {
  staleTime: Infinity,
  refetchOnWindowFocus: false,
} as const

type StatusQueryOptions<TFetched, TError, TData> = {
  query?: Partial<UseQueryOptions<TFetched, TError, TData>>
}

export function useGetHealthService<
  TData = Awaited<ReturnType<typeof getHealthService>>,
  TError = unknown,
>(
  options?: StatusQueryOptions<
    Awaited<ReturnType<typeof getHealthService>>,
    TError,
    TData
  >
): UseQueryResult<TData, TError> {
  return useGeneratedHealthService({
    query: {
      ...statusStreamQueryOptions,
      queryFn: createStatusStreamQueryFn((signal) =>
        getHealthService({ signal })
      ),
      ...options?.query,
    },
  })
}

export function useGetRuntimeOutbounds<
  TData = Awaited<ReturnType<typeof getRuntimeOutbounds>>,
  TError = unknown,
>(
  options?: StatusQueryOptions<
    Awaited<ReturnType<typeof getRuntimeOutbounds>>,
    TError,
    TData
  >
): UseQueryResult<TData, TError> {
  return useGeneratedRuntimeOutbounds({
    query: {
      ...statusStreamQueryOptions,
      queryFn: createStatusStreamQueryFn((signal) =>
        getRuntimeOutbounds({ signal })
      ),
      ...options?.query,
    },
  })
}

export function useGetRuntimeInterfaces<
  TData = Awaited<ReturnType<typeof getRuntimeInterfaces>>,
  TError = unknown,
>(
  options?: StatusQueryOptions<
    Awaited<ReturnType<typeof getRuntimeInterfaces>>,
    TError,
    TData
  >
): UseQueryResult<TData, TError> {
  return useGeneratedRuntimeInterfaces({
    query: {
      ...statusStreamQueryOptions,
      queryFn: createStatusStreamQueryFn((signal) =>
        getRuntimeInterfaces({ signal })
      ),
      ...options?.query,
    },
  })
}

export function useGetHealthIssues<
  TData = Awaited<ReturnType<typeof getHealthIssues>>,
  TError = unknown,
>(
  options?: StatusQueryOptions<
    Awaited<ReturnType<typeof getHealthIssues>>,
    TError,
    TData
  >
): UseQueryResult<TData, TError> {
  return useGeneratedHealthIssues({
    query: {
      ...statusStreamQueryOptions,
      queryFn: createStatusStreamQueryFn((signal) =>
        getHealthIssues({ signal })
      ),
      ...options?.query,
    },
  })
}

export function useGetHealthRouting<
  TData = Awaited<ReturnType<typeof getHealthRouting>>,
  TError = unknown,
>(
  options?: StatusQueryOptions<
    Awaited<ReturnType<typeof getHealthRouting>>,
    TError,
    TData
  >
): UseQueryResult<TData, TError> {
  return useGeneratedHealthRouting({
    query: {
      ...statusStreamQueryOptions,
      queryFn: createStatusStreamQueryFn((signal) =>
        getHealthRouting({ signal })
      ),
      ...options?.query,
    },
  })
}
