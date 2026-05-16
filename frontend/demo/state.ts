import type { ConfigObject } from "../src/api/generated/model/configObject"

export type DemoConfigState = {
  config: ConfigObject
  is_draft: boolean
  list_refresh_state: Record<
    string,
    { last_refresh_ts?: number; content_hash?: string }
  >
}

export function createInitialDemoState(): DemoConfigState {
  return {
    is_draft: false,
    list_refresh_state: {
      shared_list: {
        last_refresh_ts: Math.floor(Date.now() / 1000) - 3600,
        content_hash: "demo-shared",
      },
    },
    config: {
      daemon: {
        strict_enforcement: false,
        firewall_backend: "auto",
      },
      dns: {
        dns_test_server: { listen: "127.0.0.1:5353" },
        servers: [
          {
            tag: "upstream",
            type: "static",
            address: "1.1.1.1",
          },
        ],
        rules: [
          {
            server: "upstream",
            list: ["shared_list"],
          },
        ],
        fallback: ["upstream"],
      },
      lists: {
        shared_list: {
          url: "https://example.com/list.txt",
          domains: ["example.com", "*.example.com"],
        },
        other_list: {
          domains: ["other.example"],
        },
        blocked: {
          ip_cidrs: ["10.0.0.0/8"],
        },
      },
      outbounds: [
        {
          type: "interface",
          tag: "wan0",
          interface: "ppp0",
        },
        {
          type: "interface",
          tag: "vpn0",
          interface: "tun0",
        },
        {
          type: "ignore",
          tag: "drop",
        },
      ],
      route: {
        rules: [
          {
            enabled: true,
            outbound: "wan0",
            list: ["shared_list"],
            dest_addr: "10.0.0.0/8",
          },
          {
            enabled: true,
            outbound: "drop",
            list: ["shared_list"],
            proto: "udp",
          },
          {
            enabled: false,
            outbound: "vpn0",
            list: ["blocked"],
          },
        ],
      },
    },
  }
}

let demoState = createInitialDemoState()

export function getDemoState(): DemoConfigState {
  return demoState
}

export function replaceDemoConfig(config: ConfigObject) {
  demoState = {
    ...demoState,
    config,
    is_draft: true,
  }
}

export function applyDemoConfig() {
  demoState = {
    ...demoState,
    is_draft: false,
  }
}

export function resetDemoState() {
  demoState = createInitialDemoState()
}
