import {
  getGetConfigQueryKey,
  getGetDnsTestQueryKey,
  getGetHealthIssuesQueryKey,
  getGetHealthRoutingQueryKey,
  getGetHealthServiceQueryKey,
  getGetRuntimeInterfacesQueryKey,
  getGetRuntimeOutboundsQueryKey,
} from "@/api/generated/keen-api"

export const queryKeys = {
  healthService: getGetHealthServiceQueryKey,
  healthRouting: getGetHealthRoutingQueryKey,
  healthIssues: getGetHealthIssuesQueryKey,
  runtimeInterfaces: getGetRuntimeInterfacesQueryKey,
  runtimeOutbounds: getGetRuntimeOutboundsQueryKey,
  config: getGetConfigQueryKey,
  dnsTest: getGetDnsTestQueryKey,
}

// Service health, runtime outbounds and health issues are not listed here: the
// daemon pushes them over the status stream (draft stage/discard reconcile the
// service event, apply/rollback/start/stop publish the runtime state).
export const invalidationKeysAfterConfigMutation = [queryKeys.config()] as const

export const invalidationKeysAfterRuntimeActionMutation = [
  queryKeys.healthRouting(),
  queryKeys.config(),
] as const

// Routing health is re-read by the status bridge once the apply finishes; the
// POST only accepts the operation.
export const invalidationKeysAfterApplyConfigMutation = [
  queryKeys.config(),
] as const

// A list refresh can change what the runtime reports, so service and
// outbounds are kept (served from the stream cache while it is live).
export const invalidationKeysAfterListRefreshMutation = [
  queryKeys.healthService(),
  queryKeys.runtimeOutbounds(),
  queryKeys.config(),
] as const
