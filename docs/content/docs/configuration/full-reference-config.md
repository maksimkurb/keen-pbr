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
  // Global daemon settings.
  // All fields in this section are optional.
  "daemon": {
    // Path to the PID file.
    // Default: no PID file is written when omitted.
    "pid_file": "/var/run/keen-pbr.pid",

    // Directory used for remote-list cache files and cache metadata.
    // Default: (shown below)
    "cache_dir": "/var/cache/keen-pbr",

    // Firewall backend selection.
    // Supported values: "auto", "iptables", "nftables".
    // Default: (shown below)
    "firewall_backend": "auto",

    // Skip packets that already have an fwmark before keen-pbr touches them.
    // Default: (shown below)
    "skip_marked_packets": true,

    // Default strict routing behavior for interface and urltest outbounds.
    // Default: (shown below)
    "strict_enforcement": false,

    // Maximum allowed size for downloaded remote content such as URL-backed lists.
    // Default: (shown below)
    "max_file_size_bytes": 8388608,

    // Maximum stdout bytes captured per firewall verification command.
    // Use 0 for unlimited capture.
    // Default: (shown below)
    "firewall_verify_max_bytes": 262144
  },

  // Embedded HTTP API and Web UI settings.
  // WARNING: This section is NOT available in keen-pbr-headless package
  "api": {
    // Enable or disable the HTTP API / Web UI.
    // Default: false.
    "enabled": true,

    // Listen address for the API server.
    // Default: (shown below)
    "listen": "0.0.0.0:12121"
  },

  // All supported outbound types.
  // Tags are referenced by route rules, DNS server detours, and list detours.
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

      // Optional gateway for the interface outbound.
      // Default: null
      "gateway": "10.8.0.1",

      // Per-outbound strict-enforcement override.
      // Overrides daemon.strict_enforcement when set.
      // Default: inherit daemon.strict_enforcement.
      "strict_enforcement": true
    },
    
    {
      // Another interface outbound example, often used as the normal WAN path.
      "type": "interface",
      "tag": "wan",
      "interface": "eth0",
      // Set the gateway only if you are sure it will not change in the future
      // Better way is to create an outbound with table=254 which corresponds to default (main) routing table
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
      // Default: (shown below)
      "interval_ms": 180000,

      // Do not switch if the new candidate is only slightly better than the current one.
      // Default: (shown below)
      "tolerance_ms": 100,

      // Optional strict-enforcement override for the generated urltest routing table.
      // Default: inherit daemon.strict_enforcement.
      "strict_enforcement": true,

      // Ordered outbound groups.
      // Default: no default, required for type="urltest".
      // Lower weight is preferred before higher weight.
      "outbound_groups": [
        {
          // Relative priority of this group.
          // Default: (shown below)
          "weight": 1,

          // Candidate outbound tags inside this group.
          // Supported child types: interface, table, blackhole.
          "outbounds": ["vpn", "custom_table"]
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
        // Default: (shown below)
        "attempts": 3,

        // Delay between retries in milliseconds.
        // Default: (shown below)
        "interval_ms": 1000
      },

      // Circuit breaker settings for unstable outbounds.
      "circuit_breaker": {
        // Consecutive failures before the circuit opens.
        // Default: (shown below)
        "failure_threshold": 5,

        // Consecutive successes needed to close the circuit again.
        // Default: (shown below)
        "success_threshold": 2,

        // Cooldown before moving from open to half-open.
        // Default: (shown below)
        "timeout_ms": 30000,

        // Max probe requests allowed while half-open.
        // Default: (shown below)
        "half_open_max_requests": 1
      }
    }
  ],

  // All supported list styles.
  // Each list must provide at least one of: url, domains, ip_cidrs, file.
  "lists": {

    "inline_domains": {
      // Inline domains.
      // Domains match the domain itself and its subdomains.
      // Wildcards like "*.example.com" are accepted, but not required. Behavior will be the same as if you enter "example.com"
      "domains": ["example.com", "othersite.net"],

      // How long dnsmasq-resolved IPs for these domains stay in the dynamic set.
      // 0 means no timeout.
      // Should be higher than dnsmasq max-cache-ttl option. 
      // NOTE: dnsmasq max-cache-ttl is in seconds, but ttl_ms here is in milliseconds. So if your max-cache-ttl=300, I would recommend you to set ttl_ms at least to (max-cache-ttl * 1000 + 30min) = 2100000 (35 min)
      // Default: value shown below (24 hours).
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

      // No need to set ttl_ms if your list doesn't contain any domains
      // "ttl_ms": 0
    },

    "remote_list": {
      // Remote list URL.
      "url": "https://raw.githubusercontent.com/v2fly/domain-list-community/refs/heads/master/data/apple",

      // Optional outbound used to download this list.
      // Supported detour targets are routable outbounds such as interface, table, or urltest.
      // Default: null (use the system's normal routing)
      "detour": "auto_select",

      // How long dnsmasq-resolved IPs for these domains stay in the dynamic set.
      // 0 means no timeout.
      // Should be higher than dnsmasq max-cache-ttl option. 
      // NOTE: dnsmasq max-cache-ttl is in seconds, but ttl_ms here is in milliseconds. So if your max-cache-ttl=300, I would recommend you to set ttl_ms at least to (max-cache-ttl * 1000 + 30min) = 2100000 (35 min)
      // Default: value shown below (24 hours).
      "ttl_ms": 86400000
    },
  
    "local_file_list": {
      // Local list file path.
      "file": "/etc/keen-pbr/local.lst",
      
      // How long dnsmasq-resolved IPs for these domains stay in the dynamic set.
      // 0 means no timeout.
      // Should be higher than dnsmasq max-cache-ttl option. 
      // NOTE: dnsmasq max-cache-ttl is in seconds, but ttl_ms here is in milliseconds. So if your max-cache-ttl=300, I would recommend you to set ttl_ms at least to (max-cache-ttl * 1000 + 30min) = 2100000 (35 min)
      // Default: value shown below (24 hours).
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

    // Optional built-in DNS probe server used by the Web UI and troubleshooting tools.
    // You can test if your PC has correct DNS configuration by running the following command:
    // > nslookup check.keen.pbr
    // This command should return answer_ipv4 IP address (127.0.0.88), what means that your PC DNS configuration is correct and keen-pbr can see your DNS requests
    "dns_test_server": {
      // IPv4 listen address in host:port form.
      // Default: no default, required when dns_test_server is present.
      "listen": "127.0.0.88:12153",

      // IPv4 A-record answer returned by the probe server.
      // Default: value shown below if omitted from this example.
      "answer_ipv4": "127.0.0.88"
    },

    // All supported DNS server styles.
    "servers": [
      {
        // Plain static DNS server with no detour.
        "tag": "google_dns",

        // Supported values: "static" (default) or "keenetic".
        // Default: value shown below when omitted.
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
        // Keenetic DNS source that reuses the router's built-in DNS settings via RCI.
        // This is the easiest way to configure DoT/DoH on Keenetic. Just configure DoT/DoH in your router and add DNS with Keenetic type to the keen-pbr.
        // WARNING: This option is supported only on Keenetic and Netcraze routers
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
        // Default: (shown below)
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
        "server": "table_dns"
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
    // Default: (shown below)
    "start": "0x00010000",

    // Fwmark bitmask as a hex string.
    // Must contain one or more consecutive F nibbles.
    // Default: (shown below)
    "mask": "0x00FF0000"
  },

  // Policy-routing table allocation.
  // This section is optional.
  "iproute": {
    // First routing table ID used for auto-allocated outbound tables.
    // Default: (shown below)
    // Avoid reserved IDs such as 128 and 250-260.
    "table_start": 150
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
        "outbound": "custom_table"
      },

      {
        // Full filter example using proto, source/destination address, and destination port.
        "enabled": true,

        // Match traffic only if dest domain/IP is in the list
        "list": ["mixed_sources"],

        // Match traffic only if protocol is TCP
        // Possible values: null (for any protocol), tcp, udp, tcp/udp
        "proto": "tcp",

        // Match traffic only if source IP matches
        // Supported: single IP, CIDR
        "src_addr": "192.168.10.0/24,192.168.20.0/24",

        // Match traffic only if dest IP matches
        // Supported: single IP, CIDR
        "dest_addr": "203.0.113.0/24",

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
    "cron": "0 4 * * *"
  }
}
```

## Notes

- `dns.servers[].detour` supports `interface`, `table`, and `urltest` outbounds, but not `blackhole` or `ignore`.
- `lists[].detour` is useful when a remote list should be downloaded through a VPN or other non-default path.
- `route.rules[]` must include at least one matching condition: `list`, `src_port`, `dest_port`, `src_addr`, or `dest_addr`.
- `dns.rules[].allow_domain_rebinding` is mainly for internal domains that intentionally resolve to private IP ranges.
