---
title: Quick Start
weight: 2
---

This guide walks you through a minimal working configuration that routes traffic from a domain list through a VPN interface.

## Minimal Configuration

Create `/etc/keen-pbr3/config.json`:

```json
{
  "daemon": {
    "pid_file": "/var/run/keen-pbr3.pid",
    "cache_dir": "/var/cache/keen-pbr3"
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
    }
  ],
  "lists": {
    "my-domains": {
      "domains": ["example.com", "*.example.org"]
    }
  },
  "route": {
    "rules": [
      {
        "list": ["my-domains"],
        "outbound": "vpn"
      }
    ],
    "fallback": "wan"
  }
}
```

This config:
- Defines two outbounds: `vpn` (tun0) and `wan` (eth0)
- Creates a list `my-domains` with two inline domain entries
- Routes all traffic matching `my-domains` through `vpn`
- Falls back to `wan` for everything else

## Run the Daemon

```bash
keen-pbr3 --config /etc/keen-pbr3/config.json
```

Add `-d` to run as a background daemon:

```bash
keen-pbr3 --config /etc/keen-pbr3/config.json -d
```

## Verify Routing

Enable the API in your config (`"api": {"enabled": true}`), then check the routing health:

```bash
curl http://127.0.0.1:8080/api/health/routing
```

A healthy response looks like:

```json
{
  "overall": "ok",
  "firewall_backend": "nftables",
  "firewall": {
    "chain_present": true,
    "prerouting_hook_present": true
  },
  "firewall_rules": [...],
  "route_tables": [...],
  "policy_rules": [...]
}
```

If `overall` is `degraded`, check the individual entries for `missing` or `mismatch` statuses.

{{< callout type="info" >}}
For a complete configuration reference including remote lists, DNS routing, urltest failover, and advanced filtering, see the [Configuration](../configuration/) section.
{{< /callout >}}
