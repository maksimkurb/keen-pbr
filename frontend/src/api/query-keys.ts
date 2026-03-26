import {
  getGetConfigQueryKey,
  getGetDnsTestQueryKey,
  getGetHealthRoutingQueryKey,
  getGetHealthServiceQueryKey,
  getGetRuntimeOutboundsQueryKey,
} from "@/api/generated/keen-api"

export const queryKeys = {
  healthService: getGetHealthServiceQueryKey,
  healthRouting: getGetHealthRoutingQueryKey,
  runtimeOutbounds: getGetRuntimeOutboundsQueryKey,
  config: getGetConfigQueryKey,
  dnsTest: getGetDnsTestQueryKey,
}

export const invalidationKeysAfterConfigMutation = [
  queryKeys.config(),
  queryKeys.healthService(),
  queryKeys.runtimeOutbounds(),
] as const

export const invalidationKeysAfterReloadMutation = [
  queryKeys.healthRouting(),
  queryKeys.healthService(),
  queryKeys.runtimeOutbounds(),
  queryKeys.config(),
] as const

export const invalidationKeysAfterConfigSaveMutation = [
  queryKeys.healthRouting(),
  queryKeys.healthService(),
  queryKeys.runtimeOutbounds(),
  queryKeys.config(),
] as const
