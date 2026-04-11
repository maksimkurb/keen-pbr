import {
  getGetConfigQueryKey,
  getGetDnsTestQueryKey,
  getGetHealthRoutingQueryKey,
  getGetHealthServiceQueryKey,
  getGetRuntimeInterfacesQueryKey,
  getGetRuntimeOutboundsQueryKey,
} from "@/api/generated/keen-api"

export const queryKeys = {
  healthService: getGetHealthServiceQueryKey,
  healthRouting: getGetHealthRoutingQueryKey,
  runtimeInterfaces: getGetRuntimeInterfacesQueryKey,
  runtimeOutbounds: getGetRuntimeOutboundsQueryKey,
  config: getGetConfigQueryKey,
  dnsTest: getGetDnsTestQueryKey,
}

export const invalidationKeysAfterConfigMutation = [
  queryKeys.config(),
  queryKeys.healthService(),
  queryKeys.runtimeOutbounds(),
] as const

export const invalidationKeysAfterRuntimeActionMutation = [
  queryKeys.healthRouting(),
  queryKeys.healthService(),
  queryKeys.runtimeOutbounds(),
  queryKeys.config(),
] as const

export const invalidationKeysAfterApplyConfigMutation = [
  queryKeys.healthRouting(),
  queryKeys.healthService(),
  queryKeys.runtimeOutbounds(),
  queryKeys.config(),
] as const

export const invalidationKeysAfterListRefreshMutation = [
  queryKeys.healthRouting(),
  queryKeys.healthService(),
  queryKeys.runtimeOutbounds(),
  queryKeys.config(),
] as const
