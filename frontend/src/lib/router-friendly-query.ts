import type { QueryClient } from "@tanstack/react-query"

/** Default poll period for outbound/runtime-style queries when the UI tab is active. */
export const ROUTER_RUNTIME_POLL_MS = 30_000

/**
 * Pause background polling while the browser tab is hidden or while draft/apply config
 * writes are running (fewer bursts on low-power routers; avoids racing refetches mid-save).
 * Mounting components must call `useConfigMutationPending()` when using this interval so
 * the query options refresh when mutations start/end.
 */
export function routerFriendlyPollingMs(
  queryClient: QueryClient,
  intervalWhenActiveMs: number,
) {
  return () => {
    if (
      typeof document !== "undefined" &&
      document.visibilityState !== "visible"
    ) {
      return false
    }

    if (queryClient.isMutating({ mutationKey: ["postConfig"] }) > 0) {
      return false
    }

    if (queryClient.isMutating({ mutationKey: ["postConfigSave"] }) > 0) {
      return false
    }

    return intervalWhenActiveMs
  }
}
