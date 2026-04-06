---
title: DNS
weight: 4
---

keen-pbr integrates with dnsmasq to route DNS queries for specific domain lists through designated DNS servers. It provides a `generate-resolver-config` subcommand that prints a dnsmasq configuration to stdout, which dnsmasq can consume directly via its `conf-script=` directive.

## Configuration

```json
{
  "dns": {
    "dns_test_server": {
      "listen": "127.0.0.88:53"
    },
    "servers": [...],
    "rules": [...],
    "fallback": ["google-dns", "quad9"]
  }
}
```

| Field | Type | Description |
|---|---|---|
| `servers` | array | DNS server definitions |
| `rules` | array | Rules mapping lists to DNS servers |
| `fallback` | array of string | Ordered DNS server tags for queries that match no rule |
| `dns_test_server` | object | Optional built-in DNS probe listener for connectivity checks |

## System Resolver

`dns.system_resolver` configures the dnsmasq integration used by keen-pbr's
daemon runtime.

- It is required for daemon service startup, config reload, and config apply via the API.
- `address` is also the endpoint used by `/api/health/service` to query the TXT record `config-hash.keen.pbr`.
- `address` accepts `host[:port]`; when the port is omitted, keen-pbr queries port `53`.

| Field | Type | Required | Description |
|---|---|---|---|
| `type` | string | yes | Resolver integration type: `dnsmasq-ipset` or `dnsmasq-nftset` |
| `hook` | string | yes | Hook script path used to reload the system resolver |
| `address` | string | yes | Resolver address used for integration and TXT health checks, for example `"127.0.0.1"` or `"127.0.0.1:5353"` |

## DNS Test Server

`dns.dns_test_server` enables a minimal DNS server inside keen-pbr. It listens on
the configured IPv4 `host:port`, accepts both UDP and TCP DNS requests, logs
the queried name, and always replies with one synthetic `A` record.

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

When the HTTP API is enabled, `GET /api/dns/test` exposes these DNS query names
as Server-Sent Events. Each new SSE connection receives `HELLO` first, then one
event per queried DNS name observed while that connection stays open.

## DNS Servers

Each server has a tag, optional `type`, optional `address`, and optional `detour`.

| Field | Type | Required | Description |
|---|---|---|---|
| `tag` | string | yes | Unique identifier for this DNS server |
| `type` | string | no | DNS source type: `static` (default) or `keenetic`. |
| `address` | string | for `static` | IPv4 or IPv6 address of the DNS server, with optional port: `"8.8.8.8"`, `"8.8.8.8:5353"`, `"[::1]:5353"`. Default port: 53. |
| `detour` | string | no | Outbound tag to use when querying this server |

The `detour` field binds DNS queries for this server to a specific outbound. This ensures that DNS resolution goes through the same path as the routed traffic.

```json
{
  "servers": [
    {
      "tag": "vpn-dns",
      "type": "static",
      "address": "10.8.0.1",
      "detour": "vpn"
    },
    {
      "tag": "google-dns",
      "address": "8.8.8.8"
    },
    {
      "tag": "keenetic-dns",
      "type": "keenetic"
    }
  ]
}
```

### `type: keenetic` (built-in router DNS via RCI)

When `type` is set to `keenetic`, `keen-pbr` resolves the DNS server address from Keenetic RCI at startup, on config reload, and after manual restart via UI reload flow.

- Compile-time requirement: `USE_KEENETIC_API=ON`.
- Source of truth endpoint: `GET http://127.0.0.1:79/rci/show/dns-proxy`.
- Data source inside response: `proxy-status[]` entry with `proxy-name == "System"`, field `proxy-config`, first `dns_server = ...` directive.

If Keenetic API support is not compiled in, config with `type: "keenetic"` fails validation with a clear diagnostic.

### How `detour` works

When `detour` is set, keen-pbr installs a firewall mark rule for UDP **and**
TCP traffic whose destination is `<server.address>:<server.port>` (default port
53). The rule marks those packets with the fwmark of the referenced outbound,
so the kernel routes DNS queries through that outbound's routing table — the
same path as the tunnelled traffic.

Rules are installed on both the **iptables** and **nftables** backends and are
rebuilt on every `full_reload()` (SIGHUP, config API reload, or urltest
selection change).

`urltest` outbounds are supported: the rule always uses the fwmark of the
currently selected child, so DNS detour follows interface failover automatically.

`blackhole` and `ignore` outbounds cannot be used as `detour` targets and are
rejected at config validation time.

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

keen-pbr provides the `generate-resolver-config` subcommand that prints global fallback `server=` directives plus per-list `server=` and `ipset=`/`nftset=` directives to stdout.

Two resolver types are supported:

| Resolver type | Directive style | Use with |
|---|---|---|
| `dnsmasq-ipset` | `ipset=` | iptables/ipset backend |
| `dnsmasq-nftset` | `nftset=` | nftables backend |

Use dnsmasq's `conf-script=` directive to call keen-pbr directly — no intermediate file needed:

```
conf-script=/usr/sbin/keen-pbr generate-resolver-config dnsmasq-nftset
```

Restart dnsmasq after adding this line. dnsmasq will re-run the script on each reload.

{{< callout type="info" >}}
To verify dnsmasq is running with up-to-date configuration, compare the hash from keen-pbr against the TXT record dnsmasq exposes (written by `generate-resolver-config`):

```bash {filename="bash"}
# Hash known to keen-pbr
curl -s http://127.0.0.1:8080/api/health/service | grep resolver_config_hash

# Hash dnsmasq is currently using
dig +short TXT config-hash.keen.pbr @127.0.0.1
# or: nslookup -type=TXT config-hash.keen.pbr 127.0.0.1
```

If the two values differ, dnsmasq has not picked up the latest configuration — restart dnsmasq to reload.
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
    "fallback": ["google-dns", "quad9"]
  }
}
```
