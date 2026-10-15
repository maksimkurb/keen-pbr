import {
  getGetConfigQueryKey,
  getGetDnsTestQueryKey,
  getGetHealthIssuesQueryKey,
  getGetHealthServiceQueryKey,
  getGetRuntimeInterfacesQueryKey,
  getGetRuntimeOutboundsQueryKey,
} from "@/api/generated/keen-api"

export const queryKeys = {
  healthService: getGetHealthServiceQueryKey,
  healthIssues: getGetHealthIssuesQueryKey,
  runtimeInterfaces: getGetRuntimeInterfacesQueryKey,
  runtimeOutbounds: getGetRuntimeOutboundsQueryKey,
  config: getGetConfigQueryKey,
  dnsTest: getGetDnsTestQueryKey,
}

// Service health, runtime outbounds, health issues and routing health are not listed here: the
// daemon pushes them over the status stream (draft stage/discard reconcile the
// service event, apply/rollback/start/stop publish the runtime state).
export const invalidationKeysAfterConfigMutation = [queryKeys.config()] as const

export const invalidationKeysAfterRuntimeActionMutation = [
  queryKeys.config(),
] as const

export const invalidationKeysAfterApplyConfigMutation = [
  queryKeys.config(),
] as const

// The refresh POST returns after any runtime reload: reconcile_lists_only()
// publishes service and outbounds over the stream before that, so only the
// list refresh state (in config) is re-read here; the routing report follows
// the runtime-state publish over the stream.
export const invalidationKeysAfterListRefreshMutation = [
  queryKeys.config(),
] as const
