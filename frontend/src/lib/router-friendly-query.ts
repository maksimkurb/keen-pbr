import type { QueryClient } from "@tanstack/react-query"

/** Default poll period for outbound/runtime-style queries when the UI tab is active. */
export const ROUTER_RUNTIME_POLL_MS = 30_000

export function isDocumentTabHidden(
  visibilityState: DocumentVisibilityState = getDocumentVisibilityState(),
): boolean {
  return visibilityState !== "visible"
}

export function hasConfigMutationInFlight(
  queryClient: Pick<QueryClient, "isMutating">,
): boolean {
  return (
    queryClient.isMutating({ mutationKey: ["postConfig"] }) > 0 ||
    queryClient.isMutating({ mutationKey: ["postConfigSave"] }) > 0
  )
}

/** Resolves TanStack `refetchInterval` for router-friendly polling. */
export function resolveRouterFriendlyPollInterval(
  queryClient: Pick<QueryClient, "isMutating">,
  intervalWhenActiveMs: number,
  visibilityState: DocumentVisibilityState = getDocumentVisibilityState(),
): number | false {
  if (isDocumentTabHidden(visibilityState)) {
    return false
  }

  if (hasConfigMutationInFlight(queryClient)) {
    return false
  }

  return intervalWhenActiveMs
}

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
  return () => resolveRouterFriendlyPollInterval(queryClient, intervalWhenActiveMs)
}

function getDocumentVisibilityState(): DocumentVisibilityState {
  if (typeof document === "undefined") {
    return "visible"
  }

  return document.visibilityState
}
