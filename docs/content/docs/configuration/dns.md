---
title: DNS
weight: 4
---

Domain-based routing works without any DNS configuration: keen-pbr intercepts DNS responses and TLS/HTTP/QUIC traffic itself and fills the sets of your lists (see the [interception architecture](https://github.com/maksimkurb/keen-pbr/blob/main/docs/dns-interception.md)). No resolver is touched.

Use the DNS settings below only when you want domains in a list to be resolved through a specific DNS server, usually the same VPN that will carry the matching traffic. That requires the optional **dnsmasq integration**: set `dns.resolver_integration` to `"dnsmasq"` and keen-pbr manages a dnsmasq configuration for you (per-list upstream DNS, Keenetic static entries, fallback servers, rebind exceptions). dnsmasq is not installed by the keen-pbr package any more; install it yourself when you need this.

## Configuration

```json { filename="config.json" }
{
  "dns": {
    "resolver_integration": "dnsmasq",
    "system_resolver": {
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
| `resolver_integration` | string | `none` (default) or `dnsmasq`. With `none` keen-pbr never touches the system resolver; per-list `rules`/`fallback` and `system_resolver` are inactive, while server definitions (including `detour`) remain meaningful for ordinary DNS traffic. |
| `system_resolver` | object | How keen-pbr refreshes dnsmasq on the system (required for `dnsmasq`) |
| `servers` | array | DNS server definitions |
| `rules` | array | Rules mapping lists to DNS servers |
| `fallback` | array of string | Ordered DNS server tags for queries that match no rule |
| `dns_test_server` | object | Deprecated and ignored (replaced by `intercept.dns.marker`) |

## Resolver Integration

`dns.resolver_integration` selects how keen-pbr works with the system resolver.

| Value | Behaviour |
|---|---|
| `none` | Default. The daemon fills the dynamic sets by itself. dnsmasq is neither configured, restarted nor required. |
| `dnsmasq` | keen-pbr installs a `conf-script` hook into dnsmasq, generates its configuration and verifies it through the `config-hash.keen.pbr` TXT record. When interception is unavailable on the device, dnsmasq also fills the dynamic sets (`ipset=`/`nftset=` directives). |

Configs written before this option existed are migrated automatically: if `resolver_integration` is absent and the config has non-empty `dns.rules` or a `dns.system_resolver`, `dnsmasq` is used (and written explicitly on the next save); otherwise `none`. Defining `dns.rules` with `none` logs a warning because per-list upstream DNS requires `dnsmasq`.

Switching from `dnsmasq` to `none` removes the keen-pbr hook from dnsmasq and restarts it. Make sure dnsmasq (or whatever resolver you use) has upstream servers of its own afterwards.

## System Resolver

`dns.system_resolver` tells keen-pbr how to check dnsmasq state after configuration changes. It is only used (and required) with `resolver_integration: "dnsmasq"`.

On normal router package installs, you usually should not change these settings.

| Field | Type | Required | Description |
|---|---|---|---|
| `address` | string | yes | Resolver address used for integration and TXT health checks, for example `"127.0.0.1"` or `"127.0.0.1:5353"` |

## DNS Test Server (deprecated)

`dns.dns_test_server` is deprecated: the option is still accepted but ignored
(a warning is logged). The built-in probe listener was replaced by the
interceptor marker `intercept.dns.marker` (default `check.keen.pbr`, answered
with `127.0.0.88`). A UDP `nslookup check.keen.pbr` marker check works only
when DNS hold and the supported marker replacement capability are active. When
the HTTP API is enabled, you can inspect the result through the Web UI/SSE
events.

## DNS interception

The `intercept` section is enabled by default when the required kernel
capabilities are available. DNS uses NFQUEUE (queue `9053`, 30 ms userspace
processing budget); L7 uses NFLOG (group `9054`) for TLS SNI, HTTP Host and QUIC
Initial metadata. The effective DNS TTL range is 300–86400 seconds. Configure
these values only when the defaults do not suit the device:

```json
{
  "intercept": {
    "enabled": true,
    "min_ttl_s": 300,
    "max_ttl_s": 86400,
    "dns": {
      "enabled": true,
      "queue_num": 9053,
      "hold_timeout_ms": 30,
      "marker": {
        "domain": "check.keen.pbr",
        "answer_ipv4": "127.0.0.88"
      }
    },
    "l7": {
      "enabled": true,
      "nflog_group": 9054,
      "tls": true,
      "http": true,
      "quic": true
    }
  }
}
```

The 30 ms value is a processing budget, not a kernel guarantee. A listener
that is already bound but stalled can hold queued packets; `queue-bypass` only
covers an absent listener. During shutdown keen-pbr removes queueing rules
before stopping the listener, but closing a queue with pending entries can
drop them. See [DNS and L7 interception](https://github.com/maksimkurb/keen-pbr/blob/main/docs/dns-interception.md) for
the marker user-namespace requirement, counters, capability health, and other
failure semantics.

## DNS Servers

Each server has a tag, optional `type`, optional `address`, and optional `detour`.
DNS server `tag` values must match `^[a-z][a-z0-9_]*$`, be at most 24 characters, and must be unique.

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier for this DNS server |
| `type` | string | no | DNS source type: `static` (default) or `keenetic` |
| `address` | string | for `static` | IP address of the DNS server, with optional port, for example `"10.8.0.1"`, `"10.8.0.1:5353"`, `"2001:4860:4860::8888"`, or `"[2001:4860:4860::8888]:5353"` |
| `detour` | string | no | Outbound to use when contacting this DNS server |

The `detour` field is useful when a DNS server must be reached through a specific connection, usually the same VPN that will carry the matching traffic.

```json { filename="config.json" }
{
  "dns": {
    "servers": [
      {
        "tag": "vpn_dns",
        "type": "static",
        "address": "10.8.0.1:5353",
        "detour": "vpn"
      },
      {
        "tag": "google_dns",
        "address": "8.8.8.8"
      },
      {
        "tag": "google_dns_v6",
        "address": "[2001:4860:4860::8888]:53"
      },
      {
        "tag": "keenetic_dns",
        "type": "keenetic"
      }
    ]
  }
}
```

### `type: keenetic` (built-in router DNS via RCI)

On Keenetic routers, `type: "keenetic"` tells keen-pbr to reuse the router's current built-in DNS settings automatically.

Rules and behavior:

- At most one `dns.servers` entry may use `type: "keenetic"`.
- `address` must not be set for `type: "keenetic"` (resolved from RCI).
- keen-pbr reads unscoped `dns_server = ...` entries from the **System** proxy policy.
- If unscoped encrypted upstreams (DoH/DoT) are present, all of them are used in order.
- Otherwise, all unscoped plaintext upstreams are used in order.
- `static_a` / `static_aaaa` entries from the System policy are also propagated to generated dnsmasq config.

### How `detour` works

When `detour` is set, keen-pbr makes sure DNS queries for that server leave through the selected outbound; keen-pbr would automatically create firewall rule for specified DNS IP and port. This can also affect other clients in your network that trying to contact this DNS server directly.

For example, if `vpn_dns` has `detour: "vpn"`, then the DNS requests to `vpn_dns` will also go through `vpn`.

## DNS Rules

Rules map list names to a DNS server tag. Domains from the specified lists are resolved using the specified server.

| Field | Type | Required | Description |
|---|---|---|---|
| `enabled` | boolean | no | Whether this rule is active. `false` disables it. `true`, omitted, or `null` all mean enabled. |
| `list` | array of string | yes | List names whose domains should be resolved by this server |
| `server` | string | yes | DNS server tag to use for matched domains |

```json { filename="config.json" }
{
  "dns": {
    "rules": [
      {
        "enabled": true,
        "list": ["my_domains", "remote_list"],
        "server": "vpn_dns"
      }
    ]
  }
}
```

If `enabled` is omitted or set to `null`, the DNS rule is still treated as enabled.

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

```json { filename="config.json" }
{
  "dns": {
    "servers": [
      {
        "tag": "vpn_dns",
        "address": "10.8.0.1:5353",
        "detour": "vpn"
      },
      {
        "tag": "google_dns",
        "address": "8.8.8.8"
      },
      {
        "tag": "google_dns_v6",
        "address": "[2001:4860:4860::8888]:53"
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
