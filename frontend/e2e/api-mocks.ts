import type { Page } from "@playwright/test"

const JSON_HEADERS = { "Content-Type": "application/json" }

/** Minimal mocks so Overview and Routing rule create pages load offline in E2E. */
export async function installAppApiMocks(page: Page) {
  await page.route("**/api/health/service", async (route) => {
    await route.fulfill({
      status: 200,
      headers: JSON_HEADERS,
      body: JSON.stringify({
        version: "test",
        build: "test",
        status: "running",
        os_type: "testos",
        os_version: "0",
        build_variant: "test",
        resolver_live_status: "unknown",
        config_is_draft: false,
      }),
    })
  })

  await page.route("**/api/health/routing", async (route) => {
    await route.fulfill({
      status: 200,
      headers: JSON_HEADERS,
      body: JSON.stringify({
        overall: "ok",
        firewall_backend: "nftables",
        firewall: {
          chain_present: false,
          prerouting_hook_present: false,
        },
        firewall_rules: [],
        route_tables: [],
        policy_rules: [],
      }),
    })
  })

  await page.route("**/api/runtime/outbounds", async (route) => {
    await route.fulfill({
      status: 200,
      headers: JSON_HEADERS,
      body: JSON.stringify({
        outbounds: [
          {
            tag: "wan0",
            type: "interface",
            status: "healthy",
            interfaces: [
              {
                outbound_tag: "wan0",
                interface_name: "ppp0",
                status: "active",
              },
            ],
          },
        ],
      }),
    })
  })

  await page.route("**/api/runtime/interfaces", async (route) => {
    await route.fulfill({
      status: 200,
      headers: JSON_HEADERS,
      body: JSON.stringify({
        interfaces: [
          {
            name: "e2e_wan",
            status: "up",
            admin_up: true,
            oper_state: "UNKNOWN",
            carrier: true,
            ipv4_addresses: ["192.0.2.10/24", "192.0.2.11/24", "192.0.2.12/24"],
            ipv6_addresses: ["2001:db8::1/64", "2001:db8::2/64"],
          },
        ],
      }),
    })
  })

  await page.route("**/api/config", async (route) => {
    await route.fulfill({
      status: 200,
      headers: JSON_HEADERS,
      body: JSON.stringify({
        config: {
          lists: {
            shared_list: { domains: ["example.com"] },
            other_list: { domains: ["other.example"] },
          },
          outbounds: [
            {
              type: "interface",
              tag: "wan0",
              interface: "ppp0",
            },
            {
              type: "ignore",
              tag: "drop",
            },
          ],
          route: {
            rules: [
              {
                outbound: "wan0",
                list: ["shared_list"],
                dest_addr: "10.0.0.0/8",
              },
              {
                outbound: "drop",
                list: ["shared_list"],
                proto: "udp",
              },
            ],
          },
        },
        is_draft: false,
      }),
    })
  })
}
