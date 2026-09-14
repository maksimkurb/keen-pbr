---
title: Full Reference Config
weight: 6
---

This page shows one commented JSON example with every supported configuration section and every supported option.

Comments are supported in real keen-pbr config files, so you can use this example directly as a starting point.

{{< callout type="info" >}}
List names, outbound tags, and DNS server tags must match `^[a-z][a-z0-9_]*$` and must be at most 24 characters.
{{< /callout >}}

```json {filename="config.json"}
{
  // Optional device label shown in the browser title and beneath the keen-pbr logo.
  // Maximum length: 128 characters. Default: empty (the standard keen-pbr
  // branding is used).
  "device_name": "Home router",

  // Global daemon settings.
  // All fields in this section are optional.
  "daemon": {
    // Path to the PID file.
    // Default: no PID file is written when omitted.
    "pid_file": "/var/run/keen-pbr.pid",

    // Directory used for remote-list cache files and cache metadata.
    // Default: "/var/cache/keen-pbr".
    "cache_dir": "/var/cache/keen-pbr",

    // Firewall backend selection.
    // Supported values: "auto", "iptables", "nftables".
    // Default: "auto".
    "firewall_backend": "auto",

    // Skip packets that already have an fwmark before keen-pbr touches them.
    // Default: true (also when set to null).
    "skip_marked_packets": true,

    // Clear dnsmasq-managed dynamic sets during a full apply or runtime restart.
    // Default: true (also when set to null).
    "clear_dynamic_sets_on_apply": true,

    // Optional initial hash table size for ipsets created by the iptables backend.
    // Has no effect with nftables. Minimum: 1; maximum: 2147483648. Omit or set
    // to null to use the ipset default (1024).
    // Changing it while iptables is running recreates owned ipsets and clears
    // dnsmasq-learned entries.
    "ipset_hashsize": null,

    // Optional maximum element count for ipsets created by the iptables backend.
    // Has no effect with nftables. Minimum: 1; maximum: 4294967295. Omit or set
    // to null to use the ipset default (65536).
    // Changing it while iptables is running recreates owned ipsets and clears
    // dnsmasq-learned entries.
    "ipset_maxelem": null,

    // Reuse the currently live list sets during safe runtime refreshes.
    // Default: true; failed preflight falls back to PreserveSets.
    "reuse_static_sets_on_runtime_refresh": true,

    // Install IPv4/IPv6 firewall sets and resolver targets.
    // Default: true (also when set to null). If the system has no IPv6 support,
    // keen-pbr logs an error and continues in IPv4-only mode.
    "ipv6_enabled": true,

    // Default strict routing behavior for interface outbounds.
    // Default: false.
    "strict_enforcement": false,

    // Terminal action used by strict enforcement.
    // Supported values: "unreachable" (return a network error) or "blackhole"
    // (silently drop packets). Default: "unreachable".
    "strict_enforcement_action": "unreachable",

    // Maximum allowed size for downloaded remote content such as URL-backed lists.
    // Default: 8388608 bytes (8 MiB).
    "max_file_size_bytes": 8388608,

    // Maximum stdout bytes captured per firewall verification command.
    // Use 0 for unlimited capture.
    // Default: 262144.
    "firewall_verify_max_bytes": 262144,

    // Maximum time in seconds allowed for privileged helpers and hooks.
    // Minimum: 1. Default: 30.
    "exec_timeout_seconds": 30,

    // Maximum time in seconds to wait for resolver configuration generation
    // after the resolver reload hook completes. Minimum: 1. Default: 120.
    "resolver_ready_timeout_seconds": 120,

    // Grace period in seconds after SIGTERM before a timed-out helper receives
    // SIGKILL. Minimum: 0. Default: 2.
    "exec_kill_grace_seconds": 2
  },

  // Embedded HTTP API and Web UI settings.
  // WARNING: This section is NOT available in keen-pbr-headless package
  "api": {
    // Enable or disable the HTTP API / Web UI.
    // Default: false.
    "enabled": true,

    // Listen address for the API server.
    // Default: "0.0.0.0:12121".
    "listen": "0.0.0.0:12121",

    // Maximum request body size in bytes. Minimum: 1024. Default: 1048576.
    "max_request_body_bytes": 1048576,

    // Maximum time in seconds spent reading an HTTP request.
    // Minimum: 1. Default: 15.
    "read_timeout_seconds": 15,

    // Maximum time in seconds spent writing an HTTP response.
    // Minimum: 1. Default: 15.
    "write_timeout_seconds": 15,

    // Idle timeout in seconds for HTTP keep-alive connections.
    // Minimum: 1. Default: 20.
    "keep_alive_timeout_seconds": 20,

    // Optional HTTP API authentication settings. Default: disabled.
    "authentication": {
      "enabled": false
    },

    // Exact HTTP or HTTPS origins allowed to call the authenticated API.
    // Omit this object when no CORS origins are needed.
    "cors": {
      "allowed_origins": ["https://router.example"]
    }
  },

  // All supported outbound types.
  // Tags are referenced by route rules, DNS server detours, and list detours.
  // "type" is required and supports: interface, table, blackhole, ignore,
  // urltest, and icmptest.
  "outbounds": [
    {
      // "interface" sends traffic through a specific network interface.
      "type": "interface",

      // Unique outbound tag.
      // Default: no default, required.
      "tag": "vpn",

      // Egress interface name.
      // Default: no default, required for type="interface".
      "interface": "wg0",

      // Optional IPv4 gateway for the interface outbound.
      // Default: null
      "gateway": "10.8.0.1",

      // Optional IPv6 gateway for the interface outbound.
      // Default: null
      "gateway6": "2001:db8::1",

      // Per-outbound strict-enforcement override.
      // Overrides daemon.strict_enforcement when set.
      // Default: inherit daemon.strict_enforcement.
      "strict_enforcement": true,

      // Per-outbound terminal action override for strict enforcement.
      // Supported values: "unreachable" or "blackhole".
      // Default: inherit daemon.strict_enforcement_action.
      "strict_enforcement_action": "unreachable"
    },
    
    {
      // Another interface outbound, often used for the normal WAN path.
      "type": "interface",
      "tag": "wan",
      "interface": "eth0",
      // Set a gateway only when it is stable. For a dynamic WAN gateway, use a
      // table outbound with table=254 (the main routing table) instead.
      "gateway": "172.12.33.1"
    },
    
    {
      // "table" reuses an existing kernel routing table.
      "type": "table",

      // Unique outbound tag.
      "tag": "wan_as_table",

      // Existing Linux routing table ID.
      // Default: no default, required for type="table".
      "table": 254
    },

    {
      // "blackhole" drops matching traffic.
      "type": "blackhole",
      
      // Unique outbound tag.
      "tag": "block"
    },

    {
      // "ignore" lets matching traffic bypass keen-pbr handling
      // and continue with the system's normal routing.
      "type": "ignore",

      // Unique outbound tag.
      "tag": "direct"
    },

    {
      // "urltest" probes several candidate outbounds and selects one automatically.
      "type": "urltest",

      // Unique outbound tag.
      "tag": "auto_select",

      // Probe URL used for latency / reachability checks.
      // Default: no default, required for type="urltest".
      "url": "https://www.gstatic.com/generate_204",

      // Probe interval in milliseconds.
      // Default: 180000 for urltest; 60000 for icmptest.
      "interval_ms": 180000,

      // Timeout for each individual probe attempt in milliseconds.
      // Default: 5000 for urltest; 1000 for icmptest.
      // Omit this field or set it to null to use the default for the outbound type.
      "probe_timeout_ms": 5000,

      // Do not switch if the new candidate is only slightly better than the current one.
      // Minimum: 0 ms. Default: 100 for urltest.
      "tolerance_ms": 100,

      // Selection strategy: "priority" (default) keeps one selected child.
      // nftables-only "balance" spreads new connections equally over usable
      // children in the first healthy lowest-weight group.
      "strategy": "priority",

      // Compatibility field for older configs.
      // Urltest always appends terminal IPv4/IPv6 unreachable routes as a kill-switch.
      // This setting currently has no additional effect for urltest outbounds.
      "strict_enforcement": true,

      // Handling of established conntrack flows when a healthy child changes.
      // Supported values: "preserve" (default) or "delete". Failed selected
      // paths are always cleaned up.
      "conntrack_on_switch": "preserve",

      // Ordered outbound groups.
      // Default: no default, required for type="urltest".
      // Lower weight is preferred before higher weight.
      "outbound_groups": [
        {
          // Relative priority of this group.
          // Default: 1.
          "weight": 1,

          // Candidate outbound tags inside this group.
          // Supported child types: interface, table, blackhole.
          "outbounds": ["vpn", "wan_as_table"]
        },
        {
          // If the first group is unhealthy, keen-pbr can fall back to this one.
          "weight": 2,
          "outbounds": ["wan", "block"]
        }
      ],

      // Probe retry behavior.
      "retry": {
        // Retry attempts before a probe is treated as failed.
        // Default: 3.
        "attempts": 3,

        // Delay between retries in milliseconds.
        // Default: 1000.
        "interval_ms": 1000
      },

      // Circuit breaker settings for unstable outbounds.
      "circuit_breaker": {
        // Consecutive failures before the circuit opens.
        // Minimum: 1; maximum: 20. Default: 5.
        "failure_threshold": 5,

        // Consecutive successes needed to close the circuit again.
        // Minimum: 1; maximum: 20. Default: 2.
        "success_threshold": 2,

        // Cooldown before moving from open to half-open.
        // Minimum: 1 ms; maximum: 86400000 ms. Default: 30000.
        "timeout_ms": 30000,

        // Max probe requests allowed while half-open.
        // Minimum: 1; maximum: 10. Default: 1.
        "half_open_max_requests": 1
      }
    },

    {
      // "icmptest" sends ICMP Echo probes through candidate outbounds and
      // selects a healthy candidate automatically.
      "type": "icmptest",
      "tag": "auto_ping",

      // Probe interval in milliseconds. Default: 60000.
      "interval_ms": 60000,

      // ICMP Echo requests per candidate run. Minimum: 1; maximum: 10.
      // Default: 3.
      "count": 3,

      // Maximum failed packets allowed in a successful run. Minimum: 0.
      // It must be less than count. Default: 0.
      "max_failed": 0,

      // Pause between sequential ICMP attempts. Minimum: 100 ms; maximum:
      // 1000 ms. Default: 200.
      "packet_interval_ms": 200,

      // Timeout for each ICMP attempt. Runtime range: 100-5000 ms.
      // Default: 1000.
      "probe_timeout_ms": 1000,

      // Replies slower than this are failures. Minimum: 1 ms and it must not
      // exceed probe_timeout_ms. Default: 500.
      "max_rtt_ms": 500,

      // Do not switch when the new candidate is only slightly better.
      // Minimum: 0 ms. Default: 10.
      "tolerance_ms": 10,

      // Preserve or delete established conntrack flows when a healthy child
      // changes. Supported values: "preserve" (default) or "delete".
      "conntrack_on_switch": "preserve",

      // Ordered groups of explicit outbound/target pairs. Required for
      // type="icmptest". Every candidate requires both fields below.
      "outbound_groups": [
        {
          "weight": 1,
          "candidates": [
            {
              // Interface or table outbound tag.
              "outbound": "vpn",
              // Literal IPv4 or IPv6 address to ping through that outbound.
              "target": "1.1.1.1"
            },
            {
              "outbound": "wan",
              "target": "9.9.9.9"
            }
          ]
        }
      ]
    }
  ],

  // Lists may use one or more of these sources: url, domains, ip_cidrs, file.
  "lists": {

    "inline_domains": {
      // Inline domains.
      // Domains match the domain itself and its subdomains.
      // Wildcards such as "*.example.com" are optional: "example.com" has the
      // same effect.
      "domains": ["example.com", "othersite.net"],

      // Time in milliseconds that IPs resolved by dnsmasq for these domains
      // remain in the dynamic set. 0 keeps them indefinitely.
      // Set this higher than dnsmasq max-cache-ttl (which is in seconds), with
      // a safety margin. For max-cache-ttl=300, use at least 2100000 (35 min).
      // Default: 0 (no timeout); this example uses 24 hours.
      "ttl_ms": 86400000
    },

    "inline_ips": {
      // Inline IPv4/IPv6 addresses or CIDRs.
      "ip_cidrs": [
        "93.184.216.34",
        "10.0.0.0/8",
        "2001:db8::1",
        "2001:db8::/32"
      ]
    },

    "remote_list": {
      // Remote list URL.
      "url": "https://raw.githubusercontent.com/v2fly/domain-list-community/refs/heads/master/data/apple",

      // Optional outbound used to download this list.
      // Supported detour targets are routable outbounds such as interface, table, or urltest.
      // Default: null (use the system's normal routing)
      "detour": "auto_select",

      // Time in milliseconds that IPs resolved by dnsmasq for these domains
      // remain in the dynamic set. 0 keeps them indefinitely.
      // Set this higher than dnsmasq max-cache-ttl (which is in seconds), with
      // a safety margin. For max-cache-ttl=300, use at least 2100000 (35 min).
      // Default: 0 (no timeout); this example uses 24 hours.
      "ttl_ms": 86400000
    },
  
    "local_file_list": {
      // Local list file path.
      "file": "/etc/keen-pbr/local.lst",
      
      // Time in milliseconds that IPs resolved by dnsmasq for these domains
      // remain in the dynamic set. 0 keeps them indefinitely.
      // Set this higher than dnsmasq max-cache-ttl (which is in seconds), with
      // a safety margin. For max-cache-ttl=300, use at least 2100000 (35 min).
      // Default: 0 (no timeout); this example uses 24 hours.
      "ttl_ms": 86400000
    },

    "mixed_sources": {
      // keen-pbr currently supports combining multiple sources in one list.
      // This is supported, but separate lists are usually easier to maintain.
      "domains": ["intranet.example"],
      "ip_cidrs": ["192.168.50.0/24"],
      "file": "/etc/keen-pbr/mixed.lst",
      "url": "https://example.com/mixed.lst",
      "detour": "vpn",
      // ttl_ms applies to domain sources in this list. See the inline_domains
      // example above for sizing guidance.
      "ttl_ms": 86400000
    }
  },

  // DNS configuration.
  // dns.system_resolver is required for daemon runtime.
  "dns": {
    // Resolver used for runtime integration and TXT health checks.
    // Default: no default, required by the running daemon.
    "system_resolver": {
      "address": "127.0.0.1"
    },

    // Optional built-in DNS probe server for the Web UI and troubleshooting.
    // To check that this computer sends DNS queries through keen-pbr, run:
    // > nslookup check.keen.pbr
    // The response should contain answer_ipv4 (127.0.0.88).
    "dns_test_server": {
      // IPv4 listen address in host:port form.
      // Default: no default, required when dns_test_server is present.
      "listen": "127.0.0.88:12153",

      // IPv4 A-record answer returned by the probe server.
      // Default: the host part of listen.
      "answer_ipv4": "127.0.0.88"
    },

    // All supported DNS server styles.
    "servers": [
      {
        // Plain static DNS server with no detour.
        "tag": "google_dns",

        // Supported values: "static" (default) or "keenetic".
        // Default: "static".
        "type": "static",

        // Address for static DNS servers.
        // Default: no default, required for type="static".
        "address": "8.8.8.8"
      },

      {
        // Static DNS server reached through an interface outbound.
        "tag": "vpn_dns",
        "address": "10.8.0.1:5353",

        // Optional outbound used to contact this DNS server.
        // Supported detour targets: interface, table, urltest.
        // Not allowed: blackhole, ignore.
        // Default: use the system's normal routing.
        "detour": "vpn"
      },

      {
        // Static DNS server reached through a urltest outbound.
        "tag": "auto_dns",
        "address": "[2606:4700:4700::1111]:53",
        "detour": "auto_select"
      },

      {
        // Reuse the router's built-in DNS settings through RCI.
        // Configure DoT/DoH on the router first, then add this Keenetic DNS server.
        // Available only on Keenetic and Netcraze routers.
        "tag": "keenetic_dns",
        "type": "keenetic"
      }
    ],

    // Domain-to-DNS-server rules.
    "rules": [
      {
        // Whether this DNS rule is active.
        // Default: true when omitted or set to null.
        "enabled": true,

        // Lists whose domains should be resolved by this server.
        "list": ["inline_domains", "remote_list"],

        // DNS server tag to use.
        "server": "vpn_dns",

        // Allow answers that resolve to private/local IP ranges.
        // Default: false.
        "allow_domain_rebinding": false
      },

      {
        // Example rule for local services that intentionally resolve to RFC1918 addresses.
        "list": ["mixed_sources"],
        "server": "keenetic_dns",
        "allow_domain_rebinding": true
      },

      {
        // Example of a disabled DNS rule kept for later use.
        "enabled": false,
        "list": ["inline_domains"],
        "server": "google_dns"
      }
    ],

    // Upstream DNS servers used when no DNS rule matches.
    // Default: no upstream servers.
    // WARNING: if you don't provide at least one DNS server here, your Internet connectivity may fail.
    "fallback": ["google_dns", "auto_dns", "keenetic_dns"]
  },

  // Firewall mark allocation.
  // This section is optional.
  "fwmark": {
    // First fwmark assigned to routable outbounds as a hex string.
    // Default: "0x00010000".
    "start": "0x00010000",

    // Fwmark bitmask as a hex string.
    // Must contain one or more consecutive F nibbles.
    // Default: "0x00FF0000".
    "mask": "0x00FF0000"
  },

  // Policy-routing table allocation.
  // This section is optional.
  "iproute": {
    // First routing table ID used for auto-allocated outbound tables.
    // Default: 150.
    // Avoid reserved IDs such as 128 and 250-260.
    "table_start": 150,

    // First policy-routing rule priority to allocate.
    // Default: null, which inherits table_start.
    "rule_priority_start": null
  },

  // Route-processing rules.
  "route": {
    // Optional ingress interface filter.
    // If omitted or empty, keen-pbr processes packets from any interface.
    // Default: no filter.
    "inbound_interfaces": ["br0", "wg-lan"],

    "rules": [
      {
        // Basic list-based routing rule.
        // Default for enabled: true when omitted or set to null.
        "list": ["inline_domains", "remote_list"],
        "outbound": "auto_select"
      },

      {
        // Route inline IPs through an existing routing table.
        "list": ["inline_ips"],
        "outbound": "wan_as_table"
      },

      {
        // Full filter example using DSCP, proto, source/destination address, and destination port.
        "enabled": true,

        // Match traffic only if dest domain/IP is in the list
        "list": ["mixed_sources"],

        // Match traffic only if protocol is TCP
        // Possible values: null (for any protocol), tcp, udp, tcp/udp
        "proto": "tcp",

        // Match traffic only if DSCP tag equals this value.
        // Supported: integer from 1 to 63.
        "dscp": 46,

        // Match traffic only if source IP matches
        // Supported: single IP, CIDR
        "src_addr": "192.168.10.0/24,192.168.20.0/24",

        // Match traffic only if dest IP matches
        // Supported: single IP, CIDR
        "dest_addr": "203.0.113.0/24",

        // nftables-only catch-all for non-local, non-connected IPv4 traffic.
        // Keep default_gateway rules last and add a separate IPv6 rule.
        "default_gateway": "ipv4",

        // Match traffic only if source port matches
        // Supported: single port, multiple port comma-separated, range
        "src_port": "1024-65535",

        // Match traffic only if dest IP matches
        // Supported: single port, multiple port comma-separated, range
        "dest_port": "443,8443",

        // Route all matched traffic to this outbound
        "outbound": "vpn"
      },

      {
        // Rules may omit "list" if another condition is present.
        // This example blocks DNS to any destination except the trusted resolver subnet.
        "proto": "udp",
        "dest_port": "53",
        "dest_addr": "!10.10.0.0/16",
        "outbound": "block"
      },

      {
        // Example "ignore" rule used as an exception before broader VPN rules.
        "src_addr": "192.168.1.0/24",
        "dest_addr": "192.168.0.0/16",
        "outbound": "direct"
      },

      {
        // IPv6 example: route HTTPS traffic to a documentation subnet via VPN.
        "proto": "tcp",
        "src_addr": "2001:db8:10::/64",
        "dest_addr": "2001:db8:203::/48",
        "dest_port": "443",
        "outbound": "vpn"
      },

      {
        // Example disabled rule kept for later use.
        "enabled": false,
        "list": ["inline_domains"],
        "proto": "tcp/udp",
        "dest_port": "!80,443",
        "outbound": "wan"
      }
    ]
  },

  // Automatic refresh for URL-backed lists.
  // This section is optional.
  "lists_autoupdate": {
    // Enable or disable periodic refresh.
    // Default: false.
    "enabled": true,

    // Standard 5-field cron expression.
    // Required when enabled=true.
    // No default value.
    "cron": "0 4 * * 0"
  }
}
```

## Notes

- `dns.servers[].detour` supports `interface`, `table`, and `urltest` outbounds, but not `blackhole` or `ignore`.
- `lists[].detour` is useful when a remote list should be downloaded through a VPN or other non-default path.
- `route.rules[]` must include at least one matching condition: `list`, `dscp`, `src_port`, `dest_port`, `src_addr`, or `dest_addr`.
- `dns.rules[].allow_domain_rebinding` is mainly for internal domains that intentionally resolve to private IP ranges.
