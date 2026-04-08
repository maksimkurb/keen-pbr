---
title: Quick Start
weight: 2
---

This guide walks you through a minimal working configuration that routes traffic from a domain list through a VPN interface.

## Web UI Setup

If you installed the full `keen-pbr` package, you can complete the initial setup from the Web UI:

1. Open `http://<router-ip>:12121/` in your browser, or http://my.keenetic.net:12121 for Keenetic / NetCraze routers.
2. Go to **Outbounds** and create your outbound connections. See [Outbounds]({{< ref "/docs/configuration/outbounds.md" >}}) for more info. You can configure the following types of outbounds:
    - **interface**: routes all matched traffic via specified interface (and gateway IP if supplied)
    - **urltest**: periodically checks interface latency and chooses the best interface
    - **table**: routes all matched traffic via existing iproute table (e.g. table "main", which has ID `254`)
    - **blackhole**: blocks all matched traffic
    - **ignore**: ignores all matched traffic, making it follow default ip rules and routes
3. Go to **DNS Servers** and create at least one DNS server.
4. Go to **Lists** and create your lists.
5. Go to **Routing rules** and create your routing rules.
6. Go to **DNS Rules** and configure the default upstream servers.
7. Optionally, add DNS rules on the **DNS Rules** page.

{{< callout type="info" >}}
The `keen-pbr-headless` package does not include the Web UI or API.
{{< /callout >}}

## Minimal Configuration

Create `/etc/keen-pbr/config.json`:

```json
{
  "daemon": {
    "pid_file": "/var/run/keen-pbr.pid",
    "cache_dir": "/var/cache/keen-pbr"
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
    "my_domains": {
      "domains": ["example.com", "*.example.org"]
    }
  },
  "dns": {
    "system_resolver": {
      "type": "dnsmasq-nftset",
      "hook": "/usr/lib/keen-pbr/dnsmasq.sh",
      "address": "127.0.0.1"
    }
  },
  "route": {
    "rules": [
      {
        "list": ["my_domains"],
        "outbound": "vpn"
      }
    ],
    "fallback": "wan"
  }
}
```

This config:
- Defines two outbounds: `vpn` (tun0) and `wan` (eth0)
- Creates a list `my_domains` with two inline domain entries
- Configures `dns.system_resolver` for dnsmasq integration
- Routes all traffic matching `my_domains` through `vpn`
- Falls back to `wan` for everything else

## Run the Daemon

```bash {filename="bash"}
keen-pbr --config /etc/keen-pbr/config.json
```

Add `-d` to run as a background daemon:

```bash {filename="bash"}
keen-pbr --config /etc/keen-pbr/config.json -d
```

## Verify Routing

Enable the API in your config (`"api": {"enabled": true}`), then check the routing health:

```bash {filename="bash"}
curl http://127.0.0.1:12121/api/health/routing
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
