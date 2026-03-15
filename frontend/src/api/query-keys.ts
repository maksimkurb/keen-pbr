import {
  getGetConfigQueryKey,
  getGetDnsTestQueryKey,
  getGetHealthRoutingQueryKey,
  getGetHealthServiceQueryKey,
} from "@/api/generated/keen-api"

export const queryKeys = {
  healthService: getGetHealthServiceQueryKey,
  healthRouting: getGetHealthRoutingQueryKey,
  config: getGetConfigQueryKey,
  dnsTest: getGetDnsTestQueryKey,
}

export const invalidationKeysAfterConfigMutation = [
  queryKeys.config(),
  queryKeys.healthService(),
] as const

export const invalidationKeysAfterReloadMutation = [
  queryKeys.healthRouting(),
  queryKeys.healthService(),
  queryKeys.config(),
] as const

export const invalidationKeysAfterConfigSaveMutation = [
  queryKeys.healthRouting(),
  queryKeys.healthService(),
  queryKeys.config(),
] as const
