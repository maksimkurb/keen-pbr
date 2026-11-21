import type {
  ConfigStateResponse,
  DraftConfig,
  HealthIssuesResponse,
  HealthResponse,
  RoutingHealthResponse,
  RuntimeInterfaceInventoryResponse,
  RuntimeOutboundsResponse,
} from "../src/types.js";

export function baseConfig(): DraftConfig {
  return {
    version: "3.4.0",
    device_name: "Test router",
    daemon: { strict_enforcement: true },
    outbounds: [
      { type: "interface", tag: "vpn", interface: "tun0" },
      { type: "interface", tag: "wan", interface: "eth0" },
    ],
    lists: {
      existing: { domains: ["example.org"] },
      remote: { url: "https://example.com/list.txt" },
    },
    route: {
      rules: [{ list: ["existing"], outbound: "vpn" }],
    },
  };
}

export function configState(config: DraftConfig = baseConfig()): ConfigStateResponse {
  return { config, is_draft: false };
}

export function healthyHealth(): HealthResponse {
  return {
    version: "3.4.0",
    build: "20260101000000",
    status: "running",
    runtime_state: "running",
    os_type: "keenetic",
    os_version: "4.2.7",
    build_variant: "mips",
    config_is_draft: false,
    rollback_available: false,
  };
}

export function healthyRouting(): RoutingHealthResponse {
  return {
    overall: "ok",
    firewall_backend: "nftables",
    firewall: { chain_present: true, prerouting_hook_present: true, verification_state: "verified" },
    firewall_rules: [],
    route_tables: [],
    policy_rules: [],
    warnings: [],
  };
}

export function healthyIssues(): HealthIssuesResponse {
  return { issues: [], kernel_checks: [] };
}

export function healthyOutbounds(): RuntimeOutboundsResponse {
  return {
    outbounds: [
      {
        tag: "vpn",
        type: "interface",
        status: "healthy",
        interfaces: [{ outbound_tag: "vpn", interface_name: "tun0", status: "active", latency_ms: 30 }],
      },
    ],
  };
}

export function healthyInterfaces(): RuntimeInterfaceInventoryResponse {
  return {
    interfaces: [
      { name: "eth0", status: "up", admin_up: true, carrier: true },
      { name: "tun0", status: "up", admin_up: true, carrier: true },
    ],
  };
}

export function degradedHealth(): HealthResponse {
  return {
    ...healthyHealth(),
    status: "degraded",
    config_is_draft: true,
    intercept: {
      enabled: true,
      running: true,
      dns_hold_active: true,
      l7_active: true,
      capabilities: { nfqueue: true, nflog: true, connbytes: true, addrtype: true },
      reasons: [],
    },
    dnsmasq: {
      mode: "dnsmasq",
      state: "error",
      rules: 2,
      domains: 100,
      last_error: "platform hook failed",
      error_code: "DNSMASQ_HOOK_FAILED",
    },
  };
}

export function degradedRouting(): RoutingHealthResponse {
  return {
    overall: "degraded",
    firewall_backend: "nftables",
    firewall: { chain_present: true, prerouting_hook_present: true, verification_state: "verified" },
    firewall_rules: [
      {
        set_name: "keen-pbr-vpn",
        action: "MARK",
        expected_fwmark: "0x00010000",
        status: "missing",
        detail: "rule not found in mangle",
      },
    ],
    route_tables: [],
    policy_rules: [],
    warnings: [
      {
        code: "nat_missing",
        interface: "eth1",
        outbound: "wan2",
        message: "no MASQUERADE rule covers eth1",
      },
    ],
  };
}

export function degradedIssues(): HealthIssuesResponse {
  return {
    issues: [
      {
        key: "outbound-vpn",
        severity: "error",
        code: "outbound_unavailable",
        title: "vpn is unavailable",
        detail: "the selected interface has no carrier",
        params: { tag: "vpn" },
        outbound: "vpn",
        entries: ["wan1 (tun0): unavailable"],
      },
      {
        key: "dnsmasq",
        severity: "warning",
        code: "dnsmasq_error",
        title: "dnsmasq does not serve the keen-pbr config",
        params: {},
      },
    ],
    kernel_checks: [],
  };
}

export function degradedOutbounds(): RuntimeOutboundsResponse {
  return {
    outbounds: [
      {
        tag: "vpn",
        type: "interface",
        status: "unavailable",
        detail: "interface tun0 is down",
        interfaces: [
          {
            outbound_tag: "wan1",
            interface_name: "tun0",
            status: "unavailable",
            detail: "carrier down",
          },
        ],
      },
      {
        tag: "wan",
        type: "interface",
        status: "healthy",
        interfaces: [{ outbound_tag: "wan", interface_name: "eth0", status: "active" }],
      },
    ],
  };
}

export function degradedInterfaces(): RuntimeInterfaceInventoryResponse {
  return {
    interfaces: [
      { name: "eth0", status: "up", admin_up: true, carrier: true },
      { name: "eth1", status: "up", admin_up: true, carrier: true },
      { name: "tun0", status: "down", admin_up: false, carrier: false },
    ],
  };
}
