import type { Page, Route } from "@playwright/test"

import type { ConfigObject } from "../src/api/generated/model/configObject"
import type { Outbound } from "../src/api/generated/model/outbound"
import {
  createAppMockConfig,
  createAppMockListRefreshState,
  E2E_RUNTIME_INTERFACES,
} from "../fixtures/app-mock-config"

const JSON_HEADERS = { "Content-Type": "application/json" }

let mockConfig: ConfigObject = createAppMockConfig()
let mockIsDraft = false
let postConfigCallCount = 0

function configStateResponse() {
  return {
    config: mockConfig,
    is_draft: mockIsDraft,
    list_refresh_state: createAppMockListRefreshState(),
  }
}

async function fulfillJson(route: Route, status: number, payload: unknown) {
  await route.fulfill({
    status,
    headers: JSON_HEADERS,
    body: JSON.stringify(payload),
  })
}

/** Stateful mocks for offline Playwright runs (no real keen-pbr daemon). */
export async function installAppApiMocks(page: Page) {
  await page.route("**/api/**", async (route) => {
    const request = route.request()
    const url = new URL(request.url())
    const pathname = url.pathname
    const method = request.method()

    if (method === "GET" && pathname === "/api/health/service") {
      await fulfillJson(route, 200, {
        version: "test",
        build: "test",
        status: "running",
        os_type: "testos",
        os_version: "0",
        build_variant: "test",
        resolver_live_status: "healthy",
        resolver_config_sync_state: "converged",
        config_is_draft: mockIsDraft,
      })
      return
    }

    if (method === "GET" && pathname === "/api/health/routing") {
      await fulfillJson(route, 200, {
        overall: "ok",
        firewall_backend: "nftables",
        firewall: { chain_present: true, prerouting_hook_present: true },
        firewall_rules: [],
        route_tables: [],
        policy_rules: [],
      })
      return
    }

    if (method === "GET" && pathname === "/api/runtime/outbounds") {
      await fulfillJson(route, 200, {
        outbounds: (mockConfig.outbounds ?? []).map((outbound: Outbound) => ({
          tag: outbound.tag,
          type: outbound.type,
          status: outbound.tag === "drop" ? "unavailable" : "healthy",
          interfaces:
            outbound.type === "interface"
              ? [
                  {
                    outbound_tag: outbound.tag,
                    interface_name: outbound.interface ?? "eth0",
                    status: "active",
                  },
                ]
              : [],
        })),
      })
      return
    }

    if (method === "GET" && pathname === "/api/runtime/interfaces") {
      await fulfillJson(route, 200, E2E_RUNTIME_INTERFACES)
      return
    }

    if (method === "GET" && pathname === "/api/config") {
      await fulfillJson(route, 200, configStateResponse())
      return
    }

    if (method === "POST" && pathname === "/api/config") {
      const body = request.postDataJSON() as ConfigObject
      mockConfig = body
      mockIsDraft = true
      postConfigCallCount += 1
      await fulfillJson(route, 200, {
        status: "ok",
        message: "staged",
      })
      return
    }

    if (method === "POST" && pathname === "/api/config/save") {
      mockIsDraft = false
      await fulfillJson(route, 200, {
        status: "ok",
        message: "applied",
        apply_started_ts: Math.floor(Date.now() / 1000),
      })
      return
    }

    if (method === "POST" && pathname === "/api/lists/refresh") {
      await fulfillJson(route, 200, {
        status: "ok",
        message: "refreshed",
        refreshed_lists: Object.keys(mockConfig.lists ?? {}),
        changed_lists: [],
        failed_lists: [],
        reloaded: false,
      })
      return
    }

    if (
      method === "POST" &&
      (pathname === "/api/service/start" ||
        pathname === "/api/service/stop" ||
        pathname === "/api/service/restart")
    ) {
      await fulfillJson(route, 200, { status: "ok", message: "ok" })
      return
    }

    if (method === "POST" && pathname === "/api/routing/test") {
      await fulfillJson(route, 200, {
        target: "example.com",
        is_domain: true,
        resolved_ips: ["203.0.113.10"],
        warnings: [],
        no_matching_rule: false,
        rule_diagnostics: [],
        results: [
          {
            ip: "203.0.113.10",
            expected_outbound: "wan0",
            actual_outbound: "wan0",
            ok: true,
          },
        ],
      })
      return
    }

    if (method === "GET" && pathname === "/api/dns/test") {
      await route.fulfill({
        status: 200,
        headers: {
          "Content-Type": "text/event-stream",
          "Cache-Control": "no-cache",
        },
        body: 'data: {"type":"HELLO"}\n\n',
      })
      return
    }

    await fulfillJson(route, 404, {
      error: `e2e mock: unhandled ${method} ${pathname}`,
    })
  })
}

/** Reset in-memory mock config between tests in the same worker. */
export function resetAppApiMocks() {
  mockConfig = createAppMockConfig()
  mockIsDraft = false
  postConfigCallCount = 0
}

export function getMockApiState() {
  return {
    config: mockConfig,
    isDraft: mockIsDraft,
    postConfigCallCount,
  }
}
