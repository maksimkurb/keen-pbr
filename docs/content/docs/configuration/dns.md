---
title: DNS
weight: 4
---

keen-pbr3 integrates with dnsmasq to route DNS queries for specific domain lists through designated DNS servers. It provides a `generate-resolver-config` subcommand that prints a dnsmasq configuration to stdout, which dnsmasq can consume directly via its `conf-script=` directive.

## Configuration

```json
{
  "dns": {
    "servers": [...],
    "rules": [...],
    "fallback": "google-dns"
  }
}
```

| Field | Type | Description |
|---|---|---|
| `servers` | array | DNS server definitions |
| `rules` | array | Rules mapping lists to DNS servers |
| `fallback` | string | DNS server tag for queries that match no rule |

## DNS Servers

Each server has a tag, an address, and an optional `detour`.

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier for this DNS server |
| `address` | string | yes | IPv4 or IPv6 address of the DNS server |
| `detour` | string | no | Outbound tag to use when querying this server |

The `detour` field binds DNS queries for this server to a specific outbound. This ensures that DNS resolution goes through the same path as the routed traffic.

```json
{
  "servers": [
    {
      "tag": "vpn-dns",
      "address": "10.8.0.1",
      "detour": "vpn"
    },
    {
      "tag": "google-dns",
      "address": "8.8.8.8"
    }
  ]
}
```

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
      "list": ["my-domains", "remote-list"],
      "server": "vpn-dns"
    }
  ]
}
```

## dnsmasq Integration

keen-pbr3 provides the `generate-resolver-config` subcommand that prints `server=` and `ipset=`/`nftset=` directives to stdout for every domain in the configured lists.

Two resolver types are supported:

| Resolver type | Directive style | Use with |
|---|---|---|
| `dnsmasq-ipset` | `ipset=` | iptables/ipset backend |
| `dnsmasq-nftset` | `nftset=` | nftables backend |

Use dnsmasq's `conf-script=` directive to call keen-pbr3 directly — no intermediate file needed:

```
conf-script=/usr/sbin/keen-pbr3 generate-resolver-config dnsmasq-nftset
```

Restart dnsmasq after adding this line. dnsmasq will re-run the script on each reload.

{{< callout type="info" >}}
The `resolver_config_hash` field in `GET /api/health/service` is an MD5 digest of the current domain-to-resolver mapping. You can compare it against the output of `keen-pbr3 resolver-config-hash` to verify dnsmasq is using up-to-date configuration.
{{< /callout >}}

## Complete Example

```json
{
  "dns": {
    "servers": [
      {
        "tag": "vpn-dns",
        "address": "10.8.0.1",
        "detour": "vpn"
      },
      {
        "tag": "google-dns",
        "address": "8.8.8.8"
      }
    ],
    "rules": [
      {
        "list": ["my-domains", "remote-list"],
        "server": "vpn-dns"
      }
    ],
    "fallback": "google-dns"
  }
}
```
