---
title: Full Reference Config
weight: 6
---

This page shows one full configuration example with comments, including platform-specific settings marked in the example. Remove settings that are unsupported on your target platform before applying it.

You can write `//` comments in `config.json`. Replace sample interfaces, addresses, and file paths with values from your network before applying it.

{{< callout type="info" >}}
List names, outbound tags, and DNS server tags must match `^[a-z][a-z0-9_]*$` and must be at most 24 characters.
{{< /callout >}}

```json {filename="config.json"}
{
  // keen-pbr version this config was written for (X.Y.Z).
  // Set automatically when you save from the Web UI; keep it when editing by hand.
  "version": "3.4.0",

  // Optional device label shown in the browser title and beneath the keen-pbr logo.
  // Maximum length: 128 characters. Default: empty (the standard keen-pbr
  // branding is used).
  "device_name": "Home router",

  // General keen-pbr service settings.
  // All fields in this section are optional.
  "daemon": {
    // Path to the PID file (the file holding the service's process number).
    // Default: no file is written when omitted.
    "pid_file": "/var/run/keen-pbr.pid",

    // Directory used for remote-list cache files and cache metadata.
    // Default: "/var/cache/keen-pbr".
    "cache_dir": "/var/cache/keen-pbr",

    // Which firewall to use.
    // Supported values: "auto", "iptables", "nftables".
    // Default: "auto".
    "firewall_backend": "auto",

    // Do not process packets that another program has already marked (fwmark).
    // Default: true (also when set to null).
    "skip_marked_packets": true,

    // Clear the dynamic sets (domain IPs from DNS replies and connections) on
    // every config apply or service restart. Default: false (also when set to null).
    "clear_dynamic_sets_on_apply": false,

    // Optional initial hash table size for ipsets created by the iptables backend.
    // Has no effect with nftables. Minimum: 1; maximum: 2147483648. Omit or set
    // to null to use the ipset default (1024).
    // Changing it recreates the sets and empties the dynamic sets.
    "ipset_hashsize": null,

    // Optional maximum element count for ipsets created by the iptables backend.
    // Has no effect with nftables. Minimum: 1; maximum: 4294967295. Omit or set
    // to null to use the ipset default (65536).
    // Changing it recreates the sets and empties the dynamic sets.
    "ipset_maxelem": null,

    // Do not refill list sets on SIGUSR1, urltest/icmptest switches and
    // interface changes; only rules are updated. Default: true.
    "reuse_static_sets_on_runtime_refresh": true,

    // Route IPv6 traffic. Default: true (also when set to null).
    // If the system has no IPv6 support, keen-pbr logs an error and continues
    // in IPv4-only mode.
    "ipv6_enabled": true,

    // Block traffic when an interface outbound is down instead of letting it
    // go out through the normal connection. Default: false.
    "strict_enforcement": false,

    // What to do with traffic when strict_enforcement applies.
    // Supported values: "unreachable" (return a network error) or "blackhole"
    // (silently drop packets). Default: "unreachable".
    "strict_enforcement_action": "unreachable",

    // Maximum allowed size for downloaded remote content such as URL-backed lists.
    // Default: 8388608 bytes (8 MiB).
    "max_file_size_bytes": 8388608,

    // How many bytes of iptables/nft output to read when checking firewall rules
    // (`keen-pbr status`, status page). 0 means no limit.
    // Default: 262144.
    "firewall_verify_max_bytes": 262144,

    // How many seconds to wait for keen-pbr's helper scripts (for example the
    // dnsmasq script) before stopping them. Minimum: 1. Default: 30.
    "exec_timeout_seconds": 30,

    // How many seconds to give a script to exit after it is stopped before it
    // is killed. Minimum: 0. Default: 2.
    "exec_kill_grace_seconds": 2
  },

  // Embedded HTTP API and web UI settings.
  // WARNING: This section is NOT available in keen-pbr-headless package
  "api": {
    // Enable or disable the HTTP API and the web UI.
    // Default: false.
    "enabled": true,

    // Address and port the API server listens on.
    // Default: "0.0.0.0:12121".
    "listen": "0.0.0.0:12121",

    // Maximum request body size in bytes. Minimum: 1024. Default: 1048576.
    "max_request_body_bytes": 1048576,

    // How many seconds to wait for an HTTP request from the client.
    // Minimum: 1. Default: 15.
    "read_timeout_seconds": 15,

    // How many seconds to send an HTTP response to the client.
    // Minimum: 1. Default: 15.
    "write_timeout_seconds": 15,

    // How many seconds to keep a connection open between requests.
    // Minimum: 1. Default: 20.
    "keep_alive_timeout_seconds": 20,

    // Password login for the API and the web UI. Default: disabled.
    // With enabled: true, password_hash is required: create it with
    // `keen-pbr hash-password --update` or in the web UI (Security).
    "authentication": {
      "enabled": false
    },

    // Site addresses (origins) allowed to call the API from a browser.
    // Exact http:// or https:// addresses only, no path or wildcards.
    // Omit this object when no CORS origins are needed.
    "cors": {
      "allowed_origins": ["https://router.example"]
    }
  },

  // All supported outbound types.
  // An outbound is the channel traffic goes through: an interface, a routing
  // table, a block, and so on. Tags are referenced by route rules, DNS server
  // detours, and list detours.
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

      // Optional IPv4 gateway for the interface outbound. Set to "auto" to
      // discover the unique lowest-metric matching default route in the main
      // table; omit it for a gatewayless route.
      // Default: null
      "gateway": "10.8.0.1",

      // Optional IPv6 gateway for the interface outbound. Set to "auto" to
      // discover the unique lowest-metric matching default route in the main
      // table; omit it for a gatewayless route.
      // Default: null
      "gateway6": "2001:db8::1",

      // This outbound's own strict_enforcement value.
      // Takes precedence over daemon.strict_enforcement when set.
      // Default: inherit daemon.strict_enforcement.
      "strict_enforcement": true,

      // This outbound's own value of daemon.strict_enforcement_action.
      // Supported values: "unreachable" or "blackhole".
      // Default: inherit daemon.strict_enforcement_action.
      "strict_enforcement_action": "unreachable"
    },
    
    {
      // Another interface outbound, for example for the normal internet (WAN) connection.
      "type": "interface",
      "tag": "wan",
      "interface": "eth0",
      // Set the gateway explicitly, or use "auto" to detect it from the matching
      // default route in the main table.
      "gateway": "auto"
    },
    
    {
      // "table" reuses an existing kernel routing table.
      "type": "table",

      // Unique outbound tag.
      "tag": "wan_as_table",

      // ID of an existing Linux routing table.
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
      // "ignore" keeps matching traffic on the system's normal routing;
      // keen-pbr does not touch it.
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

      // Selection strategy: "priority" (default) uses one selected link.
      // "balance" (not on Keenetic) spreads new connections over the working
      // links of the first working group in proportion to their weight.
      "strategy": "priority",

      // Kept for compatibility with older configs; it has no effect:
      // urltest always blocks traffic when no link works.
      "strict_enforcement": true,

      // What to do with open conntrack connections when another link is selected.
      // Supported values: "preserve" (default) or "delete". If the selected
      // link stops working, the entries are always deleted.
      "conntrack_on_switch": "preserve",

      // Ordered outbound groups.
      // Default: no default, required for type="urltest".
      // Groups are tried in array order (first group first).
      "outbound_groups": [
        {
          // Members of this group. Supported types: interface, table.
          "members": [
            // "weight" (1-100, default 1) is the share of new connections
            // under strategy "balance"; ignored by "priority".
            { "outbound": "vpn", "weight": 7 },
            { "outbound": "wan_as_table", "weight": 3 }
          ]
        },
        {
          // If the first group does not work, keen-pbr switches to this one.
          // If no link works at all, traffic is blocked.
          "members": [{ "outbound": "wan" }]
        }
      ],

      // Probe retry behavior.
      "retry": {
        // How many times to try the probe, including the first attempt.
        // Default: 1.
        "attempts": 3,

        // Delay between retries in milliseconds.
        // Default: 1000.
        "interval_ms": 1000
      },

      // Circuit breaker settings.
      "circuit_breaker": {
        // The circuit breaker temporarily disables an unstable outbound.
        // How many failures in a row open it (the outbound is disabled).
        // Minimum: 1; maximum: 20. Default: 5.
        "failure_threshold": 5,

        // How many successful probes are needed to return to normal.
        // Minimum: 1; maximum: 20. Default: 2.
        "success_threshold": 2,

        // How many milliseconds after opening the outbound gets a trial probe
        // (half-open). Minimum: 1; maximum: 86400000.
        // Default: 30000 for urltest; for icmptest, the larger of 60000 and interval_ms.
        "timeout_ms": 30000,

        // How many trial probes are allowed while half-open.
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

      // Keep or delete open conntrack connections when another link
      // is selected. Supported values: "preserve" (default) or "delete".
      "conntrack_on_switch": "preserve",

      // Ordered groups (first tried first) of explicit outbound/target
      // members. Required for type="icmptest". Every member requires
      // "outbound" and "target"; "weight" (1-100, default 1) is optional.
      "outbound_groups": [
        {
          "members": [
            {
              // Interface or table outbound tag.
              "outbound": "vpn",
              // IPv4 or IPv6 address to ping through that outbound.
              "target": "1.1.1.1"
            },
            {
              "outbound": "wan",
              "target": "9.9.9.9",
              "weight": 3
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

      // Minimum lifetime of IPs learned through DNS and L7 interception.
      // Omitted, 0, and values below 1000 ms use intercept.min_ttl_ms.
      // Values of 1000 ms or more are truncated to whole seconds. A DNS
      // record TTL may raise the timeout up to intercept.max_ttl_ms. This is
      // separate from the refresh interval for remote lists.
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
      // Interface, table, urltest and icmptest outbounds are supported.
      // Default: null (use the system's normal routing)
      "detour": "auto_select",

      // Minimum lifetime of IPs learned through DNS and L7 interception.
      // Omitted, 0, and values below 1000 ms use intercept.min_ttl_ms.
      // Values of 1000 ms or more are truncated to whole seconds. A DNS
      // record TTL may raise the timeout up to intercept.max_ttl_ms. This is
      // separate from the refresh interval for remote lists.
      "ttl_ms": 86400000
    },
  
    "local_file_list": {
      // Local list file path.
      "file": "/etc/keen-pbr/local.lst",
      
      // Minimum lifetime of IPs learned through DNS and L7 interception.
      // Omitted, 0, and values below 1000 ms use intercept.min_ttl_ms.
      // Values of 1000 ms or more are truncated to whole seconds. A DNS
      // record TTL may raise the timeout up to intercept.max_ttl_ms. This is
      // separate from the refresh interval for remote lists.
      "ttl_ms": 86400000
    },

    "mixed_sources": {
      // You can combine several sources in one list,
      // but separate lists are usually easier to maintain.
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

  // DNS configuration. Interception fills dynamic sets without a resolver.
  "dns": {
    // Optional: per-list DNS servers via dnsmasq.
    // Default: "none" (keen-pbr does not touch the system resolver).
    // Set to "dnsmasq" to resolve domains from specific lists through chosen
    // DNS servers. With "none", dns.rules and fallback are not used and a
    // warning is logged.
    "resolver_integration": "none",

    // Which dnsmasq instance to use: "uci:<section>" (OpenWrt),
    // "entware:default" (Keenetic) or "systemd:<unit>.service" (Debian).
    // Default: empty, the instance is chosen automatically. Fill it in only if
    // `keen-pbr resolver dnsmasq detect` finds several instances.
    "dnsmasq_instance": "uci:cfg01411c",

    // Built-in DNS proxy for lists with several DNS servers or with DoH.
    // Default: 127.0.0.89 and ports from 52100 (up to 32 ports). Change them
    // only if the address or the ports are taken.
    "proxy_address": "127.0.0.89",
    "proxy_port_start": 52100,

    // All supported DNS server styles.
    "servers": [
      {
        // Plain DNS server with a fixed address, no detour.
        "tag": "google_dns",

        // Supported values: "static" (default), "doh" or "keenetic".
        // Default: "static".
        "type": "static",

        // DNS server address (for type="static").
        // Default: no default, required for type="static".
        "address": "8.8.8.8"
      },

      {
        // DNS server reached through an interface outbound.
        "tag": "vpn_dns",
        "address": "10.8.0.1:5353",

        // Optional outbound used to contact this DNS server.
        // Supported: interface, table, urltest, icmptest outbounds.
        // Not allowed: blackhole, ignore.
        // Default: use the system's normal routing.
        "detour": "vpn"
      },

      {
        // DNS server reached through a urltest (or icmptest) outbound.
        "tag": "auto_dns",
        "address": "[2606:4700:4700::1111]:53",
        "detour": "auto_select"
      },

      {
        // DNS server reached over HTTPS (DNS-over-HTTPS). The address must start with https://.
        // The router needs CA certificates installed to check the server.
        // detour works for it like for other servers.
        "tag": "google_doh",
        "type": "doh",
        "address": "https://dns.google/dns-query"
      },

      {
        // [Only on Keenetic and Netcraze platforms]
        // Reuse the router's built-in DNS settings through RCI.
        // Configure DoT/DoH on the router first, then add this Keenetic DNS server.
        "tag": "keenetic_dns",
        "type": "keenetic"
      }

    ],

    // "List domains -> DNS server" rules (only work when resolver_integration: "dnsmasq").
    "rules": [
      {
        // Whether this DNS rule is active. Default: true when omitted or null.
        "enabled": true,

        // Lists whose domains should be resolved by this server.
        "list": ["inline_domains"],

        // Tags of the DNS servers to use. With several, requests go to them
        // in turn; when one does not answer, the next one takes over.
        "servers": ["vpn_dns"],

        // Allow answers that resolve to private/local IP ranges.
        // Default: false.
        "allow_domain_rebinding": false
      }
    ],

    // DNS servers for domains that match no rule
    // (only work when resolver_integration: "dnsmasq").
    // Default: empty (servers from resolv.conf are used).
    "fallback": ["google_dns"]
  },

  // Traffic interception. All fields below have defaults. If the kernel lacks
  // something, only that part (DNS or L7) is turned off; see the status page.
  "intercept": {
    // Routing by domain. When false, domain rules do not work;
    // IP and CIDR rules still do.
    "enabled": true,

    // Minimum and maximum lifetime of an address in a set, in milliseconds.
    // At least 1000, min_ttl_ms must not exceed max_ttl_ms; values are rounded to seconds.
    "min_ttl_ms": 300000,
    "max_ttl_ms": 86400000,
    "dns": {
      // Read plain (unencrypted) DNS replies on port 53.
      "enabled": true,

      // NFQUEUE queue number (1-65535, except 64511 and 65023: Keenetic uses them).
      "queue_num": 9053,

      // How many milliseconds to hold a DNS reply while the address is added to the set (5-500).
      "hold_timeout_ms": 30,
      "marker": {
        // Domain and IPv4 answer used to check that DNS interception works.
        "domain": "check.keen-pbr.fyi",
        "answer_ipv4": "127.0.0.88"
      }
    },
    "l7": {
      // Learn addresses from the connections themselves: the TLS name (SNI), the HTTP Host header, QUIC.
      "enabled": true,

      // NFLOG group number (1-65535, except 64511 and 65023).
      "nflog_group": 9054,

      // Which protocols to parse.
      "tls": true,
      "http": true,
      "quic": true
    }
  },

  // Range of fwmark values.
  // This section is optional.
  "fwmark": {
    // First fwmark (packet label) assigned to outbounds, as a hex string.
    // Default: "0x00010000".
    "start": "0x00010000",

    // Fwmark bitmask as a hex string.
    // Must contain one or more consecutive F nibbles.
    // Default: "0x00FF0000".
    "mask": "0x00FF0000"
  },

  // Routing table numbers and `ip rule` priorities.
  // This section is optional.
  "iproute": {
    // First routing table ID used for auto-allocated outbound tables.
    // Default: 150.
    // Do not use reserved IDs: 0, 128, 250-260 and everything from 32000.
    "table_start": 150,

    // First priority of the `ip rule` entries.
    // Default: null, which inherits table_start.
    "rule_priority_start": null,

    // Apply route rules to, and learn domains from, the router's own traffic.
    // Default: false (only traffic of devices in the network; DNS server detour works either way).
    "process_router_traffic": false
  },

  // Routing rules.
  "route": {
    // Optional ingress interface filter.
    // If omitted or empty, keen-pbr processes packets from any interface.
    // Default: no filter.
    "inbound_interfaces": ["br0", "wg-lan"],

    "rules": [
      {
        // Simple rule: traffic to domains and IPs from the lists goes to the outbound.
        // enabled defaults to true when omitted or set to null.
        "list": ["inline_domains", "remote_list"],
        "outbound": "auto_select"
      },

      {
        // Send addresses from a list through an existing routing table.
        "list": ["inline_ips"],
        "outbound": "wan_as_table"
      },

      {
        // Example rule with every condition: DSCP, protocol, addresses and ports.
        "enabled": true,

        // Only if the destination domain or IP is in the list
        "list": ["mixed_sources"],

        // Only if the protocol is TCP
        // Possible values: null (for any protocol), tcp, udp, tcp/udp
        "proto": "tcp",

        // Only if the DSCP tag (traffic priority) equals this value.
        // Integer from 1 to 63.
        "dscp": 46,

        // Only if the source IP matches.
        // One IP or CIDR; several separated by commas. A leading `!` inverts the condition.
        "src_addr": "192.168.10.0/24,192.168.20.0/24",

        // Only if the destination IP matches. Same format as src_addr.
        "dest_addr": "203.0.113.0/24",

        // [Only for nftables when combining src_port with a dest_port list]
        // On iptables, remove src_port when dest_port contains a port list.
        // Only if the source port matches.
        // One port, several separated by commas, or a range
        "src_port": "1024-65535",

        // Only if the destination port matches.
        // One port, several separated by commas, or a range; a leading `!` inverts the condition
        "dest_port": "443,8443",

        // Send matched traffic to this outbound
        "outbound": "vpn"
      },

      {
        // "list" can be omitted if another condition is present.
        // This example blocks DNS requests to any address except the trusted subnet.
        "proto": "udp",
        "dest_port": "53",
        "dest_addr": "!10.10.0.0/16",
        "outbound": "block"
      },

      {
        // Example exception: an "ignore" outbound placed before broader VPN rules.
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
      },

      {
        // [Only for nftables]
        // "Everything else" rule for outgoing IPv4 traffic. Keep default_gateway rules
        // last. Traffic of devices in the network is covered by default; to include
        // the router's own traffic, set iproute.process_router_traffic to true.
        "default_gateway": "ipv4",
        "outbound": "auto_select"
      },

      {
        // [Only for nftables]
        // "Everything else" rule for outgoing IPv6 traffic. Keep it last.
        "default_gateway": "ipv6",
        "outbound": "auto_select"
      }
    ]
  },

  // Automatic refresh for URL-backed lists.
  // This section is optional.
  "lists_autoupdate": {
    // Enable or disable periodic refresh.
    // Default: false.
    "enabled": true,

    // Schedule: a 5-field cron expression.
    // Required when enabled=true.
    // No default value.
    "cron": "0 4 * * 0"
  }
}
```

## Notes

- `dns.servers[].detour` supports `interface`, `table`, `urltest`, and `icmptest` outbounds, but not `blackhole` or `ignore`.
- `lists[].detour` is useful when a remote list should be downloaded through a VPN or other non-default path.
- `route.rules[]` must include at least one matching condition: `list`, `dscp`, `src_port`, `dest_port`, `src_addr`, `dest_addr`, or `default_gateway`.
- `dns.rules[].allow_domain_rebinding` is mainly for internal domains that intentionally resolve to private IP ranges.
