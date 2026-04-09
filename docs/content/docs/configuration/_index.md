---
title: Configuration
weight: 3
---

keen-pbr is configured via a JSON file. The default path is set at build time and overridable with `--config` (`/etc/keen-pbr/config.json` on OpenWrt builds, `/opt/etc/keen-pbr/config.json` on Keenetic builds).

## Top-Level Keys

| Key | Type | Description |
|---|---|---|
| `daemon` | object | PID file and cache directory settings |
| `api` | object | HTTP API listen address and enable flag |
| `outbounds` | array | Outbound connection definitions |
| `lists` | object | Named domain/IP list definitions |
| `route` | object | Route rules and fallback outbound |
| `dns` | object | DNS servers and routing rules |
| `fwmark` | object | Firewall mark range settings |
| `iproute` | object | Routing table ID allocation |
| `lists_autoupdate` | object | Automatic list refresh schedule |

All top-level keys are optional, but `outbounds`, `lists`, and `route` are needed for any meaningful routing.

## Complete Example

The following is the full annotated example configuration:

```json
{
  "daemon": {
    "pid_file": "/var/run/keen-pbr.pid",
    "cache_dir": "/var/cache/keen-pbr"
  },

  "api": {
    "enabled": true,
    "listen": "127.0.0.1:8080"
  },

  "outbounds": [
    {
      "type": "interface",
      "tag": "vpn",
      "interface": "tun0",
      "gateway": "10.8.0.1"
    },
    {
      "type": "interface",
      "tag": "wan",
      "interface": "eth0",
      "gateway": "192.168.1.1"
    },
    {
      "type": "table",
      "tag": "custom-table",
      "table": 200
    },
    {
      "type": "blackhole",
      "tag": "block"
    },
    {
      "type": "ignore",
      "tag": "direct"
    },
    {
      "type": "urltest",
      "tag": "auto-select",
      "url": "https://www.gstatic.com/generate_204",
      "interval_ms": 180000,
      "tolerance_ms": 100,
      "outbound_groups": [
        { "weight": 1, "outbounds": ["vpn"] },
        { "weight": 2, "outbounds": ["wan"] }
      ],
      "retry": { "attempts": 3, "interval_ms": 1000 },
      "circuit_breaker": {
        "failure_threshold": 5,
        "success_threshold": 2,
        "timeout_ms": 30000,
        "half_open_max_requests": 1
      }
    }
  ],

  "lists": {
    "my_domains": {
      "domains": ["example.com", "*.example.org"]
    },
    "my_ips": {
      "ip_cidrs": ["93.184.216.34", "10.0.0.0/8"]
    },
    "remote_list": {
      "url": "https://raw.githubusercontent.com/v2fly/domain-list-community/refs/heads/master/data/apple"
    },
    "local_list": {
      "file": "./my-list.txt"
    }
  },

  "dns": {
    "system_resolver": {
      "type": "dnsmasq-nftset",
      "address": "127.0.0.1"
    },
    "servers": [
      { "tag": "vpn-dns", "address": "10.8.0.1" },
      { "tag": "google-dns", "address": "8.8.8.8" }
    ],
    "rules": [
      { "list": ["my_domains", "remote_list"], "server": "vpn-dns" }
    ],
    "fallback": ["google-dns", "quad9"]
  },

  "fwmark": {
    "start": "0x00010000",
    "mask": "0x00FF0000"
  },

  "iproute": {
    "table_start": 150
  },

  "lists_autoupdate": {
    "enabled": true,
    "cron": "0 4 * * *"
  },

  "route": {
    "rules": [
      { "list": ["my_domains", "my_ips", "remote_list"], "outbound": "vpn" },
      { "list": ["local_list"], "outbound": "auto-select" }
    ],
    "fallback": "wan"
  }
}
```

{{< callout type="info" >}}
List names must be 1-24 characters, use only `a-z`, `A-Z`, `0-9`, and `_`, and the first character must be a letter (`[a-zA-Z][a-zA-Z0-9_]{0,23}`).
{{< /callout >}}

## Sections

- [Outbounds](outbounds/) — interface, table, blackhole, ignore, urltest
- [Lists](lists/) — domain and IP list definitions
- [Route Rules](route-rules/) — traffic matching and routing
- [DNS](dns/) — DNS server routing and dnsmasq integration
- [Advanced](advanced/) — daemon, fwmark, iproute, lists_autoupdate, api
