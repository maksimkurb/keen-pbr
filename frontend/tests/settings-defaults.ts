import type { ConfigObject } from "../src/api/generated/model/configObject"
import { toSettingsDefaults } from "../src/pages/general-config-page"

// What GET /api/config/defaults returns (make_config_defaults() in the daemon).
export const defaults = toSettingsDefaults({
  daemon: {
    strict_enforcement: false,
    skip_marked_packets: true,
    clear_dynamic_sets_on_apply: false,
    reuse_static_sets_on_runtime_refresh: true,
    ipv6_enabled: true,
    ipset_hashsize: 1024,
    ipset_maxelem: 65536,
  },
  fwmark: { start: "0x00010000", mask: "0x00ff0000" },
  iproute: { table_start: 150, process_router_traffic: false },
  lists_autoupdate: { enabled: false },
  intercept: {
    enabled: true,
    min_ttl_ms: 300000,
    max_ttl_ms: 86400000,
    dns: {
      enabled: true,
      queue_num: 9053,
      hold_timeout_ms: 30,
      marker: { domain: "check.keen-pbr.fyi", answer_ipv4: "127.0.0.88" },
    },
    l7: { enabled: true, nflog_group: 9054, tls: true, http: true, quic: true },
  },
} as ConfigObject)
