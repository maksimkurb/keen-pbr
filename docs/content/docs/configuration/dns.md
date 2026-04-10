---
title: DNS
weight: 4
---

Use DNS settings when you want domains in a list to be resolved through a specific DNS server, usually the same VPN that will carry the matching traffic.

On package-based router installs, keen-pbr normally takes care of dnsmasq integration for you. Most users only need to define:

- `system_resolver`
- `servers`
- `rules`
- `fallback`

## Configuration

```json
{
  "dns": {
    "system_resolver": {
      "type": "dnsmasq-nftset",
      "hook": "/usr/lib/keen-pbr/dnsmasq.sh",
      "address": "127.0.0.1"
    },
    "servers": [...],
    "rules": [...],
    "fallback": ["google_dns", "quad9"]
  }
}
```

| Field | Type | Description |
|---|---|---|
| `system_resolver` | object | How keen-pbr refreshes dnsmasq on the system |
| `servers` | array | DNS server definitions |
| `rules` | array | Rules mapping lists to DNS servers |
| `fallback` | array of string | Ordered DNS server tags for queries that match no rule |
| `dns_test_server` | object | Optional built-in DNS probe listener for advanced troubleshooting |

## System Resolver

`dns.system_resolver` tells keen-pbr how to refresh dnsmasq after configuration changes.

On normal router package installs, you usually keep this configured exactly as shown in the package examples.

| Field | Type | Required | Description |
|---|---|---|---|
| `type` | string | yes | Resolver integration type: `dnsmasq-ipset` or `dnsmasq-nftset` |
| `address` | string | yes | Resolver address used for integration and TXT health checks, for example `"127.0.0.1"` or `"127.0.0.1:5353"` |

## DNS Test Server

`dns.dns_test_server` is optional. It is mainly useful when you are troubleshooting DNS and want keen-pbr to expose a simple test DNS listener.

| Field | Type | Required | Description |
|---|---|---|---|
| `listen` | string | yes | IPv4 listen address in `host:port` form, for example `"127.0.0.88:53"` |
| `answer_ipv4` | string | no | IPv4 address returned in the `A` answer. Defaults to the host part of `listen`. |

```json
{
  "dns": {
    "dns_test_server": {
      "listen": "127.0.0.88:53"
    }
  }
}
```

When the HTTP API is enabled, `GET /api/dns/test` can stream the DNS queries seen by this test server.

## DNS Servers

Each server has a tag, optional `type`, optional `address`, and optional `detour`.

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier for this DNS server |
| `type` | string | no | DNS source type: `static` (default) or `keenetic` |
| `address` | string | for `static` | IP address of the DNS server, with optional port |
| `detour` | string | no | Outbound to use when contacting this DNS server |

The `detour` field is useful when a DNS server must be reached through a specific connection, usually the same VPN that will carry the matching traffic.

```json
{
  "servers": [
    {
      "tag": "vpn_dns",
      "type": "static",
      "address": "10.8.0.1",
      "detour": "vpn"
    },
    {
      "tag": "google_dns",
      "address": "8.8.8.8"
    },
    {
      "tag": "keenetic_dns",
      "type": "keenetic"
    }
  ]
}
```

### `type: keenetic` (built-in router DNS via RCI)

On Keenetic routers, `type: "keenetic"` tells keen-pbr to reuse the router's current built-in DNS settings automatically.

### How `detour` works

When `detour` is set, keen-pbr makes sure DNS queries for that server leave through the selected outbound.

For example, if `vpn_dns` has `detour: "vpn"`, then the DNS requests to `vpn_dns` will also go through `vpn`.

## DNS Rules

Rules map list names to a DNS server tag. Domains from the specified lists are resolved using the specified server.

| Field | Type | Required | Description |
|---|---|---|---|
| `list` | array of string | yes | List names whose domains should be resolved by this server |
| `server` | string | yes | DNS server tag to use for matched domains |

```json
{
  "rules": [
    {
      "list": ["my_domains", "remote_list"],
      "server": "vpn_dns"
    }
  ]
}
```

## dnsmasq Integration

On packaged router installs, you usually do not need to configure dnsmasq manually.

{{% details title="Manual dnsmasq integration (advanced)" closed="true" %}}
keen-pbr provides the `generate-resolver-config` subcommand that prints dnsmasq configuration to stdout.

Two resolver types are supported:

| Resolver type | Directive style | Use with |
|---|---|---|
| `dnsmasq-ipset` | `ipset=` | iptables/ipset backend |
| `dnsmasq-nftset` | `nftset=` | nftables backend |

Example dnsmasq integration:

```text
conf-script=/usr/sbin/keen-pbr generate-resolver-config dnsmasq-nftset
```

Restart dnsmasq after adding this line.
{{% /details %}}

## Complete Example

```json
{
  "dns": {
    "servers": [
      {
        "tag": "vpn_dns",
        "address": "10.8.0.1",
        "detour": "vpn"
      },
      {
        "tag": "google_dns",
        "address": "8.8.8.8"
      }
    ],
    "rules": [
      {
        "list": ["my_domains", "remote_list"],
        "server": "vpn_dns"
      }
    ],
    "fallback": ["google_dns", "quad9"]
  }
}
```

{{% details title="Under the hood: how domain-based routing works" closed="true" %}}
When a domain in a matched list is resolved, dnsmasq adds the resulting IP address to an internal set used by keen-pbr. Traffic to that IP can then be routed through the correct outbound.
{{% /details %}}
