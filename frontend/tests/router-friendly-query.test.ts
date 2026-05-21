import { describe, expect, test } from "bun:test"
import { QueryClient } from "@tanstack/react-query"

import {
  hasConfigMutationInFlight,
  isDocumentTabHidden,
  resolveRouterFriendlyPollInterval,
  ROUTER_RUNTIME_POLL_MS,
} from "../src/lib/router-friendly-query"

describe("router-friendly polling", () => {
  test("isDocumentTabHidden respects visibility state", () => {
    expect(isDocumentTabHidden("visible")).toBe(false)
    expect(isDocumentTabHidden("hidden")).toBe(true)
  })

  test("hasConfigMutationInFlight detects postConfig and postConfigSave", () => {
    const queryClient = {
      isMutating: ({ mutationKey }: { mutationKey: string[] }) => {
        if (mutationKey[0] === "postConfig") {
          return 1
        }
        return 0
      },
    } as Pick<QueryClient, "isMutating">

    expect(hasConfigMutationInFlight(queryClient)).toBe(true)
  })

  test("resolveRouterFriendlyPollInterval pauses when hidden or mutating", () => {
    const idleClient = {
      isMutating: () => 0,
    } as Pick<QueryClient, "isMutating">

    expect(
      resolveRouterFriendlyPollInterval(
        idleClient,
        ROUTER_RUNTIME_POLL_MS,
        "visible",
      ),
    ).toBe(ROUTER_RUNTIME_POLL_MS)

    expect(
      resolveRouterFriendlyPollInterval(
        idleClient,
        ROUTER_RUNTIME_POLL_MS,
        "hidden",
      ),
    ).toBe(false)

    const mutatingClient = {
      isMutating: ({ mutationKey }: { mutationKey: string[] }) =>
        mutationKey[0] === "postConfigSave" ? 1 : 0,
    } as Pick<QueryClient, "isMutating">

    expect(
      resolveRouterFriendlyPollInterval(
        mutatingClient,
        ROUTER_RUNTIME_POLL_MS,
        "visible",
      ),
    ).toBe(false)
  })
})
